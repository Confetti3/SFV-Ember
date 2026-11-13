"""Diffs two copies' states: two dumps (NetplayMode "dump <frame>") or two
desync state files (%APPDATA%/SFVEmber/desync-states/*.state, one from each
player's PC after a desync, EMBER_LIBRARY.md section 55).

  python tools/diffstate.py <a> <b>
  python tools/diffstate.py --selftest

A state file names its regions, so two of them are paired by name. A dump
does not, and regions are listed in a different order in each process, so
they are paired by size, then by fewest differing bytes. Words that look like
addresses (the mod's own test) are ignored, as the state hash ignores them:
only words on an 8-byte boundary of their real address (masked).
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
    """The state hash's masking (StateLayout::ZeroAddresses): the 8 bytes of
    each word on an 8-byte boundary of the region's real address (`address`,
    not the offset in the file) that looks like an address, and nothing else.
    Windows never overlap, so values alone decide and equal values mask
    alike in every process (docs/design/beta8/rollback-fixes.md)."""
    m = bytearray(b)
    off = (8 - (address & 7)) & 7
    while off + 8 <= len(b):
        if looks_like_address(int.from_bytes(b[off:off + 8], "little")):
            m[off:off + 8] = bytes(8)
        off += 8
    return m


def selftest() -> int:
    """The rule's cases, as tests/StateRegionsTests.cxx has them."""
    def word(v: int) -> bytes:
        return v.to_bytes(8, "little")

    def dword(v: int) -> bytes:
        return v.to_bytes(4, "little")
    failures = []

    def check(what: str, ok: bool) -> None:
        if not ok:
            failures.append(what)
    # An aligned pointer is zeroed, whatever precedes it: the S6b tail, a
    # pointer just past a 4 GB boundary after a dword >= 0x10000.
    for pointer in (0x0000013D000042D0, 0x00000217806F42D0):
        b = bytes(8) + dword(0) + dword(0x00C70000) + word(pointer) + bytes(8)
        check(f"S6b tail {pointer:#x}", masked(0x1000, b) == bytearray(b[:16]) + bytes(16))
    # An ordinary pair of dwords 4 bytes off an 8-byte boundary (280 and 293,
    # which read together as 0x0000012500000118) is hashed as it is.
    b = dword(1) + dword(0x118) + dword(0x125) + dword(1)
    check("value pair at 4 mod 8", masked(0x1000, b) == bytearray(b))
    # The same bytes in a region that starts 4 bytes past a boundary: the
    # pair is now on a real 8-byte boundary, and looks like an address.
    check("value pair at 0 mod 8", masked(0x1004, b) == bytearray(dword(1) + bytes(8) + dword(1)))
    # A word cut by the region's end is left alone.
    b = word(0x00000204F5F982A0)[:7]
    check("cut word", masked(0x1000, b) == bytearray(b))
    for f in failures:
        print(f"FAIL {f}")
    print("selftest:", "FAIL" if failures else "PASS")
    return 1 if failures else 0


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
    ra = [(s, a, masked(a, b), n) for s, a, b, n in raw_a]
    rb = [(s, a, masked(a, b), n) for s, a, b, n in raw_b]
    differing = 0
    if all(r[3] for r in ra) and all(r[3] for r in rb):
        # Two state files: the same names, compared as such; but an unowned
        # run's name ("game.N") is its place in that process's region list,
        # which differs between processes, so those are paired by size and
        # fewest differing bytes, as a dump's regions are.
        def unowned(name: str) -> bool:
            return name.startswith("game.")
        by_name = {r[3]: r for r in rb if not unowned(r[3])}
        loose = [r for r in rb if unowned(r[3])]
        for size, addr_a, ba, name in ra:
            if unowned(name):
                candidates = [r for r in loose if r[0] == size]
                if not candidates:
                    print(f"{name} ({size} bytes): no partner in b")
                    continue
                other = min(candidates, key=lambda r: sum(x != y for x, y in zip(ba, r[2])))
                loose.remove(other)
                differing += show(f"{name} ({size} bytes; b's {other[3]})", ba, other[2], 16)
                continue
            other = by_name.pop(name, None)
            if other is None:
                print(f"{name} ({size} bytes): only in a")
                continue
            differing += show(f"{name} ({size} bytes)", ba, other[2], 16)
        for name in list(by_name) + [r[3] for r in loose]:
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
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    main(sys.argv[1], sys.argv[2])
