"""Diffs two copies' states: two dumps (NetplayMode "dump <frame>") or two
desync state files (%APPDATA%/SFVEmber/desync-states/*.state, one from each
player's PC after a desync, EMBER_LIBRARY.md section 55).

  python tools/diffstate.py <a> <b>

A state file names its regions, so two of them are paired by name. A dump
does not, and regions are listed in a different order in each process, so
they are paired by size, then by fewest differing bytes. Words that look like
addresses (the mod's own test) are ignored, as the state hash ignores them.
"""

from __future__ import annotations

import struct
import sys


def read(path: str) -> list[tuple[int, int, bytes, str]]:
    data = open(path, "rb").read()
    out, at = [], 0
    if data[:8] == b"SFVSTATE":
        version, count = struct.unpack_from("<II", data, 8)
        if version != 1:
            sys.exit(f"{path}: state file version {version} is not known")
        at = 16
        for _ in range(count):
            (length,) = struct.unpack_from("<H", data, at)
            name = data[at + 2:at + 2 + length].decode("utf-8", "replace")
            at += 2 + length
            size, address = struct.unpack_from("<QQ", data, at)
            at += 16
            out.append((size, address, data[at:at + size], name))
            at += size
        return out
    while at < len(data):
        size, address = struct.unpack_from("<QQ", data, at)
        at += 16
        out.append((size, address, data[at:at + size], ""))
        at += size
    return out


def looks_like_address(v: int) -> bool:
    return (1 << 40) <= v <= 0x00007FFFFFFFFFFF and v & 3 == 0


def masked(address: int, b: bytes) -> bytearray:
    """The state hash's masking: 8 bytes at each 4-byte-aligned address word
    (not tested 4 bytes into one it found)."""
    m = bytearray(b)
    off = (4 - (address & 3)) & 3
    while off + 8 <= len(b):
        if looks_like_address(int.from_bytes(b[off:off + 8], "little")):
            m[off:off + 8] = bytes(8)
            off += 4
        off += 4
    return m


def show(label: str, ba: bytes, bb: bytes, words: int) -> bool:
    diffs = [i for i in range(min(len(ba), len(bb))) if ba[i] != bb[i]]
    if not diffs and len(ba) == len(bb):
        return False
    print(f"{label}: {len(diffs)} byte(s) differ")
    shown: list[int] = []
    for i in diffs:
        w = i - i % 8
        if w in shown:
            continue
        if len(shown) >= words:
            print(f"    ... and more from +{w:#06x}")
            break
        shown.append(w)
        print(f"    +{w:#06x}  a {bytes(ba[w:w + 8])[::-1].hex()}  b {bytes(bb[w:w + 8])[::-1].hex()}")
    return True


def main(pa: str, pb: str) -> None:
    ra = [(s, a, masked(a, b), n) for s, a, b, n in read(pa)]
    rb = [(s, a, masked(a, b), n) for s, a, b, n in read(pb)]
    differing = 0
    if all(r[3] for r in ra) and all(r[3] for r in rb):
        # Two state files: the same names, compared as such.
        by_name = {r[3]: r for r in rb}
        for size, addr_a, ba, name in ra:
            other = by_name.pop(name, None)
            if other is None:
                print(f"{name} ({size} bytes): only in a")
                continue
            differing += show(f"{name} ({size} bytes)", ba, other[2], 16)
        for name in by_name:
            print(f"{name}: only in b")
        print(f"{differing} region(s) differ")
        return
    unused = list(range(len(rb)))
    for size, addr_a, ba, _ in ra:
        candidates = [j for j in unused if rb[j][0] == size]
        if not candidates:
            print(f"{size} bytes: no partner")
            continue
        best = min(candidates, key=lambda j: sum(x != y for x, y in zip(ba, rb[j][2])))
        unused.remove(best)
        differing += show(f"{size} bytes (a {addr_a:#x}, b {rb[best][1]:#x})", ba, rb[best][2], 4)
    print(f"{differing} region(s) differ")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
