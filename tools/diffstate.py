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


# How near one of a file's own regions an address must be for masked() to
# count it as that process's memory (its heap), standing in for the mod's
# committed-memory test, which a file cannot answer for another process.
NEAR = 64 << 30


def mapped_test(regions) -> "callable":
    """The tie-break masked() uses for one file: near one of its regions, or
    inside the game's image (found from the global random generator)."""
    addresses = sorted(a for _, a, _, _ in regions)
    image = next((a - 0x3966538 - 8 for _, a, _, n in regions if n == "global.rng"), None)

    def mapped(v: int) -> bool:
        if image is not None and image <= v < image + 0x6000000:
            return True
        return any(abs(v - a) < NEAR for a in addresses)

    return mapped


def masked(address: int, b: bytes, mapped=None) -> bytearray:
    """The state hash's masking (StateLayout::ZeroAddresses and AddressAt): 8
    bytes at each 4-byte-aligned address word, not tested 4 bytes into one it
    found; where a window 4 bytes off an 8-byte boundary and the aligned one
    after it both look like addresses, the one `mapped` says is memory wins
    (else the first)."""
    m = bytearray(b)
    off = (4 - (address & 3)) & 3
    while off + 8 <= len(b):
        value = int.from_bytes(b[off:off + 8], "little")
        address_here = looks_like_address(value)
        if address_here and mapped and (address + off) & 7 == 4 and off + 12 <= len(b):
            aligned = int.from_bytes(b[off + 4:off + 12], "little")
            if looks_like_address(aligned) and mapped(aligned) and not mapped(value):
                address_here = False
        if address_here:
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
    raw_a, raw_b = read(pa), read(pb)
    mapped_a, mapped_b = mapped_test(raw_a), mapped_test(raw_b)
    ra = [(s, a, masked(a, b, mapped_a), n) for s, a, b, n in raw_a]
    rb = [(s, a, masked(a, b, mapped_b), n) for s, a, b, n in raw_b]
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
