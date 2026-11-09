"""Test automation for SFV Ember: deploy, launch, control, capture.

Shared by the command line (sfve.py) and the MCP server (sfve_mcp.py).
Windows only; standard library only.

Two ways to start a copy:
* through SFV Ember's Launcher.exe (SFVE_VIA=launcher), as players do: the
  launcher starts the game with the mod injected from its own folder
  (SFVE_PACKAGE_DIR: a staged package, or the build tree), and nothing needs
  to be in the game folder. Copy N > 0 is the launcher's test instance
  (--instance N); the test's game options go through --game-option;
* straight to the exe, with the xinput1_3.dll proxy deployed beside it
  (SFVE_VIA=proxy, the default: deploy first).

Safety rules this module enforces:
* A copy is only launched with the mod in place (the proxy and the mod
  deployed, or the launcher, the mod and the helper in the package folder),
  and is killed if its control pipe (which the mod opens only after the
  network guard is installed) does not answer within GUARD_TIMEOUT_S. Through
  the launcher the clock has two phases: the launcher's own preparation (the
  Steam check, up to LAUNCHER_PREP_TIMEOUT_S) ends when the launcher has
  started the game, or when its log reports the Steam check as failed (the
  launcher then stays up on a recovery window), and only then does the
  GUARD_TIMEOUT_S guard deadline run.
* Nothing here uses the game's online modes.
* Only copies this module started are killed. A process is known by its id
  and its creation time together (a bare id can be reused), kept in the
  registry of the run that started it: a copy whose pipe answers is ours
  only when its process is the one just started or descends from it, and
  kill_all ends a registered process only while its creation time still
  matches. Steam is never ended: steam.exe, steamwebhelper.exe and
  SteamService.exe, and what they started, are outside every process tree
  this module ends, even when the launcher started them.
* Every process this module starts or ends belongs to a run, and the run is
  mandatory: registry_path() raises without one, so there is no shared
  default registry. A runner script sets SFVE_RUN (localstack.ps1,
  runroom.ps1) or a Python runner calls begin_run(); the two clients with no
  runner around them, sfve.py and sfve_mcp.py, call use_run("cli") and
  use_run("mcp"): SFVE_RUN when set, otherwise that fixed name. The run is
  never inferred from the caller's process ancestry, so it is the same
  however each command is started (a new shell per command included): a later
  `sfve.py kill` finds what an earlier `sfve.py launch` started, a restarted
  MCP server finds its predecessor's copies, and the CLI, the MCP server and
  each runner never meet. Two people or agents using the CLI on one PC share
  the "cli" run: give each its own SFVE_RUN. Helper processes of a run (a
  room supervisor and the room hosts it starts) are registered with track(),
  by id and creation time like the copies. A run that died is cleaned up
  with `sfve.py kill --all`.
* The game folder is only written in proxy mode, by deploy, which records
  what it installed in MANIFEST; undeploy removes only the files listed there.
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import NamedTuple

GAME_DIR = Path(os.environ.get(
    "SFVE_GAME_DIR",
    r"G:\SteamLibrary\steamapps\common\StreetFighterV\StreetFighterV\Binaries\Win64"))
BUILD_DIR = Path(os.environ.get("SFVE_BUILD_DIR", r"D:\sfv-ember-build"))
GAME_EXE = GAME_DIR / "StreetFighterV.exe"
MOD_FILES = ("xinput1_3.dll", "sfv_ember.dll", "sf4-net.exe")  # sf4-net: scripts/build-helper.ps1
# Ember's own pictures the shell draws (the menu backdrop, button prompts and
# flags), which the build copies from the Ember library into its assets/.
ASSETS = "assets"
GUARD_TIMEOUT_S = 20.0
# The launcher's preparation before the game exists: the Steam check waits up
# to 60 s for the old client to exit and 90 s for the new one's sign-in
# (kExitWaitMs and kRecordWaitMs in src/launcher/SteamSession.cxx: keep these
# two in step), then the game starts. A failed check is normally reported
# from the launcher's log long before; this is the backstop.
STEAM_EXIT_WAIT_S = 60.0
STEAM_SIGN_IN_WAIT_S = 90.0
LAUNCHER_PREP_SLACK_S = 30.0
LAUNCHER_PREP_TIMEOUT_S = STEAM_EXIT_WAIT_S + STEAM_SIGN_IN_WAIT_S + LAUNCHER_PREP_SLACK_S
STEAM_APP_ID = "310950"
VIA = os.environ.get("SFVE_VIA", "proxy")
PACKAGE_DIR = Path(os.environ.get("SFVE_PACKAGE_DIR", str(BUILD_DIR)))
LAUNCHER_FILES = ("Launcher.exe", "sfv_ember.dll", "sf4-net.exe")
# The mod's logs (mod/Log.cxx) and the launcher's, however the game started.
LOG_DIR = Path(os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))) / "SFVEmber" / "logs"
# What deploy installed in the game folder (proxy mode), relative to it.
MANIFEST = "sfve-deployed.json"


class SfveError(RuntimeError):
    pass


def runtime_dir() -> Path:
    """Where a copy loads its runtime files from, by the one mode switch: the
    package or build folder through the launcher, the game folder (where
    deploy puts them) in proxy mode."""
    return PACKAGE_DIR if VIA == "launcher" else GAME_DIR


def runtime_file(name: str) -> Path:
    """The runtime file `name` (a helper such as sf4-net.exe) in runtime_dir(),
    which must exist."""
    path = runtime_dir() / name
    if not path.is_file():
        where = "SFVE_VIA=launcher, SFVE_PACKAGE_DIR" if VIA == "launcher" else "SFVE_VIA=proxy, the game folder (deploy first)"
        raise SfveError(f"{path} is missing ({where})")
    return path


def verdict(failure: str | list[str], pass_text: str = "PASS") -> int:
    """The one verdict rule of the acceptance runners: prints PASS or
    FAIL (`failure`: a reason, or a list of them) and returns the exit code.
    A runner calls it after its cleanup, with a failure for every missing
    piece of evidence as well as for every difference found."""
    reasons = [failure] if isinstance(failure, str) else list(failure)
    reasons = [r for r in reasons if r]
    print("FAIL: " + "; ".join(reasons) if reasons else pass_text)
    return 1 if reasons else 0


# --- deploy -----------------------------------------------------------------

def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _deployed_files() -> list[str]:
    """The proxy, the mod, its helper and its assets, relative to the build."""
    assets = BUILD_DIR / ASSETS
    pictures = [str(p.relative_to(BUILD_DIR)) for p in sorted(assets.rglob("*")) if p.is_file()] if assets.exists() else []
    return list(MOD_FILES) + pictures


def _installed() -> list[str]:
    """What an earlier deploy recorded as installed, relative to the game folder."""
    try:
        return [str(name) for name in json.loads((GAME_DIR / MANIFEST).read_text())["files"]]
    except (OSError, ValueError, KeyError, TypeError):
        return []


def _require_game_stopped(action: str) -> None:
    """The game holds the proxy and the mod open: they cannot be replaced or removed under it."""
    if running_pids():
        raise SfveError(f"StreetFighterV is running (pid {', '.join(map(str, running_pids()))}): stop it before {action}")


def deploy(adopt: bool = False) -> list[str]:
    """Copies the built proxy, mod and assets next to the game exe (proxy
    mode only; the launcher needs nothing in the game folder) and records
    them in MANIFEST. A file of the same name that deploy did not install is
    refused, unless `adopt` takes it over. Returns the files copied."""
    if VIA == "launcher":
        raise SfveError("SFVE_VIA=launcher starts the game from the package folder: nothing is installed in the game folder")
    installed = set(_installed())
    copied = []
    for name in _deployed_files():
        src, dst = BUILD_DIR / name, GAME_DIR / name
        if not src.exists():
            raise SfveError(f"{src} is not built")
        if dst.exists() and _sha256(src) == _sha256(dst):
            continue
        if dst.exists() and name not in installed and not adopt:
            raise SfveError(f"{dst} was not installed by deploy: remove it, or deploy with --adopt to replace it")
        copied.append(name)
    if copied:
        _require_game_stopped("deploying")
        # Recorded before the copies, so a deploy that stops half way is still undone.
        (GAME_DIR / MANIFEST).write_text(json.dumps({"build": str(BUILD_DIR), "files": sorted(installed | set(copied))}, indent=1))
        for name in copied:
            (GAME_DIR / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(BUILD_DIR / name, GAME_DIR / name)
    return copied


def undeploy() -> list[str]:
    """Removes the files MANIFEST lists, and the folders deploy made once
    they are empty (logs and everything else in the game folder stay)."""
    root = GAME_DIR.resolve()
    owned = [(name, (GAME_DIR / name).resolve()) for name in _installed()]
    for name, path in owned:
        if not path.is_relative_to(root) or path == root:
            raise SfveError(f"{MANIFEST} lists {name}, which is not inside the game folder: not removing anything")
    if any(path.is_file() for _, path in owned):
        _require_game_stopped("removing the mod")
    removed = []
    for name, path in owned:
        if path.is_file():
            path.unlink()
            removed.append(name)
        for folder in path.parents:
            if folder == root:
                break
            try:
                folder.rmdir()  # only while empty
            except OSError:
                break
    (GAME_DIR / MANIFEST).unlink(missing_ok=True)
    return removed


def deployed_matches_build() -> bool:
    return all((GAME_DIR / n).exists() and _sha256(GAME_DIR / n) == _sha256(BUILD_DIR / n) for n in _deployed_files())


# --- control pipe -------------------------------------------------------------

def pipe_path(instance: int) -> str:
    return rf"\\.\pipe\sfve-{instance}"


def request(instance: int, line: str, timeout_s: float = 5.0) -> str:
    """Sends one command line to a running copy; returns its reply line."""
    deadline = time.monotonic() + timeout_s
    while True:
        try:
            with open(pipe_path(instance), "r+b", buffering=0) as pipe:
                pipe.write(line.encode() + b"\n")
                reply = b""
                while not reply.endswith(b"\n"):
                    chunk = pipe.read(65536)
                    if not chunk:
                        break
                    reply += chunk
                text = reply.decode(errors="replace").strip()
                if text:
                    return text
                # A copy that is closing can accept and then drop the line.
                raise OSError("empty reply")
        except (FileNotFoundError, OSError):
            if time.monotonic() > deadline:
                raise SfveError(f"instance {instance} is not answering on {pipe_path(instance)}")
            time.sleep(0.1)


def _reply_pid(reply: str) -> int:
    """The game's own process id in a status reply."""
    return int(reply.split('"pid":', 1)[1].split(",", 1)[0])


