"""SFV Ember test driver (command line).

  python tools/sfve.py deploy [--adopt] | undeploy
  python tools/sfve.py launch <index> [--mode probe|synctest|netplay] [-- extra game args]
  python tools/sfve.py ctl <index> <command line>        e.g. ctl 0 "pad 0 A:100 -:300"
  python tools/sfve.py shot <index> <out.png>
  python tools/sfve.py log <index> [lines]
  python tools/sfve.py versus <index>                    main menu to a P1 vs P2 battle
  python tools/sfve.py fight <index>                     fresh launch to a P1 vs P2 battle
  python tools/sfve.py focus <index>                     bring a copy's window to the front
  python tools/sfve.py mash <index> <slot> <seconds> [seed]  random pad input on a virtual slot
  python tools/sfve.py ps | kill [--all]
  python tools/sfve.py track <label> <pid>               register a helper process (and its children) for kill
  python tools/sfve.py runtime-file <name>               path of a runtime file (sf4-net.exe) for the mode, or fail

Every command runs in the run named by SFVE_RUN, or in the fixed run "cli" when
it is not set (sfvelib.use_run), however the command is started: `kill` ends
what `launch` started, even from another shell. Set SFVE_RUN to keep separate
callers' copies apart; `kill --all` also ends other runs' copies after a run
that died.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="sfve")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("deploy").add_argument("--adopt", action="store_true", help="replace a same-named file deploy did not install")
    sub.add_parser("undeploy")
    launch = sub.add_parser("launch")
    launch.add_argument("index", type=int)
    launch.add_argument("--mode", default="probe")
    ctl = sub.add_parser("ctl")
    ctl.add_argument("index", type=int)
    ctl.add_argument("line")
    shot = sub.add_parser("shot")
    shot.add_argument("index", type=int)
    shot.add_argument("out", type=Path)
    log = sub.add_parser("log")
    log.add_argument("index", type=int)
    log.add_argument("lines", type=int, nargs="?", default=40)
    versus = sub.add_parser("versus")
    versus.add_argument("index", type=int)
    fightp = sub.add_parser("fight")
    fightp.add_argument("index", type=int)
    fg = sub.add_parser("focus")
    fg.add_argument("index", type=int)
    mash = sub.add_parser("mash")
    mash.add_argument("index", type=int)
    mash.add_argument("slot", type=int)
    mash.add_argument("seconds", type=float)
    mash.add_argument("seed", type=int, nargs="?", default=1)
    sub.add_parser("ps")
    track = sub.add_parser("track")
    track.add_argument("label")
    track.add_argument("pid", type=int)
    sub.add_parser("runtime-file").add_argument("name")
    sub.add_parser("kill").add_argument("--all", action="store_true", help="also the copies other runs started (after a run that died)")
    # The game's own arguments follow "--" and are split off here: argparse
    # before Python 3.12 leaves "--" unconsumed after a nargs="*" positional.
    game_args: list[str] = []
    if "--" in argv:
        cut = argv.index("--")
        argv, game_args = argv[:cut], argv[cut + 1:]
    a = p.parse_args(argv)
    if game_args and a.cmd != "launch":
        p.error('only "launch" takes game arguments after "--"')

    try:
        if a.cmd in ("launch", "track", "kill"):  # the commands that start or end processes
            lib.use_run("cli")
        if a.cmd == "deploy":
            print("copied:", lib.deploy(a.adopt) or "nothing (already current)")
        elif a.cmd == "undeploy":
            removed = lib.undeploy()
            print("removed:", removed or "nothing (only what deploy recorded in the manifest is removed)")
        elif a.cmd == "launch":
            inst = lib.launch(a.index, a.mode, game_args)
            print(f"instance {inst.index} pid {inst.pid}: guard confirmed")
        elif a.cmd == "ctl":
            print(lib.request(a.index, a.line))
        elif a.cmd == "shot":
            print(lib.screenshot(lib.status_pid(a.index), a.out))
        elif a.cmd == "log":
            print("\n".join(lib.tail(a.index, a.lines)))
        elif a.cmd == "versus":
            lib.run_route(a.index, lib.VERSUS_FROM_MAIN_MENU)
            print(f"instance {a.index}: route to versus played")
        elif a.cmd == "fight":
            lib.fight(a.index)
            print(f"instance {a.index}: in a battle")
        elif a.cmd == "focus":
            lib.focus(lib.status_pid(a.index))
            print(f"instance {a.index} in front")
        elif a.cmd == "mash":
            print(lib.request(a.index, f"pad {a.slot} {lib.mash_steps(a.seconds, a.seed)}"))
        elif a.cmd == "ps":
            print(lib.running_pids())
        elif a.cmd == "track":
            print("tracking:", lib.track(a.label, a.pid))
        elif a.cmd == "runtime-file":
            print(lib.runtime_file(a.name))
        elif a.cmd == "kill":
            print("killed:", lib.kill_all(all_runs=a.all))
    except lib.SfveError as e:
        print("error:", e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
