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

State files come in two versions (src/ember/DesyncReport.hxx, StateFile):
version 1 holds the regions; version 2 adds each region's flags and a pointer
census: for every address-like word of a region the peers hash, whether it is
mapped, what it points at relative to something that is the same in both
processes (an image offset, a saved region and offset, a named object and
offset, or an offset in a heap allocation of some size holding some 32
bytes), so two copies' diverged pointers can be told apart from pointers that
only differ because the processes' addresses do. Given two version 2 files
this prints, per differing region, the value words that differ (offset and
both values) and the pointer words whose relative references differ.
"""

from __future__ import annotations

import struct
import sys
from typing import NamedTuple

CLASS_NAMES = {0: "unmapped", 1: "image", 2: "module", 3: "region", 4: "object", 5: "heap"}
WORD = struct.Struct("<IIQBBHQIIQQQ")  # one census word, 60 bytes
MAX_VALUE_LINES = 16


class Word(NamedTuple):
    region: int
    offset: int
    value: int
    cls: int
    mapped: bool
    reference: int
    ref_index: int
    alloc_base: int
    alloc_size: int
    target_hash: int


class Census(NamedTuple):
    objects: list  # (name, address, extent)
    modules: list  # names
    image_base: int
    image_size: int
    truncated: bool
    words: list  # Word


class State(NamedTuple):
    version: int  # 0 for a dump without names
    regions: list  # (size, address, bytes, name, flags)
    census: Census | None


def read_state(path: str) -> State:
    data = open(path, "rb").read()
    return parse_state(data, path)


def parse_state(data: bytes, path: str = "<memory>") -> State:
    regions: list = []
    at = 0
    if data[:8] == b"SFVSTATE":
        version, count = struct.unpack_from("<II", data, 8)
        if version not in (1, 2):
            sys.exit(f"{path}: state file version {version} is not known")
        at = 16
        for _ in range(count):
            (length,) = struct.unpack_from("<H", data, at)
            name = data[at + 2:at + 2 + length].decode("utf-8", "replace")
            at += 2 + length
            size, address = struct.unpack_from("<QQ", data, at)
            at += 16
            flags = 3  # version 1 has none: every region was hashed
            if version == 2:
                (flags,) = struct.unpack_from("<I", data, at)
                at += 4
            regions.append((size, address, data[at:at + size], name, flags))
            at += size
        if version == 1:
            return State(1, regions, None)
        (count,) = struct.unpack_from("<I", data, at)
        at += 4
        objects = []
        for _ in range(count):
            (length,) = struct.unpack_from("<H", data, at)
            name = data[at + 2:at + 2 + length].decode("utf-8", "replace")
            at += 2 + length
            address, extent = struct.unpack_from("<QQ", data, at)
            at += 16
            objects.append((name, address, extent))
        (count,) = struct.unpack_from("<I", data, at)
        at += 4
        modules = []
        for _ in range(count):
            (length,) = struct.unpack_from("<H", data, at)
            modules.append(data[at + 2:at + 2 + length].decode("utf-8", "replace"))
            at += 2 + length
        image_base, image_size, truncated, count = struct.unpack_from("<QQBI", data, at)
        at += 21
        words = []
        for _ in range(count):
            f = WORD.unpack_from(data, at)
            at += WORD.size
            words.append(Word(f[0], f[1], f[2], f[3], bool(f[4]), f[6], f[7], f[9], f[10], f[11]))
        if at != len(data):
            sys.exit(f"{path}: {len(data) - at} byte(s) after the census")
        return State(2, regions, Census(objects, modules, image_base, image_size, bool(truncated), words))
    while at < len(data):
        size, address = struct.unpack_from("<QQ", data, at)
        at += 16
        regions.append((size, address, data[at:at + size], "", 3))
        at += size
    return State(0, regions, None)


def read(path: str) -> list[tuple[int, int, bytes, str]]:
    """The regions as (size, address, bytes, name), whichever the version."""
    return [r[:4] for r in read_state(path).regions]


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


def word_spans(address: int, size: int) -> list[tuple[int, int]]:
    """The region's words as (start, end) offsets: a part word up to the first
    8-byte boundary of its real address, then 8 bytes from each boundary."""
    first = min((8 - (address & 7)) & 7, size)
    out = [(0, first)] if first else []
    out += [(off, min(off + 8, size)) for off in range(first, size, 8)]
    return out


def encode_v2(regions, objects, modules, image, words) -> bytes:
    """A version 2 state file, as src/ember/DesyncReport.cxx writes it (for the
    tests): regions (name, address, bytes, flags), objects (name, address,
    extent), module names, image (base, size) and words (region, offset, value,
    class, mapped, reference index, reference, allocation base, allocation
    size, target hash)."""
    out = b"SFVSTATE" + struct.pack("<II", 2, len(regions))
    for name, address, data, flags in regions:
        n = name.encode()
        out += struct.pack("<H", len(n)) + n + struct.pack("<QQI", len(data), address, flags) + data
    out += struct.pack("<I", len(objects))
    for name, address, extent in objects:
        n = name.encode()
        out += struct.pack("<H", len(n)) + n + struct.pack("<QQ", address, extent)
    out += struct.pack("<I", len(modules))
    for name in modules:
        n = name.encode()
        out += struct.pack("<H", len(n)) + n
    out += struct.pack("<QQBI", image[0], image[1], 0, len(words))
    for w in words:
        out += WORD.pack(w[0], w[1], w[2], w[3], w[4], 0, w[6], w[5], 0, w[7], w[8], w[9])
    return out


def describe(census: Census, regions: list, w: Word) -> tuple:
    """What a pointer word refers to, in terms that are the same in both
    processes when the copies agree: the target's image offset, its saved
    region's name and offset, its object's name and offset, or its heap
    allocation's size, the offset in it and the hash of 32 bytes at the target."""
    if not w.mapped:
        return ("unmapped",)
    if w.cls == 1:
        return ("image", w.reference)
    if w.cls == 2:
        module = census.modules[w.ref_index] if w.ref_index < len(census.modules) else "?"
        return ("module", module, w.reference)
    if w.cls == 3:
        name = regions[w.ref_index][3] if w.ref_index < len(regions) else "?"
        return ("region", name, w.reference)
    if w.cls == 4:
        name = census.objects[w.ref_index][0] if w.ref_index < len(census.objects) else "?"
        return ("object", name, w.reference)
    return ("heap", w.alloc_size, w.reference, w.target_hash)