def status_pid(index: int) -> int:
    reply = request(index, "status")
    if not reply.startswith("ok "):
        raise SfveError(f"instance {index}: {reply}")
    return _reply_pid(reply)


# --- processes ----------------------------------------------------------------

@dataclass
class Instance:
    index: int
    pid: int


def running_pids() -> list[int]:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq StreetFighterV.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    return [int(line.split('","')[1]) for line in out.splitlines() if line.startswith('"StreetFighterV.exe"')]


# A process id is reused once its process ends; an id together with the
# process's creation time (a FILETIME) is not. That pair is an Identity.
Identity = tuple[int, int]

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_PROCESS_TERMINATE, _PROCESS_QUERY_LIMITED_INFORMATION = 0x0001, 0x1000
_TH32CS_SNAPPROCESS, _STILL_ACTIVE = 0x2, 259


class _ProcessEntry(ctypes.Structure):  # PROCESSENTRY32W
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
                ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", wt.LONG), ("dwFlags", wt.DWORD),
                ("szExeFile", wt.WCHAR * 260)]


_kernel32.OpenProcess.restype = wt.HANDLE
_kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
_kernel32.CloseHandle.argtypes = [wt.HANDLE]
_kernel32.GetProcessTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(wt.FILETIME)] * 4
_kernel32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
_kernel32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
_kernel32.CreateToolhelp32Snapshot.restype = wt.HANDLE
_kernel32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
_kernel32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(_ProcessEntry)]
_kernel32.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(_ProcessEntry)]


