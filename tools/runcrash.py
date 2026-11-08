"""One copy crashes on purpose; what the crash leaves is checked.

  python tools/runcrash.py [--where game|thread] [--keep]

EMBER_LIBRARY.md section 29. Copy 0 starts with -sfve_crash_command=1 (as
SFVE_VIA says: through Launcher.exe, as players start it, or under the
proxy), reaches the main menu and is asked on the pipe to fault ("crash
game": on the game thread, inside SFV's own frame; "crash thread": on a
thread of the mod's). SFV's crash reporter handles it and calls the
MiniDumpWriteDump the mod hooks (mod/CrashRecord). Then:
- the crash record (Product().crashLog in the logs folder) must name an
  access violation, a write of 0x10;
- a new sfv_ember-crash-*.dmp must be in the logs folder: written by the
  launcher (the mod's log says so) or, under the proxy, by the game;
- through the launcher, the mod ends the game after the game's own dump, and
  the launcher's log must say it wrote the dump and give the game's exit
  code, which it must take for a crash (IsCrashExit). Under the proxy the
  game's reporter goes on as it would, and the copy is killed.
The launcher then shows its crash window; this script never touches it and
kills it. Refuses to run while %APPDATA%\\SFVEmber\\settings.json has Send
problem reports on, which would send the test's report without asking.
Deletes the dumps it caused (Ember's and the game's own Dump*.dmp under
%LOCALAPPDATA%\\StreetFighterV\\Saved\\Logs) unless --keep.

Prints what it saw, and PASS or FAIL.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402

CRASH_LOG = "sfv_ember-crash.log"
DUMP_PREFIX = "sfv_ember-crash-"
GAME_DUMPS = Path(os.environ.get("LOCALAPPDATA", "")) / "StreetFighterV" / "Saved" / "Logs"
EXIT_LIMIT_S = 60.0
SYNCHRONIZE = 0x00100000
QUERY_LIMITED = 0x1000


def reports_on() -> bool:
    try:
        settings = json.loads((lib.LOG_DIR.parent / "settings.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    found = [False]

    def walk(value) -> None:
        if isinstance(value, dict):
            for key, inner in value.items():
                if key == "sendProblemReportsWithDump" and inner is True:
                    found[0] = True
                walk(inner)
        elif isinstance(value, list):
            for inner in value:
                walk(inner)

    walk(settings)
    return found[0]


def files(folder: Path, pattern: str) -> set[Path]:
    return set(folder.glob(pattern)) if folder.exists() else set()


def wait_exit(pid: int) -> int | None:
    """The game's exit code once it is gone, or None if it is still running."""
    kernel = ctypes.windll.kernel32
    handle = kernel.OpenProcess(SYNCHRONIZE | QUERY_LIMITED, False, pid)
    if not handle:
        return None
    try:
        if kernel.WaitForSingleObject(handle, int(EXIT_LIMIT_S * 1000)) != 0:
            return None
        code = ctypes.c_ulong()
        kernel.GetExitCodeProcess(handle, ctypes.byref(code))
        return code.value
    finally:
        kernel.CloseHandle(handle)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="runcrash")
    p.add_argument("--where", choices=("game", "thread"), default="game")
    p.add_argument("--keep", action="store_true")
    a = p.parse_args(argv)
    if reports_on():
        print("FAIL: Send problem reports is on in settings.json; a test crash would be sent. Turn it off first.")
        return 1
    via_launcher = lib.VIA == "launcher"
    launcher_log = lib.LOG_DIR / "launcher.log"
    (lib.LOG_DIR / CRASH_LOG).unlink(missing_ok=True)
    ours_before = files(lib.LOG_DIR, f"{DUMP_PREFIX}*.dmp")
    game_before = files(GAME_DUMPS, "Dump*.dmp")
    failures: list[str] = []
    try:
        lib.kill_all()
        # As every run starts its copies: a virtual pad on slot 0 and background
        # play (an unfocused SFV draws nothing and never leaves its title).
        inst = lib.launch(0, "netplay", ["-sfve_pads=0", "-sfve_background=1", "-sfve_crash_command=1"])
        lib.boot_to_menu(0, shot_on_timeout=Path(r"D:\sfv-re\shots\runcrash-boot.png"), bring_to_front=False)
        status = json.loads(lib.request(0, "status")[3:])
        print(f"crash record before: {json.dumps(status.get('crashRecord'))}")
        record_state = status.get("crashRecord") or {}
        if not record_state.get("dumpWriterHooked"):
            failures.append("the game's dump writer is not hooked")
        if via_launcher and not record_state.get("launcherChannel"):
            failures.append("the launcher's dump channel did not reach the mod")
        print(f"asked: {lib.request(0, 'crash ' + a.where)}")
        if via_launcher:
            # The mod ends the game once its dump is written.
            code = wait_exit(inst.pid)
            if code is None:
                failures.append(f"the game was still running {EXIT_LIMIT_S:.0f} s after the fault")
            else:
                print(f"game exit code: 0x{code:08X}")
            time.sleep(2)  # the launcher logs the exit and prunes after the game is gone
        else:
            # Without a launcher the game's reporter goes on as it would
            # (its error box); the record and the dump are what count.
            deadline = time.monotonic() + EXIT_LIMIT_S
            while time.monotonic() < deadline and lib.count_log(0, "crash: wrote the dump in the game") == 0:
                time.sleep(1)
            time.sleep(2)  # the game's own dump, after ours
    except (lib.SfveError, TimeoutError) as error:
        failures.append(str(error))

    record_path = lib.LOG_DIR / CRASH_LOG
    record = record_path.read_text(errors="replace") if record_path.exists() else ""
    print(f"record: {record.strip() or '(none)'}")
    if not record.startswith("kind=unhandled_exception code=0xC0000005"):
        failures.append("the record does not name the access violation")
    elif "write of 0x0000000000000010" not in record:
        failures.append("the record does not name the faulting write")
    mod_log = lib.log_path(0).read_text(errors="replace") if lib.log_path(0).exists() else ""
    for line in mod_log.splitlines():
        if "crash" in line and ("crash:" in line or "crash record" in line):
            print(f"mod: {line}")
    expected = "crash: the launcher wrote the dump" if via_launcher else "crash: wrote the dump in the game (no launcher)"
    if expected not in mod_log:
        failures.append(f"the mod's log lacks '{expected}'")
    new_ours = sorted(files(lib.LOG_DIR, f"{DUMP_PREFIX}*.dmp") - ours_before)
    new_game = sorted(files(GAME_DUMPS, "Dump*.dmp") - game_before)
    for dump in new_ours:
        print(f"Ember dump: {dump.name} ({dump.stat().st_size // 1024} KB)")
    for dump in new_game:
        print(f"game's own dump: {dump.name} ({dump.stat().st_size // 1024} KB)")
    if not [d for d in new_ours if not d.name.endswith("-report.dmp")]:
        failures.append("no new sfv_ember-crash-*.dmp in the logs folder")
    if via_launcher:
        # Each launcher start rotates the log and opens a fresh launcher.log,
        # so the whole file is this run's launcher.
        text = launcher_log.read_bytes().decode(errors="replace") if launcher_log.exists() else ""
        for line in text.splitlines():
            if re.search(r"crash dump|exited with code|dump channel", line):
                print(f"launcher: {line}")
        if "Wrote the game's crash dump" not in text:
            failures.append("the launcher's log does not say it wrote the crash dump")
        exited = re.search(r"Game exited with code (0x[0-9a-fA-F]+)", text)
        if not exited:
            failures.append("the launcher's log does not give the game's exit code")
        elif not lib_is_crash_exit(int(exited.group(1), 16)):
            failures.append(f"the launcher does not take exit code {exited.group(1)} for a crash")
    lib.kill_all()
    if not a.keep:
        for dump in new_ours + new_game:
            dump.unlink(missing_ok=True)
    print(f"FAIL: {'; '.join(failures)}" if failures else f"PASS: crash on the {a.where} recorded and dumped")
    return 1 if failures else 0


def lib_is_crash_exit(code: int) -> bool:
    """common/CrashReport.hxx's IsCrashExit, as the launcher judges an exit."""
    if code in (0xC0000135, 0xC0000139, 0xC0E90002):
        return False
    return (code & 0xF0000000) == 0xC0000000 or code in (0xE06D7363, 3, 255)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