def describe_text(ref: tuple) -> str:
    kind = ref[0]
    if kind == "unmapped":
        return "not mapped"
    if kind == "image":
        return f"image+{ref[1]:#x}"
    if kind == "module":
        return f"{ref[1]}+{ref[2]:#x}"
    if kind == "region":
        return f"region {ref[1]}+{ref[2]:#x}"
    if kind == "object":
        return f"object {ref[1]}+{ref[2]:#x}"
    return f"heap allocation of {ref[1]:#x} +{ref[2]:#x}, 32 bytes there hash {ref[3]:016x}"


def selftest() -> int:
    """The rule's cases, as tests/StateRegionsTests.cxx has them, and the
    state file's two versions with the census's comparison."""
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
    check("word spans", word_spans(0x1004, 20) == [(0, 4), (4, 12), (12, 20)] and word_spans(0x1000, 12) == [(0, 8), (8, 12)])

    # State files. Version 1: regions only.
    def v1(regions) -> bytes:
        out = b"SFVSTATE" + struct.pack("<II", 1, len(regions))
        for name, address, data in regions:
            n = name.encode()
            out += struct.pack("<H", len(n)) + n + struct.pack("<QQ", len(data), address) + data
        return out

    old = parse_state(v1([("A", 0x20000000000, bytes(16)), ("B", 0x20000001000, bytes(8))]))
    check("v1 regions", old.version == 1 and old.census is None and [r[3] for r in old.regions] == ["A", "B"] and old.regions[0][4] == 3)

    # Version 2 as src/ember/DesyncReport.cxx writes it: the two copies' states
    # of one region, "A", at other addresses (the processes differ), where
    # +0 is a pointer to the same image offset, +8 a pointer into heap
    # allocations of the same size but holding other bytes, +16 a plain value
    # that differs and +24 a pointer into region "B" at another offset.
    def copy(base: int, heap_hash: int, plain: int, b_offset: int) -> bytes:
        image = 0x7FF600000000 + (base >> 8)  # another image base in each process
        data = bytearray(32)
        data[0:8] = word(image + 0x1234)
        data[8:16] = word(base + 0x5000)
        data[16:24] = word(plain)
        data[24:32] = word(base + 0x1000 + b_offset)
        return encode_v2(
            [("A", base, bytes(data), 3), ("B", base + 0x1000, bytes(64), 3)],
            [("CharaUnit[0]", base + 0x9000, 0x800)], ["foo.dll"], (image, 0x1000000),
            [
                # region, offset, value, class, mapped, ref_index, reference, alloc base, alloc size, hash
                (0, 0, image + 0x1234, 1, 1, 0, 0x1234, 0, 0, 0),
                (0, 8, base + 0x5000, 5, 1, 0, 0x20, base + 0x4FE0, 0x1000, heap_hash),
                (0, 24, base + 0x1000 + b_offset, 3, 1, 1, b_offset, base, 0x2000, 7),
            ])

    a = parse_state(copy(0x20000000000, 0xAAAA, 5, 8))
    b_same = parse_state(copy(0x30000000000, 0xAAAA, 5, 8))
    b_diff = parse_state(copy(0x30000000000, 0xBBBB, 6, 16))
    check("v2 regions", a.version == 2 and len(a.regions) == 2 and a.regions[0][4] == 3)
    check("v2 census", a.census is not None and len(a.census.words) == 3 and a.census.modules == ["foo.dll"] and
          a.census.objects[0][0] == "CharaUnit[0]" and a.census.image_size == 0x1000000)
    check("v2 words", a.census.words[1].cls == 5 and a.census.words[1].alloc_size == 0x1000 and a.census.words[1].target_hash == 0xAAAA)
    # The same copy in another process: no value differs and every pointer refers to the same things.
    lines_same: list[str] = []
    check("same copies", compare_regions(a, b_same, 0, 0, "A", lines_same.append) == (0, 0))
    lines_diff: list[str] = []
    values, pointers = compare_regions(a, b_diff, 0, 0, "A", lines_diff.append)
    check("one value differs", values == 1)
    check("two pointers differ", pointers == 2)
    text = "\n".join(lines_diff)
    check("value line", "+0x0010" in text and "0000000000000005" in text and "0000000000000006" in text)
    check("heap pointer line", "+0x0008" in text and "hash 000000000000aaaa" in text and "hash 000000000000bbbb" in text)
    check("region pointer line", "+0x0018" in text and "region B+0x8" in text and "region B+0x10" in text)
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


