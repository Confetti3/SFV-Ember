"""SFV Ember test driver (command line).

  python tools/sfve.py deploy | undeploy
  python tools/sfve.py launch <index> [--mode probe|synctest|netplay] [-- extra game args]
  python tools/sfve.py ctl <index> <command line>        e.g. ctl 0 "pad 0 A:100 -:300"
  python tools/sfve.py shot <index> <out.png>
  python tools/sfve.py log <index> [lines]
  python tools/sfve.py versus <index>                    main menu to a P1 vs P2 battle
  python tools/sfve.py fight <index>                     fresh launch to a P1 vs P2 battle
  python tools/sfve.py focus <index>                     bring a copy's window to the front
  python tools/sfve.py mash <index> <slot> <seconds> [seed]  random pad input on a virtual slot
  python tools/sfve.py ps | kill
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
    sub.add_parser("deploy")
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
    sub.add_parser("kill")
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
        if a.cmd == "deploy":
            print("copied:", lib.deploy() or "nothing (already current)")
        elif a.cmd == "undeploy":
            print("removed:", lib.undeploy())
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
        elif a.cmd == "kill":
            print("killed:", lib.kill_all())
    except lib.SfveError as e:
        print("error:", e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
