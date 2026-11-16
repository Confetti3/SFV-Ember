"""sfvelib: processes and the run's registry: who a run started (by id and creation time), and ending them."""

from __future__ import annotations

import contextlib
import ctypes
import ctypes.wintypes as wt
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import NamedTuple

from sfvelib_config import BUILD_DIR, SfveError


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


def _kill_tree(roots: list[Identity]) -> list[Identity]:
    """Ends each running process `roots` names, with everything it started
    (a launcher's game and helpers) except Steam and what Steam started,
    however it got there. A root whose id now belongs to another process is
    left alone. Returns what was ended."""
    table = _process_table()
    victims = {pid: created for root in roots for pid, created in _process_tree(table, root)}
    return [(pid, created) for pid, created in victims.items() if _terminate((pid, created))]


def _registries(all_runs: bool) -> list[Path]:
    return [p for p in sorted(_registry_dir().glob("sfve-launched-*")) if p.is_dir()] if all_runs else [registry_path()]


def surviving(all_runs: bool = False) -> list[Identity]:
    """The processes this run's registry (or, with `all_runs`, every run's) still lists that
    are running now: what kill_all has not ended, or ended and that came back."""
    return [identity for path in _registries(all_runs) for copy in _registered(path).values() for identity in copy
            if process_identity(identity[0]) == identity]


def kill_all(timeout_s: float = 15.0, all_runs: bool = False) -> list[int]:
    """Kills the copies this run started (and their launchers) and the helper
    processes it tracked (track), never Steam, and waits
    until they are gone, their pipes with them. A copy anything else started
    is left running, and so is a process that only has a recorded id's
    number now. `all_runs` also ends the copies other runs' registries
    still list, to clean up after a run that died. Returns the ids ended.
    A process that is still running afterwards (it did not end in time, or could not be ended)
    stays in the registry, so the next kill_all, `sfve.py kill` or `sfve.py kill --all` finds it
    again: surviving() names them, and only an ended process is forgotten."""
    paths = _registries(all_runs)
    killed = _kill_tree([identity for path in paths for copy in _registered(path).values() for identity in copy])
    deadline = time.monotonic() + timeout_s
    while any(process_identity(pid) == (pid, created) for pid, created in killed) and time.monotonic() < deadline:
        time.sleep(0.2)
    for folder in paths:
        left = False
        for entry in sorted(folder.glob("*.json")):
            alive = [identity for identity in _read_entry(entry) if process_identity(identity[0]) == identity]
            if alive:
                left = True
                entry.write_text(json.dumps(alive))
            else:
                entry.unlink(missing_ok=True)
        if not left:
            shutil.rmtree(folder, ignore_errors=True)
    return sorted(pid for pid, _ in killed)


@contextlib.contextmanager
def run_scope(keep: bool = False):
    """The one wrapper a runner puts around everything it starts: on the way out, normally or by
    an exception, kill_all() ends what this run started (by id and creation time: never a game
    this run did not launch) unless `keep`. Needs a run (begin_run() / use_run() first). The
    cleanup never hides what went wrong inside; a copy that is still running afterwards is said
    on stderr and stays in the registry for `sfve.py kill`."""
    try:
        yield
    finally:
        if not keep:
            try:
                kill_all()
            except (SfveError, OSError) as error:
                print(f"cleanup: could not end this run's copies: {error}", file=sys.stderr)
            try:
                left = surviving()
            except (SfveError, OSError):
                left = []
            if left:
                print(f"cleanup: still running after kill_all: {', '.join(str(pid) for pid, _ in left)} "
                      "(kept in the run's registry: python tools/sfve.py kill ends them)", file=sys.stderr)