def compare_regions(sa: State, sb: State, ia: int, ib: int, label: str, emit) -> tuple[int, int]:
    """Two version 2 states' regions `ia` and `ib` (their places in the files):
    the value words that differ (the hash's masked words) and the pointer
    words whose relative references differ. Lines go to `emit`; returns the
    two counts."""
    size_a, addr_a, data_a, _, _ = sa.regions[ia]
    size_b, addr_b, data_b, _, _ = sb.regions[ib]
    if (addr_a - addr_b) & 7:
        emit(f"    the region lies {addr_a & 7} and {addr_b & 7} bytes past an 8-byte boundary in the two files: the hash "
             "tests other words in each")
    ma, mb = masked(addr_a, data_a), masked(addr_b, data_b)
    values = 0
    for off, end in word_spans(addr_a, min(size_a, size_b)):
        if ma[off:end] == mb[off:end]:
            continue
        values += 1
        if values <= MAX_VALUE_LINES:
            emit(f"    value   +{off:#06x}  a {bytes(data_a[off:end])[::-1].hex():>16}  b {bytes(data_b[off:end])[::-1].hex():>16}")
    if values > MAX_VALUE_LINES:
        emit(f"    ... and {values - MAX_VALUE_LINES} more value word(s)")
    pointers = 0
    wa = {w.offset: w for w in sa.census.words if w.region == ia}
    wb = {w.offset: w for w in sb.census.words if w.region == ib}
    for off in sorted(set(wa) | set(wb)):
        pa, pb = wa.get(off), wb.get(off)
        if pa is not None and pb is not None:
            ra, rb = describe(sa.census, sa.regions, pa), describe(sb.census, sb.regions, pb)
            if ra == rb:
                continue
            pointers += 1
            emit(f"    pointer +{off:#06x}  a {describe_text(ra)}  b {describe_text(rb)}")
        else:
            # An address-like word in one copy only: the other holds an ordinary value there.
            pointers += 1
            only, w, other = ("a", pa, wb) if pa is not None else ("b", pb, wa)
            sx = sa if pa is not None else sb
            emit(f"    pointer +{off:#06x}  only in {only}: {describe_text(describe(sx.census, sx.regions, w))}")
    if values or pointers:
        emit(f"    -> {values} value word(s) and {pointers} pointer word(s) differ in {label}")
    return values, pointers