def _creation_time(handle) -> int:
    times = [wt.FILETIME() for _ in range(4)]  # creation, exit, kernel, user
    if not _kernel32.GetProcessTimes(handle, *(ctypes.byref(t) for t in times)):
        return 0
    return (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime


def process_identity(pid: int) -> Identity | None:
    """The identity of the running process `pid`, or None when none runs."""
    handle = _kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        code = wt.DWORD()
        if not _kernel32.GetExitCodeProcess(handle, ctypes.byref(code)) or code.value != _STILL_ACTIVE:
            return None
        created = _creation_time(handle)
        return (pid, created) if created else None
    finally:
        _kernel32.CloseHandle(handle)


class _Process(NamedTuple):
    parent: int
    created: int  # creation time; 0 when unreadable
    name: str     # the exe's file name, lower case


# Steam is shared by every game and every player on the PC: a test copy
# never owns it, even when the launcher's preparation had it started.
_SHARED_PROCESSES = frozenset({"steam.exe", "steamwebhelper.exe", "steamservice.exe"})


def _process_table() -> dict[int, _Process]:
    """Every running process, by id."""
    snapshot = _kernel32.CreateToolhelp32Snapshot(_TH32CS_SNAPPROCESS, 0)
    if snapshot in (None, ctypes.c_void_p(-1).value):
        raise SfveError("could not list the running processes")
    entry = _ProcessEntry()
    entry.dwSize = ctypes.sizeof(entry)
    table = {}
    try:
        more = _kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            table[entry.th32ProcessID] = _Process(entry.th32ParentProcessID, (process_identity(entry.th32ProcessID) or (0, 0))[1],
                                                  entry.szExeFile.lower())
            more = _kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        _kernel32.CloseHandle(snapshot)
    return table


def _process_tree(table: dict[int, _Process], root: Identity) -> list[Identity]:
    """`root` and every process that descends from it, or [] when `root`
    is not running. A process only descends from a parent that was created
    before it: a parent id that has since been reused does not count. Steam
    (_SHARED_PROCESSES) and what it started are not part of the tree, for
    ending processes and for asking who descends from whom alike; a root that
    is Steam has no tree."""
    found = table.get(root[0])
    if found is None or found.created != root[1] or found.name in _SHARED_PROCESSES:
        return []
    members = {root[0]: root[1]}
    grew = True
    while grew:
        grew = False
        for pid, p in table.items():
            if (pid not in members and p.created and p.parent in members and p.created >= members[p.parent]
                    and p.name not in _SHARED_PROCESSES):
                members[pid] = p.created
                grew = True
    return list(members.items())


def _terminate(identity: Identity) -> bool:
    """Ends the process, if it is still the one `identity` names (checked on the handle that ends it)."""
    handle = _kernel32.OpenProcess(_PROCESS_TERMINATE | _PROCESS_QUERY_LIMITED_INFORMATION, False, identity[0])
    if not handle:
        return False
    try:
        return _creation_time(handle) == identity[1] and bool(_kernel32.TerminateProcess(handle, 1))
    finally:
        _kernel32.CloseHandle(handle)


# --- the run's registry -------------------------------------------------------

def begin_run() -> str:
    """Gives this process (and what it starts) a registry of its own, so
    runners do not kill each other's copies; a SFVE_RUN already set stays."""
    return os.environ.setdefault("SFVE_RUN", f"{os.getpid()}-{int(time.time())}")


def use_run(default: str) -> str:
    """The run of a client with no runner around it: SFVE_RUN when set,
    otherwise the client's fixed name `default` ("cli" or "mcp"), set in the
    environment. Nothing is inferred, so every command of that client lands
    in the same registry however it was started."""
    return os.environ.setdefault("SFVE_RUN", default)


def _registry_dir() -> Path:
    return Path(os.environ.get("TEMP", str(BUILD_DIR)))


def _safe_name(text: str) -> str:
    return re.sub(r'[^A-Za-z0-9_.-]', '_', text)


def registry_path() -> Path:
    """The registry folder of this run: one file per entry, so launches that
    overlap never write the same file. Run ownership is mandatory: with no
    SFVE_RUN there is no registry (see the module docstring), so nothing is
    started or ended."""
    run = os.environ.get("SFVE_RUN")
    if not run:
        raise SfveError("no run: set SFVE_RUN, or call begin_run() / use_run() first")
    return _registry_dir() / f"sfve-launched-{_safe_name(run)}"


def _read_entry(path: Path) -> list[Identity]:
    try:
        return [(int(pid), int(created)) for pid, created in json.loads(path.read_text())]
    except (OSError, ValueError, TypeError):
        return []


def _registered(folder: Path) -> dict[str, list[Identity]]:
    """The copies a run started, by index: the identities of the process
    started and of the game it turned out to be."""
    try:
        return {entry.stem: _read_entry(entry) for entry in folder.glob("*.json")}
    except OSError:
        return {}


def _remember(key: int | str, *identities: Identity) -> None:
    folder = registry_path()
    folder.mkdir(parents=True, exist_ok=True)
    entry = folder / f"{_safe_name(str(key))}.json"
    # Kept beside an earlier entry under the key this run made: it is still ours to end.
    merged = list(dict.fromkeys(_read_entry(entry) + list(identities)))
    # Written beside the entry and moved over it, so a reader never sees half a file.
    temp = entry.with_suffix(f".{os.getpid()}.tmp")
    temp.write_text(json.dumps(merged))
    os.replace(temp, entry)


def track(label: str, pid: int) -> list[int]:
    """Registers a process this run started that is not a game (a room
    supervisor, a bridge) and what descends from it now, by id and creation
    time, so kill_all ends them: the room hosts a supervisor starts outlive
    it when it dies, so call this again while it runs. `pid` must be running.
    Returns the ids registered."""
    root = process_identity(pid)
    if root is None:
        raise SfveError(f"process {pid} is not running: nothing to track as {label}")
    members = _process_tree(_process_table(), root)
    _remember(f"track-{label}", *members)
    return sorted(p for p, _ in members)


def _game_options(index: int, mode: str, extra: list[str], windowed: bool) -> list[str]:
    options = [f"-sfve_instance={index}", f"-sfve_mode={mode}"]
    if windowed:
        # SFVE_RES ("1920x1080") sets the window's size; 960x540 by default.
        width, height = os.environ.get("SFVE_RES", "960x540").lower().split("x")
        options += ["-windowed", f"-ResX={int(width)}", f"-ResY={int(height)}"]
    return options + extra


def launch(index: int, mode: str = "probe", extra: list[str] | None = None, windowed: bool = True) -> Instance:
    """Starts one copy and waits until its guard is confirmed (or kills it).
    Through the launcher that is two waits: the launcher's preparation until
    it has started the game (LAUNCHER_PREP_TIMEOUT_S), then the game's start-up
    until the guard answers (GUARD_TIMEOUT_S)."""
    registry_path()  # the run is mandatory: refuse before anything starts
    options = _game_options(index, mode, extra or [], windowed)
    set_aside(log_path(index))  # each run starts its own log; the last one is kept
    if VIA == "launcher":
        missing = [name for name in LAUNCHER_FILES if not (PACKAGE_DIR / name).exists()]
        if missing:
            raise SfveError(f"{PACKAGE_DIR} has no {', '.join(missing)}: build or stage SFV Ember first")
        # The test's options come first on the game's command line, so they
        # take the place of the launcher's own (ModConfig reads the first).
        launcher_log = _LogCursor(launcher_log_path(index))  # before the launcher can write to it
        args = [str(PACKAGE_DIR / "Launcher.exe")]
        if index:
            args += ["--instance", str(index)]
        args += [f"--game-option={option}" for option in options if not option.startswith("-sfve_instance=")]
        proc = subprocess.Popen(args, cwd=PACKAGE_DIR, creationflags=subprocess.DETACHED_PROCESS)
    else:
        if not deployed_matches_build():
            raise SfveError("the game folder does not hold the current build: run deploy first")
        # Steam sets these when it starts a game; its DRM wrapper exits (0x35) without them.
        env = dict(os.environ, SteamAppId=STEAM_APP_ID, SteamGameId=STEAM_APP_ID)
        proc = subprocess.Popen([str(GAME_EXE)] + options, cwd=GAME_DIR, env=env, creationflags=subprocess.DETACHED_PROCESS)
    # The process just started: the launcher, or the game itself.
    started = process_identity(proc.pid)
    if started is None:
        raise SfveError(f"instance {index}: the process just started ended at once (exit code {proc.poll()})")
    _remember(index, started)

    def refuse(why: str) -> SfveError:
        _kill_tree([started])
        return SfveError(f"instance {index} {why}; killed")

    if VIA == "launcher":
        # The game is the launcher's child once the preparation (the Steam
        # check) is over; the guard's clock starts then, not at the launcher.
        problem = _await_game_start(started, index, launcher_log)
        if problem:
            raise refuse(problem)
    try:
        reply = request(index, "status", timeout_s=GUARD_TIMEOUT_S)
    except SfveError:
        raise refuse(f"did not confirm its network guard within {GUARD_TIMEOUT_S:.0f} s")
    if not reply.startswith("ok "):
        raise refuse(f"answered '{reply}'")
    game_pid = _reply_pid(reply)
    # The pipe's name is all the instance number gives: whoever answers on it
    # must be the process just started or one it started, not another copy.
    game = process_identity(game_pid)
    if game is None or game not in _process_tree(_process_table(), started):
        raise refuse(f"answered from process {game_pid}, which is not the process just started ({started[0]}) or one it started")
    _remember(index, started, game)
    # The pipe answers as soon as the guard is in; the mode's own commands
    # (freeze, host, ...) exist once the battle status does.
    if mode != "inert":
        deadline = time.monotonic() + GUARD_TIMEOUT_S
        while '"battle":' not in reply and time.monotonic() < deadline:
            time.sleep(0.2)
            reply = request(index, "status")
        if '"battle":' not in reply:
            _kill_tree([started, game])
            raise SfveError(f"instance {index} did not finish starting the mod; killed")
    return Instance(index, game_pid)


def launcher_log_path(index: int) -> Path:
    return LOG_DIR / ("launcher.log" if index == 0 else f"launcher.instance{index}.log")


class _LogCursor:
    """The lines a log gains after the cursor was made. The log is set aside
    first, as launch does for the game's, so the launcher starts a new file
    and every line of it is read from the top. A log that cannot be moved
    (it is open) is read from its current end, and a size below the position
    (the launcher's rotate_on_open replaced it) starts over at the top."""

    def __init__(self, path: Path):
        self.path = path
        self.offset = 0 if set_aside(path) else self._size()

    def _size(self) -> int:
        try:
            return self.path.stat().st_size
        except OSError:
            return 0

    def new_lines(self) -> list[str]:
        size = self._size()
        if size < self.offset:
            self.offset = 0
        if size <= self.offset:
            return []
        try:
            with open(self.path, "rb") as f:
                f.seek(self.offset)
                data = f.read()
        except OSError:
            return []
        whole = data[:data.rfind(b"\n") + 1]  # a half-written last line waits
        self.offset += len(whole)
        return whole.decode(errors="replace").splitlines()


def _steam_check_failure(lines: list[str]) -> str:
    """The first line of `lines` that reports the launcher's Steam check as
    failed ("Steam check:" at spdlog's error level, src/launcher/SteamSession.cxx),
    or ""."""
    return next((line.strip() for line in lines if "Steam check:" in line and "[error]" in line), "")


def _await_game_start(launcher: Identity, index: int, log: _LogCursor) -> str:
    """Waits through the launcher's preparation until the launcher has
    started the game (a StreetFighterV.exe in its process tree). `log` is the
    launcher's log from before it started. Returns "" once the game exists,
    or why not: the launcher ended first, the log reported the Steam check as
    failed (the launcher then stays up on its recovery window, so the log is
    what ends the wait), or the preparation took more than
    LAUNCHER_PREP_TIMEOUT_S. The process table says whether the game started,
    so that does not depend on the log's wording; only the early report of a
    failure does."""
    game = GAME_EXE.name.lower()
    deadline = time.monotonic() + LAUNCHER_PREP_TIMEOUT_S
    while True:
        table = _process_table()
        tree = _process_tree(table, launcher)
        if any(table[pid].name == game for pid, _ in tree):
            return ""
        if not tree:
            return f"launcher ended before it started the game (see {launcher_log_path(index)})"
        failure = _steam_check_failure(log.new_lines())
        if failure:
            return f"launcher's Steam check failed ({failure}; log {launcher_log_path(index)})"
        if time.monotonic() > deadline:
            return f"launcher did not start the game within {LAUNCHER_PREP_TIMEOUT_S:.0f} s (see {launcher_log_path(index)})"
        time.sleep(0.5)


def _kill_tree(roots: list[Identity]) -> list[Identity]:
    """Ends each running process `roots` names, with everything it started
    (a launcher's game and helpers) except Steam and what Steam started,
    however it got there. A root whose id now belongs to another process is
    left alone. Returns what was ended."""
    table = _process_table()
    victims = {pid: created for root in roots for pid, created in _process_tree(table, root)}
    return [(pid, created) for pid, created in victims.items() if _terminate((pid, created))]


def kill_all(timeout_s: float = 15.0, all_runs: bool = False) -> list[int]:
    """Kills the copies this run started (and their launchers) and the helper
    processes it tracked (track), never Steam, and waits
    until they are gone, their pipes with them. A copy anything else started
    is left running, and so is a process that only has a recorded id's
    number now. `all_runs` also ends the copies other runs' registries
    still list, to clean up after a run that died. Returns the ids ended."""
    paths = [p for p in sorted(_registry_dir().glob("sfve-launched-*")) if p.is_dir()] if all_runs else [registry_path()]
    killed = _kill_tree([identity for path in paths for copy in _registered(path).values() for identity in copy])
    deadline = time.monotonic() + timeout_s
    while any(process_identity(pid) == (pid, created) for pid, created in killed) and time.monotonic() < deadline:
        time.sleep(0.2)
    for folder in paths:
        shutil.rmtree(folder, ignore_errors=True)
    return sorted(pid for pid, _ in killed)


# --- menu routes ----------------------------------------------------------------

# Main menu (cursor on ARCADE, as after boot) to a Player 1 vs Player 2 battle
# on the preselected stage with each side's preselected fighter. Each step is
# (pad slot, steps, seconds to wait after queueing). Needs pads 0 and 1 virtual.
VERSUS_FROM_MAIN_MENU = [
    (0, "DOWN:120 -:500 DOWN:120 -:500 A:150 -:300", 3.5),     # VERSUS
    (0, "A:150 -:300", 3.0),                                    # PLAYER 1 VS PLAYER 2
    (0, " ".join(["DOWN:120 -:400"] * 4) + " A:150 -:300", 0.5),  # P1: Proceed to Match
    (1, "DOWN:120 -:500 A:150 -:300", 6.0),                      # P2: Proceed to Match
    (0, "A:150 -:800 A:150 -:300", 8.0),                         # stage, then its setting
    (0, " ".join(["A:120 -:700"] * 9), 0.0),                     # P1 fighter, costume, color, V-Skill...
    (1, " ".join(["A:120 -:700"] * 9), 25.0),                    # P2 likewise; VS screen and load
]


_MASH_DIRECTIONS = ["-", "UP", "DOWN", "LEFT", "RIGHT", "DOWN+LEFT", "DOWN+RIGHT", "UP+LEFT", "UP+RIGHT"]
_MASH_BUTTONS = ["", "X", "Y", "RB", "A", "B", "RT", "X+A", "Y+B"]


def mash_steps(seconds: float, seed: int) -> str:
    """Seeded random pad steps (directions and attacks, never Start) lasting about `seconds`."""
    import random
    rng = random.Random(seed)
    steps, total = [], 0
    while total < seconds * 1000:
        held = rng.choice(_MASH_DIRECTIONS)
        button = rng.choice(_MASH_BUTTONS)
        if button:
            held = button if held == "-" else f"{held}+{button}"
        ms = rng.randint(30, 220)
        steps.append(f"{held}:{ms}")
        total += ms
    return " ".join(steps)


def run_route(index: int, route: list[tuple[int, str, float]]) -> None:
    """Plays a menu route on a running copy's virtual pads (menus need its window in front)."""
    focus(status_pid(index))
    for slot in sorted({slot for slot, _, _ in route}):
        request(index, f"virtual {slot} on")
    time.sleep(1.0)
    for slot, steps, wait_s in route:
        reply = request(index, f"pad {slot} {steps}")
        if not reply.startswith("ok"):
            raise SfveError(f"instance {index}: {reply}")
        time.sleep(wait_s)


MAIN_MENU_SCENE = "0x2D98660"  # the main menu state's vtable rva (status "scene")


def scene(index: int) -> str:
    """The game's current scene state kind, or "" while unknown."""
    try:
        return json.loads(request(index, "status")[3:]).get("scene", "")
    except (OSError, ValueError, SfveError):
        return ""


def boot_to_menu(index: int, timeout: float = 90.0, shot_on_timeout: Path | None = None, bring_to_front: bool = True) -> None:
    """From a fresh launch to the main menu. The mod hides the offline start-up
    dialogs and leaves the title by itself, so this only waits. On a timeout
    the copy's window is saved to `shot_on_timeout`, when given. A run whose
    copies play in the background (-sfve_background=1) may leave the window
    where it is (bring_to_front=False): Windows refuses it on a locked desktop."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if scene(index) == MAIN_MENU_SCENE:
            time.sleep(1)  # the menu's own intro
            if bring_to_front:
                focus(status_pid(index))
            return
        time.sleep(1)
    if shot_on_timeout:
        try:
            shot_on_timeout.parent.mkdir(parents=True, exist_ok=True)
            screenshot(status_pid(index), shot_on_timeout)
        except (SfveError, OSError):
            pass
    raise TimeoutError(f"instance {index} did not reach the main menu in {timeout:.0f} s")


def fight(index: int) -> None:
    """From a fresh launch into a Player 1 vs Player 2 battle (fight on screen)."""
    boot_to_menu(index)
    run_route(index, VERSUS_FROM_MAIN_MENU)
    time.sleep(8)


# --- logs ---------------------------------------------------------------------

def log_path(index: int) -> Path:
    name = "sfv_ember.log" if index == 0 else f"sfv_ember.{index}.log"
    return LOG_DIR / name


SET_ASIDE_KEPT = 5


def set_aside(path: Path) -> bool:
    """Moves a log a test run is about to start afresh to
    <name>.before-test-<time>.log beside it, so a run never loses a player's
    earlier log (the game's or the launcher's), and keeps the newest
    SET_ASIDE_KEPT of those. True when the path is free for the run: it was
    moved or never there; False when it could not be moved (it is open)."""
    if not path.exists():
        return True
    stamp = time.strftime("%Y%m%d-%H%M%S")
    target = path.with_name(f"{path.stem}.before-test-{stamp}{path.suffix}")
    try:
        path.replace(target)
    except OSError:
        return False
    earlier = sorted(path.parent.glob(f"{path.stem}.before-test-*{path.suffix}"))
    for old in earlier[:-SET_ASIDE_KEPT]:
        try:
            old.unlink()
        except OSError:
            pass
    return True


def tail(index: int, lines: int = 40) -> list[str]:
    path = log_path(index)
    if not path.exists():
        return []
    return path.read_text(errors="replace").splitlines()[-lines:]


def count_log(index: int, text: str) -> int:
    """How many of a copy's log lines hold `text`."""
    path = log_path(index)
    return sum(text in line for line in path.read_text(errors="replace").splitlines()) if path.exists() else 0


# --- window capture -------------------------------------------------------------

_user32 = ctypes.WinDLL("user32", use_last_error=True)
_gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
_EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
_user32.GetWindowDC.restype = wt.HDC
_user32.GetWindowDC.argtypes = [wt.HWND]
_user32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
_user32.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
_user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
_gdi32.CreateCompatibleDC.restype = wt.HDC
_gdi32.CreateCompatibleDC.argtypes = [wt.HDC]
_gdi32.CreateCompatibleBitmap.restype = wt.HBITMAP
_gdi32.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
_gdi32.SelectObject.restype = wt.HGDIOBJ
_gdi32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
_gdi32.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
_gdi32.DeleteObject.argtypes = [wt.HGDIOBJ]
_gdi32.DeleteDC.argtypes = [wt.HDC]


def main_window(pid: int) -> int:
    """The largest visible top-level window of a process."""
    best, best_area = 0, 0

    def visit(hwnd, _):
        nonlocal best, best_area
        owner = wt.DWORD()
        _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and _user32.IsWindowVisible(hwnd):
            r = wt.RECT()
            _user32.GetWindowRect(hwnd, ctypes.byref(r))
            area = (r.right - r.left) * (r.bottom - r.top)
            if area > best_area:
                best, best_area = hwnd, area
        return True

    _user32.EnumWindows(_EnumWindowsProc(visit), 0)
    if not best:
        raise SfveError(f"process {pid} has no visible window")
    return best


def focus(pid: int) -> None:
    """Brings a copy's window to the front (SFV menus only take input there).

    Windows lets a process take the foreground right after keyboard input, so
    a bare Alt press and release comes first.
    """
    hwnd = main_window(pid)
    VK_MENU, KEYEVENTF_KEYUP, SW_RESTORE = 0x12, 0x0002, 9
    _user32.keybd_event(VK_MENU, 0, 0, 0)
    _user32.keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0)
    _user32.ShowWindow(hwnd, SW_RESTORE)
    if not _user32.SetForegroundWindow(hwnd):
        raise SfveError(f"could not bring process {pid}'s window to the front")


