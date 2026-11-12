"""Shows one region of a desync state file word by word, with the fields
named where the game's code says what they are.

  python tools/stateregion.py <file.state>                     list the regions
  python tools/stateregion.py <file.state> --list FILTER       ...whose name has FILTER
  python tools/stateregion.py <file.state> NAME [--all]        one region (zero words skipped unless --all)
  python tools/stateregion.py <file.state> NAME --against <other.state>
                                                               only the words that differ, both sides
  python tools/stateregion.py <file.state> --fighters          what each fighter and live projectile plays

A state file (%APPDATA%/SFVEmber/desync-states/*.state, EMBER_LIBRARY.md
section 56) holds every region of the rollback state at a desync's
checkpoint; tools/diffstate.py reads the format. Each word is shown as hex,
two int32, two float32 and a tag: `img+0x...` for a pointer into the game's
image (the image base comes from the global random generator's region, whose
address is sfv::rng::kGlobal + kState past it), `heap` for another address
(the state hash masks both, diffstate.masked), `nan`. Offsets are the run's
own and the owner's (`+run / @owner`).

Field names come from build 13173604 (IDA, every one with its rva in
docs/design/beta8/desync.md and determinism.md); a name is only given where
the code was read.
"""

from __future__ import annotations

import argparse
import math
import os
import re
import struct
import sys

# diffstate.py lies beside this script; `python -I` leaves the script's
# folder out of the import path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import diffstate  # noqa: E402

# SfvBattleLayouts.hxx: rng::kGlobal (the global generator object) and
# rng::kState (its uint32 state, the region the layout registers).
RNG_GLOBAL = 0x3966538
RNG_STATE = 0x8
IMAGE_SPAN = 0x6000000  # the image is under 96 MB

# A run's start in its owner, from the region name ("...+0xD8").
RUN_START = re.compile(r"\+0x([0-9A-F]+)(?:\.[a-z]+)?$")


def timeline_fields() -> dict[int, str]:
    # The timeline object (272 bytes, constructor rva 0x2ECAD0, which zeroes
    # it) whose +0x30..+0x110 is registered (map step 5; registration rva
    # 0x315A30, 4-byte pieces, +0x108 and +0x10C included: both are in the
    # game's snapshot). The script player fields are the frame meter's
    # (SfvFighterOffsets.hxx: SetScript rva 0x307840, SetScriptFrame rva
    # 0x3099E0); keys are object offsets, so the region's run offset is the
    # key - 0x30 (script number and frame: run +0x18; data revision and
    # condition flags: run +0xD8).
    #
    # +0x108, the data revision: written once, at CharaActionPart's
    # registration (rva 0x32F570), from the battle data's u16 at +76 for both
    # timelines; fighter code compares it with constants 2..0x1A (rva 0x35F5A0
    # among many), so it is constant for a battle.
    # +0x10C, the condition flags: set and cleared by the script's flag command
    # (rva 0x2E4340: |= or &= ~ of the command's mask), zeroed by SetScript
    # (rva 0x307840) for every new script, and tested by the command runners
    # (rva 0x302780, 0x30D570, 0x30D850, 0x30DF50): each command record's
    # require mask (+0) and forbid mask (+4) against it, a command that fails
    # them skipped (rva 0x302926..0x302943). It decides which of the playing
    # script's commands run (spawns, hitboxes, branches): fight state.
    # A difference at run +0xD8/+0xDC means the copies took different script
    # branches; run +0x18 says which script and frame set it.
    return {
        0x30: "-1 at construction (qword)",
        0x38: "-1 at construction (qword)",
        0x40: "script header (pointer)",
        0x48: "script number | frame (16.16)",
        0x50: "? | end (16.16)",
        0x58: "first attack frame | last attack frame (16.16)",
        0x60: "interrupt frame | time scale (16.16, 0 while frozen)",
        0x68: "speed (16.16, 0x10000 at construction) | phase bits",
        0x108: "data revision (battle data u16 +76, rva 0x32F570) | condition flags (each command's require/forbid "
               "masks, tested by the command runners rva 0x302780 etc.; zeroed by SetScript)",
    }


