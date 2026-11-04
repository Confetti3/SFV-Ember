"""Diffs two state dumps (NetplayMode "dump <frame>") from two copies.

  python tools/diffstate.py <a.bin> <b.bin>

Regions are listed in a different order in each process, so they are paired
by size, then by fewest differing bytes. Words that look like addresses (the
mod's own test) are ignored, as the state hash ignores them.
"""

from __future__ import annotations

import struct
import sys


def read(path: str) -> list[tuple[int, int, bytes]]:
    data = open(path, "rb").read()
    out, at = [], 0
    while at < len(data):
        size, address = struct.unpack_from("<QQ", data, at)
        at += 16
        out.append((size, address, data[at:at + size]))
        at += size
    return out


def looks_like_address(v: int) -> bool:
    return (1 << 40) <= v <= 0x00007FFFFFFFFFFF and v & 3 == 0


def masked(address: int, b: bytes) -> bytearray:
    """The state hash's masking: 8 bytes at each 4-byte-aligned address word."""
    m = bytearray(b)
    off = (4 - (address & 3)) & 3
    while off + 8 <= len(b):
        if looks_like_address(int.from_bytes(b[off:off + 8], "little")):
            m[off:off + 8] = bytes(8)
        off += 4
    return m


def main(pa: str, pb: str) -> None:
    ra = [(s, a, masked(a, b)) for s, a, b in read(pa)]
    rb = [(s, a, masked(a, b)) for s, a, b in read(pb)]
    unused = list(range(len(rb)))
    differing = 0
    for size, addr_a, ba in ra:
        candidates = [j for j in unused if rb[j][0] == size]
        if not candidates:
            print(f"{size} bytes: no partner")
            continue
        best = min(candidates, key=lambda j: sum(x != y for x, y in zip(ba, rb[j][2])))
        unused.remove(best)
        bb = rb[best][2]
        diffs = [i for i in range(size) if ba[i] != bb[i]]
        if not diffs:
            continue
        differing += 1
        print(f"{size} bytes (a {addr_a:#x}, b {rb[best][1]:#x}): {len(diffs)} byte(s) differ")
        shown = set()
        for i in diffs:
            w = i - i % 8
            if w in shown or len(shown) >= 4:
                continue
            shown.add(w)
            print(f"    +{w:#06x}  a {bytes(ba[w:w + 8])[::-1].hex()}  b {bytes(bb[w:w + 8])[::-1].hex()}")
    print(f"{differing} region(s) differ")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