def close_window(pid: int) -> None:
    """Asks a copy's window to close as its title bar's close button does
    (WM_CLOSE, which Process.CloseMainWindow sends)."""
    WM_CLOSE = 0x0010
    if not _user32.PostMessageW(main_window(pid), WM_CLOSE, 0, 0):
        raise SfveError(f"could not post WM_CLOSE to process {pid}'s window")


def _png(width: int, height: int, rgb_rows: list[bytes]) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\x00" + row for row in rgb_rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def _dpi_aware() -> None:
    """Window sizes in the game's own pixels: a process the system scales
    reads a scaled client size (768x432 for a 960x540 window at 125%) while
    PrintWindow draws the window's real pixels, so a capture kept only its
    top-left part."""
    try:
        _user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # PER_MONITOR_AWARE_V2
    except (AttributeError, OSError):
        pass  # before Windows 10 1703: the scaled size, as before


def screenshot(pid: int, out: Path) -> Path:
    """Captures a game window (even when covered) to a PNG."""
    _dpi_aware()
    hwnd = main_window(pid)
    r = wt.RECT()
    _user32.GetClientRect(hwnd, ctypes.byref(r))
    width, height = r.right, r.bottom
    window_dc = _user32.GetWindowDC(hwnd)
    mem_dc = _gdi32.CreateCompatibleDC(window_dc)
    bitmap = _gdi32.CreateCompatibleBitmap(window_dc, width, height)
    old = _gdi32.SelectObject(mem_dc, bitmap)
    try:
        PW_CLIENTONLY, PW_RENDERFULLCONTENT = 1, 2
        if not _user32.PrintWindow(hwnd, mem_dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT):
            raise SfveError("PrintWindow failed")
        header = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 24, 0, 0, 0, 0, 0, 0)
        stride = (width * 3 + 3) & ~3
        buffer = ctypes.create_string_buffer(stride * height)
        _gdi32.GetDIBits(mem_dc, bitmap, 0, height, buffer, header, 0)
        rows = []
        for y in range(height):
            bgr = buffer.raw[y * stride: y * stride + width * 3]
            rgb = bytearray(bgr)
            rgb[0::3], rgb[2::3] = bgr[2::3], bgr[0::3]
            rows.append(bytes(rgb))
    finally:
        _gdi32.SelectObject(mem_dc, old)
        _gdi32.DeleteObject(bitmap)
        _gdi32.DeleteDC(mem_dc)
        _user32.ReleaseDC(hwnd, window_dc)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(_png(width, height, rows))
    return out