def status_part_fields() -> dict[int, str]:
    # CharaStatusPart's run at +0xC0 (372 bytes, registration rva 0x354260);
    # keys are part offsets (run +0x50 is +0x110, run +0x98 is +0x158).
    # +0x110 bit 0: the hold that stops the V-Trigger timer draining (rva
    # 0x3632E0, 0x361590; SfvBattleLayouts.hxx, kPartRunsOutsideSnapshot).
    # +0x158, the dizzy variant: picked by rva 0x35F810 when the fighter is
    # stunned, the fighter's per-tick draw (CharaUnit +0xC50, the 24-byte
    # generator pair) modulo a 4-entry weight table, or 3 when status dword
    # 203 is set; handed to the fighter's CharaVisualPart (rva 0x3A42E0). Only
    # a visual reader was found, but a difference means the fighters' draws
    # differed.
    # +0x15C, the dizzied flag: set to 1 by rva 0x35F5A0's "Stun" case (stun
    # reached the meter), cleared by the status reset (rva 0x35DD20) and by
    # rva 0x343E00 and 0x38C480; the stun add (rva 0x346250) accumulates only
    # while it is 0. Fight state.
    return {
        0x110: "V-Trigger timer hold (bit 0) | dword",
        0x158: "dizzy variant (rva 0x35F810: the fighter's draw mod a weight table) | dizzied flag (rva 0x35F5A0 sets, "
               "0x35DD20 clears; stun adds only while 0, rva 0x346250)",
    }


def action_part_fields() -> dict[int, str]:
    # ActionPart's registration (rva 0x2E8D30, through rva 0x2FED10): the
    # controller at part+0xC0 registers these runs, which merge into the one
    # 15808-byte region at part+0xD8. Keys are part offsets.
    return {
        0xD8: "the script player now playing (pointer to the ->0x3EC8 or ->0x3ED8 timeline object)",
        0xE0: "block of 32 (controller +32)",
        0x100: "block of 160 (controller +64)",
        0x1A0: "block of 80 (controller +224)",
        0x1F0: "block of 5360 (controller +304): 120-byte records, each opening with an image pointer",
        0x16E0: "block of 6712 (controller +5664)",
        0x3118: "block of 2392 (controller +12376)",
        0x3A70: "block of 1056 (controller +14768)",
        0x3E90: "facing: 1 = x mirrored (rva 0x2FB820 compares it with 1; 0x2F9310 returns it) | dword (controller +15828)",
    }


def owner_fields(name: str) -> tuple[int, dict[int, str]]:
    """The run's start in its owner and the field names known for it."""
    m = RUN_START.search(name)
    start = int(m.group(1), 16) if m else 0
    if "->0x3EC8" in name or "->0x3ED8" in name or "->0x3ED0" in name:
        return start, timeline_fields()
    if "ActionPart+0xD8" in name:
        return start, action_part_fields()
    if "CharaStatusPart+0xC0" in name:
        return start, status_part_fields()
    if name.startswith(("CharaUnit[", "ProjectileUnit[")) and name.endswith("]+0xF8"):
        # ActionUnit's run (rva 0x2FED60): +248, +252 counter, +256 player slot.
        return start, {0xF8: "dword | sequential counter", 0x100: "player slot (0 P1, 1 P2) | dword"}
    if name.startswith("ProjectileUnit[") and name.endswith("]+0x1D0"):
        # 13 vectors (map section 2); +0x00 is the position, copied to +0x20,
        # +0x30, +0x40 (EMBER_LIBRARY.md section 48).
        return start, {0x1D0: "position x, y (z, w in the next word)", 0x1F0: "position copy", 0x200: "position copy",
                       0x210: "position copy"}
    return start, {}


def image_base(regions) -> int | None:
    for size, address, _, name in regions:
        if name == "global.rng":
            return address - RNG_GLOBAL - RNG_STATE
    return None


def tag(q: int, image: int | None) -> str:
    if image is not None and image <= q < image + IMAGE_SPAN:
        return f"img+{q - image:#x}"
    if diffstate.looks_like_address(q):
        return "heap"
    return ""


def floats(b: bytes) -> str:
    out = []
    for (v,) in struct.iter_unpack("<f", b):
        out.append("nan" if math.isnan(v) else f"{v:.6g}")
    return " ".join(f"{x:>12}" for x in out)


def word_line(run_off: int, start: int, b: bytes, image: int | None, label: str) -> str:
    q = int.from_bytes(b.ljust(8, b"\0"), "little")
    ints = struct.unpack("<2i", b.ljust(8, b"\0"))
    t = tag(q, image)
    text = f"+{run_off:#06x} @{start + run_off:#06x}  {q:016x}  {ints[0]:>11} {ints[1]:>11}  {floats(b.ljust(8, bytes(1)))}"
    extra = " ".join(x for x in (t, label) if x)
    return text + (f"  {extra}" if extra else "")