def main_v2(sa: State, sb: State) -> None:
    """Two version 2 files: regions paired by name, a run named "game.N" (its
    place in that process's list, which differs) by size and fewest differing
    bytes; each differing region's value words and pointers are listed."""
    differing = 0
    for state, label in ((sa, "a"), (sb, "b")):
        if state.census.truncated:
            print(f"note: {label}'s census lists only its first {len(state.census.words)} pointer words")
    unowned = lambda name: name.startswith("game.")
    by_name = {r[3]: i for i, r in enumerate(sb.regions) if not unowned(r[3])}
    loose = [i for i, r in enumerate(sb.regions) if unowned(r[3])]
    paired: list[tuple[int, int]] = []
    for ia, (size, _, data, name, _) in enumerate(sa.regions):
        if unowned(name):
            candidates = [j for j in loose if sb.regions[j][0] == size]
            if not candidates:
                print(f"{name} ({size} bytes): no partner in b")
                continue
            j = min(candidates, key=lambda j: sum(x != y for x, y in zip(data, sb.regions[j][2])))
            loose.remove(j)
            paired.append((ia, j))
            continue
        j = by_name.pop(name, None)
        if j is None:
            print(f"{name} ({size} bytes): only in a")
            continue
        paired.append((ia, j))
    for name in list(by_name) + [sb.regions[j][3] for j in loose]:
        print(f"{name}: only in b")
    for ia, ib in paired:
        lines: list[str] = []
        label = sa.regions[ia][3] + (f" (b's {sb.regions[ib][3]})" if unowned(sa.regions[ia][3]) else "")
        if sa.regions[ia][0] != sb.regions[ib][0]:
            print(f"{label}: sizes differ: {sa.regions[ia][0]} and {sb.regions[ib][0]}")
            differing += 1
            continue
        values, pointers = compare_regions(sa, sb, ia, ib, label, lines.append)
        if values or pointers:
            differing += 1
            print(f"{label} ({sa.regions[ia][0]} bytes):")
            print("\n".join(lines))
    print(f"{differing} region(s) differ")


def main(pa: str, pb: str) -> None:
    state_a, state_b = read_state(pa), read_state(pb)
    if state_a.version == 2 and state_b.version == 2:
        main_v2(state_a, state_b)
        return
    raw_a = [r[:4] for r in state_a.regions]
    raw_b = [r[:4] for r in state_b.regions]
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
        if state_a.version != state_b.version:
            print("note: one file is version 1 (no pointer census); the pointer words are not compared")
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
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
