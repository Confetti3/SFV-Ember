"""sfvelib: deploying the mod, and starting a copy (through the proxy or the launcher) until its guard answers."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from sfvelib_config import (ASSETS, BUILD_DIR, GAME_DIR, GAME_EXE, GUARD_TIMEOUT_S, LAUNCHER_FILES, LAUNCHER_PREP_TIMEOUT_S,
                            LOG_DIR, MANIFEST, MOD_FILES, PACKAGE_DIR, STEAM_APP_ID, VIA, SfveError)
from sfvelib_logs import log_path, set_aside
from sfvelib_pipe import _reply_pid, request
from sfvelib_procs import (Identity, Instance, _kill_tree, _process_table, _process_tree, _remember, process_identity, registry_path,
                           running_pids)


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
    refuse_player_build(BUILD_DIR)
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


def _game_options(index: int, mode: str, extra: list[str], windowed: bool) -> list[str]:
    options = [f"-sfve_instance={index}", f"-sfve_mode={mode}"]
    if windowed:
        # SFVE_RES ("1920x1080") sets the window's size; 960x540 by default.
        width, height = os.environ.get("SFVE_RES", "960x540").lower().split("x")
        options += ["-windowed", f"-ResX={int(width)}", f"-ResY={int(height)}"]
        # SFVE_SCREEN=borderless: the mod also takes the frame off the copy's
        # window and lays it over its monitor (docs/design/beta9/screen.md;
        # one copy at a time, two would cover each other). The player's own
        # Screen mode never reaches a copy started here: these window options
        # are the test's, and the launcher then adds nothing.
        if os.environ.get("SFVE_SCREEN", "").lower() == "borderless":
            options.append("-sfve_screen=borderless")
    return options + extra


def launch(index: int, mode: str = "probe", extra: list[str] | None = None, windowed: bool = True) -> Instance:
    """Starts one copy and waits until its guard is confirmed (or kills it).
    Through the launcher that is two waits: the launcher's preparation until
    it has started the game (LAUNCHER_PREP_TIMEOUT_S), then the game's start-up
    until the guard answers (GUARD_TIMEOUT_S)."""
    registry_path()  # the run is mandatory: refuse before anything starts
    options = _game_options(index, mode, extra or [], windowed)
    # Each run starts its own log; the last one is kept.
    if not set_aside(log_path(index)):
        raise SfveError(f"{log_path(index)} is held open by another program: close it first")
    if VIA == "launcher":
        missing = [name for name in LAUNCHER_FILES if not (PACKAGE_DIR / name).exists()]
        if missing:
            raise SfveError(f"{PACKAGE_DIR} has no {', '.join(missing)}: build or stage SFV Ember first")
        refuse_player_build(PACKAGE_DIR)
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
        reply = request(index, "status", timeout_s=GUARD_TIMEOUT_S, idempotent=True)
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
            reply = request(index, "status", idempotent=True)
        if '"battle":' not in reply:
            _kill_tree([started, game])
            raise SfveError(f"instance {index} did not finish starting the mod; killed")
    return Instance(index, game_pid)


def build_kind(folder: Path) -> str:
    """The kind of build a build tree or package holds, by its receipt
    (scripts/build.ps1, build-provenance.json): "player", "development", or ""
    for none."""
    try:
        return json.loads((folder / "build-provenance.json").read_text(encoding="utf-8-sig")).get("buildKind", "")
    except (OSError, ValueError):
        return ""


def refuse_player_build(folder: Path) -> None:
    """The tools drive a development build only: a player build takes no test
    options and its pipe answers ping and status alone."""
    if build_kind(folder) == "player":
        raise SfveError(f"{folder} is a player build, which takes no test options: "
                        "build a development one (scripts/build.ps1 without -Player)")


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


def _await_game_start(launcher: Identity, index: int, log: _LogCursor) -> str:
    """Waits through the launcher's preparation until the launcher has
    started the game (a StreetFighterV.exe in its process tree). `log` is the
    launcher's log from before it started. Returns "" once the game exists,
    or why not: the launcher ended first, or the preparation took more than
    LAUNCHER_PREP_TIMEOUT_S. The process table says whether the game started,
    so that does not depend on the log's wording."""
    game = GAME_EXE.name.lower()
    deadline = time.monotonic() + LAUNCHER_PREP_TIMEOUT_S
    while True:
        table = _process_table()
        tree = _process_tree(table, launcher)
        if any(table[pid].name == game for pid, _ in tree):
            return ""
        if not tree:
            return f"launcher ended before it started the game (see {launcher_log_path(index)})"
        if time.monotonic() > deadline:
            return f"launcher did not start the game within {LAUNCHER_PREP_TIMEOUT_S:.0f} s (see {launcher_log_path(index)})"
        time.sleep(0.5)