def show_region(regions, name: str, show_all: bool) -> None:
    found = [r for r in regions if r[3] == name]
    if not found:
        sys.exit(f"no region named {name!r} (--list shows them)")
    size, address, data, _ = found[0]
    image = image_base(regions)
    start, fields = owner_fields(name)
    print(f"{name}: {size} bytes at {address:#x}" + (f", image at {image:#x}" if image is not None else ""))
    zeros = 0
    for off in range(0, size, 8):
        b = data[off:off + 8]
        label = fields.get(start + off, "")
        if not show_all and not any(b) and not label:
            zeros += 1
            continue
        if zeros:
            print(f"    ({zeros} zero word{'s' if zeros > 1 else ''})")
            zeros = 0
        print(word_line(off, start, b, image, label))
    if zeros:
        print(f"    ({zeros} zero word{'s' if zeros > 1 else ''})")


def show_against(regions, other, name: str) -> None:
    mine = {r[3]: r for r in regions}
    theirs = {r[3]: r for r in other}
    if name not in mine or name not in theirs:
        sys.exit(f"{name!r} is not in both files")
    _, addr_a, a, _ = mine[name]
    _, addr_b, b, _ = theirs[name]
    ma = diffstate.masked(addr_a, a, diffstate.mapped_test(regions))
    mb = diffstate.masked(addr_b, b, diffstate.mapped_test(other))
    image_a, image_b = image_base(regions), image_base(other)
    start, fields = owner_fields(name)
    count = 0
    for off in range(0, min(len(a), len(b)), 8):
        if ma[off:off + 8] == mb[off:off + 8]:
            continue
        count += 1
        label = fields.get(start + off, "")
        print("a " + word_line(off, start, a[off:off + 8], image_a, label))
        print("b " + word_line(off, start, b[off:off + 8], image_b, ""))
    if len(a) != len(b):
        print(f"sizes differ: {len(a)} and {len(b)}")
    print(f"{count} word(s) differ in {name} once addresses are masked as the hash masks them")


def int_at(data: bytes, off: int) -> int:
    return struct.unpack_from("<i", data, off)[0]


def fighters(regions) -> None:
    """Each fighter's and live projectile's script, from the regions alone."""
    by_name = {r[3]: r for r in regions}
    by_address = {r[1]: r for r in regions}
    for name in sorted(by_name):
        m = re.match(r"^((?:Chara|Projectile)Unit\[\d+\])\.(?:Chara|Projectile)ActionPart\+0xD8$", name)
        if not m:
            continue
        unit = m.group(1)
        _, _, data, _ = by_name[name]
        player = int.from_bytes(data[0:8], "little")
        # The timeline object's registered run starts 0x30 into it.
        timeline = by_address.get(player + 0x30)
        slot = by_name.get(unit + "+0xF8")
        side = int_at(slot[2], 8) if slot else -1
        facing = int_at(data, 0x3E90 - 0xD8)
        if timeline is None:
            if unit.startswith("ProjectileUnit") and not player:
                continue
            print(f"{unit}: side {side}, facing {'mirrored' if facing == 1 else facing}, script player {player:#x} (no timeline region)")
            continue
        t = timeline[2]
        script, frame = int_at(t, 0x48 - 0x30), int_at(t, 0x4C - 0x30)
        print(f"{unit}: side {side}, facing {'mirrored' if facing == 1 else facing}, script {script} frame "
              f"{frame / 65536:.2f} ({timeline[3]})")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("file")
    p.add_argument("name", nargs="?")
    p.add_argument("--list", nargs="?", const="", metavar="FILTER")
    p.add_argument("--all", action="store_true")
    p.add_argument("--against")
    p.add_argument("--fighters", action="store_true")
    args = p.parse_args()
    regions = diffstate.read(args.file)
    if not all(r[3] for r in regions):
        sys.exit(f"{args.file} is a dump without region names; use a desync state file")
    if args.fighters:
        fighters(regions)
        return
    if args.name is None:
        flt = args.list or ""
        for size, address, _, name in regions:
            if flt in name:
                print(f"{size:>8}  {address:#014x}  {name}")
        return
    if args.against:
        show_against(regions, diffstate.read(args.against), args.name)
        return
    show_region(regions, args.name, args.all)


if __name__ == "__main__":
    main()
