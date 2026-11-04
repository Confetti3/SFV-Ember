"""Compares two netplay copies' state at the same confirmed frames.

  python tools/compare.py [a] [b]

* the last 60 confirmed frames' state hashes, side by side;
* the latest common checkpoint's regions, as multisets of size:hash (each
  process lists the game's regions in its own order), unmatched ones grouped
  by size;
* the first checkpoint whose hash differs, from both mod logs (every
  checkpoint is logged; a replay logs it again, the last line wins).

Exit code 1 when the copies differ.
"""

from __future__ import annotations

import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402

_CHECKPOINT = re.compile(r"checkpoint: frame (\d+) hash ([0-9a-f]+)")


def netplay_status(index: int) -> dict:
    return json.loads(lib.request(index, "status")[3:])["battle"]["mode"]


def logged_checkpoints(index: int) -> dict[int, str]:
    found: dict[int, str] = {}
    for line in lib.tail(index, 200000):
        m = _CHECKPOINT.search(line)
        if m:
            found[int(m.group(1))] = m.group(2)
    return found


def main(a: int = 0, b: int = 1) -> int:
    sa, sb = netplay_status(a), netplay_status(b)
    for index, s in ((a, sa), (b, sb)):
        print(f"copy {index}: frame {s['frame']} confirmed {s['confirmed']} rollbacks {s['rollbacks']} "
              f"longest {s['longestRollback']} stalls {s['stalls']} pacing {s['pacing']}")
    confirmed = min(sa["confirmed"], sb["confirmed"])
    if confirmed < 1:
        print("nothing confirmed yet")
        return 0

    first = max(1, confirmed - 60)
    compared = differ = 0
    for frame in range(first, confirmed + 1):
        ha, hb = lib.request(a, f"hash {frame}"), lib.request(b, f"hash {frame}")
        if ha.startswith("ok") and hb.startswith("ok"):
            compared += 1
            differ += ha != hb
    print(f"frames {first}..{confirmed}: {compared} compared, {differ} differ")

    ca = lib.request(a, f"checkpoint {confirmed}").split()
    cb = lib.request(b, f"checkpoint {confirmed}").split()
    if ca[0] == "ok" and cb[0] == "ok" and ca[1] == cb[1]:
        only_a, only_b = Counter(ca[2:]) - Counter(cb[2:]), Counter(cb[2:]) - Counter(ca[2:])
        print(f"checkpoint {ca[1]}: {sum(only_a.values())} region(s) unmatched")
        sizes = Counter(item.split(":")[0] for item in only_a.elements())
        sizes_b = Counter(item.split(":")[0] for item in only_b.elements())
        for size in sorted(set(sizes) | set(sizes_b), key=int):
            print(f"  {size:>6} bytes: {sizes[size]} on copy {a}, {sizes_b[size]} on copy {b}")

    la, lb = logged_checkpoints(a), logged_checkpoints(b)
    common = sorted(set(la) & set(lb))
    divergent = [f for f in common if la[f] != lb[f]]
    if divergent:
        print(f"first divergent checkpoint: frame {divergent[0]} "
              f"({len(divergent)} of {len(common)} logged checkpoints differ)")
    elif common:
        print(f"all {len(common)} logged checkpoints match (frames {common[0]}..{common[-1]})")
    return 1 if differ or divergent else 0


if __name__ == "__main__":
    sys.exit(main(*(int(x) for x in sys.argv[1:3])))
