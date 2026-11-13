# Rollback fixes: the address rule and the CRT math (2026-11-12)

Two durable fixes from the rollback inventory of 2026-11-12 (items 1 and 3), on
`b8/rollback-fixes` from `b8/integrate` 317c59a. Neither was built or run when this was
written; the checks below are from the state files already on the desktop, and the laptop
runs at the end are what must confirm them. Match version stays 0.6: beta.8 is unpublished,
and the room build's layout part (below) keeps these builds apart from earlier beta.8 builds.

## 1. The state hash tests only 8-byte boundaries for addresses

**What it was.** The state hash (`StateLayout::HashRegion`), the start agreement's view of
addresses (`AddressUnits`) and the tools zeroed every 8-byte window, tested on every 4-byte
boundary, that passes `LooksLikeAddress` (at least 2^40, at most 0x7FFFFFFFFFFF, a multiple
of 4). Two small dwords side by side pass that too: a value with its low bits clear, then one
between 256 and 32767. So health, stun and gauge values, frame counters and input bits were
zeroed wherever such a pair lay. Overlapping windows were settled by a tie-break
(`kSmallLowHalf`, `desync.md`), which still left the S6b false desync for a pointer in the
first 32 KB past a 4 GB boundary preceded by a dword of 0x10000 or more (Cody's timeline in
The maintainer's state at 12780 holds one: a script data pointer at run +0x60 after the 16.16 frame
199 at +0x5C).

**Rule 2** (`kAddressRuleVersion`, `core/StateRegions.hxx`). Only the 8-byte words that start
on an 8-byte boundary of their real address are tested; each that passes is zeroed whole, and
nothing else is. Bytes before a region's first boundary, and a word cut by its end, are hashed
as they are. The windows never overlap, so the tie-break went with nothing kept: it only chose
between two overlapping windows. `ZeroAddresses`, `AddressUnits` and `diffstate.masked` run
the same scan; `stateregion.py` shows a run's words at those boundaries.

**Real address, not the offset in the saved state.** The scan has always started from the
region's real address (`r.ptr`, also for a hash of a saved buffer), and has to: the game
registers runs at any 4-byte place in an object, and of the about 554 regions in each state
file, 522 or 523 start on an 8-byte boundary and 31 to 33 start 4 bytes past one
(`CharaUnit+0x894` and the like). Region bases are not all 8-aligned, so the offset inside the
saved buffer says nothing about alignment.

**What it rests on.** A word must lie on the same boundary in every process, and pointers must
lie on boundaries:
- The compiler places a pointer field on an 8-byte boundary of its object, and the game's
  allocator gives every object at least 8-byte alignment. Checked: the same-named regions of
  the 17 named state files (39,712 pairs across files from different processes) never differ
  in their address mod 8. They do differ mod 16 (52 pairs), so the rule must not assume 16.
- Every pointer found in 22 files (the 17 state files and the 5 Z42 dumps; a pointer is a
  value into, or within 1 MB of, one of the file's own regions, or into the game's image):
  4,743, all on an 8-byte boundary, none 4 bytes off one. A looser test (high half near the
  file's heap or in the DLL range) finds 33 words 4 bytes off a boundary with no pointer
  beside them, all values: the 632-byte run's counters next to button bits, round and
  StatsUnit statistics (unhashed), and `0x0000020000000000`-like words.
- A pointer packed 4 bytes off a boundary would now be hashed as it is. It would show as a
  false desync that names its region in the first runs, never as a hidden one.

**What it changes.** Value-like windows zeroed in hashed regions (the windows that are no
pointer by either test above), old rule against rule 2:

| Files | Old | Rule 2 |
|---|---|---|
| all 22 | 4,616 | 3,410 |
| The maintainer's 12780 | 120 | 71 |
| S6b P1 at 60 | 75 | 34 |
| R2 (two copies) | 115, 121 | 68, 74 |
| R3 | 130 | 79 |
| The maintainer's 360 | 86 | 34 |
| The maintainer's 1800, 10020, 3360, 16140 | 340 to 395 | 293 to 320 |

What rule 2 unhides is every pair 4 bytes off a boundary: the command history's 40-byte
records (15 a fighter), the 632-byte run's pairs, motion part words, action part words.

**What is still hidden** (1,943 windows over the 17 state files, by run):
- `InputUnit[n]+0x178`, the input ring: 1,024 (in the four files whose frame numbers fell in
  range); no pointer seen there in any file.
- Each fighter's command history `CharaCommandPart->0xD8+0x10`: 484, plus about 100 near the
  heap's high half. It does hold heap pointers (some 110 over the files), at varying places in
  its records.
- The unowned 632-byte run (Attack Data, inventory item 5): 361; no pointer.
- Single words: `inputunit.counters` 16, `CharaUnit+0x894` 17 (which also holds 34 pointers),
  `CharaActionPart+0xD8` 13 (1,271 pointers), `CharaStatusPart+0xC0` 11, `CharaUnit+0x9B0` 6,
  `battleframe` 4, `CharaUnit+0x178` 4, `CharaStatusPart+0x508` 3.

A cheap further tightening, not built: a per-owner mark "holds no addresses" for the runs where
no pointer was ever seen (the input ring, `inputunit.counters`, `battleframe`, the status
part's two runs, `CharaUnit+0x178`, and the 632-byte run once item 5 names its owner), so the
address test skips them. That unhides about 1,420 of the 1,943 (73%), changes the layout id,
and fails as a named false desync if a pointer ever appears there. It does not help the
command history or the action part, which hold pointers among their values: those need the
per-owner table of pointer slots (inventory item 1 (b)), a later project.

**Re-diff of the two-copy pairs** (`tools/diffstate.py`, old and new): F5, R2 at 3000 and the
three S6b copies at 60 (three pairs) differ in exactly the regions they did before. R2 and S6b:
only `inputunit.counters` +0xA4, the sync clock the peers' hash leaves out. F5: the six regions
of its known one-ulp projectile difference (`desync.md`, LOD), the material part
(presentation), `inputunit.counters` +0xA4 and `battleframe`, whose differing words are the
per-process update counters and clock the peers' hash leaves out (now shown whole). No compared
word that the old rule hid differs: no hidden divergence in these files. The maintainer's desync states
have no partner file yet.

**The start agreement** skips a dword that is an address on either side (`ApplyRegion`). With
rule 2 that is only a word on a boundary, so a differing value pair 4 bytes off one is now
repaired from P1's state instead of being left as it was.

**Tests.** `StateRegionsTests`: only boundary words are tested; the chunk-edge cases moved onto
real boundaries (a region 4 bytes past one); the S6b case and the S6b tail (pointer after
0x00C70000) hash equal with only the pointer's dwords as address units; a value pair 4 bytes
off a boundary hashes apart when the values differ, and the same pair on a boundary is still
zeroed (the cost left); a pointer packed 4 bytes off a boundary hashes apart; an empty layout's
id carries the rule. `StartAgreementTests`: the repair writes such a value pair and still leaves
an address on a boundary alone. `diffstate.py --selftest` has the rule's cases. The new scan was
checked against a whole-region model over 400 random regions (every start, chunk edges), and
every test case above against that model, in Python; the C++ itself is unbuilt.

## 2. One CRT math path in every mode that rolls back

The fight calls msvcr120's `sinf`, `cosf`, `atan2f`, `acosf`, `tanf` and `powf`
(EMBER_LIBRARY.md section 56). The CRT runs FMA3 code where the CPU and Windows support it and
SSE2 code elsewhere, and the two may round a result differently: a player on a CPU without FMA3
against one with it could diverge for real. Every log in `b8-runs` reads "can use FMA3; it now
uses FMA3"; five replays played with FMA3 off matched their FMA3 recordings, which shows the
paths agree on those fights, not on all.

**Now** (`mod/ModConfig.hxx`: `RollsBack`, `CrtMath`, `kRollbackCrtMath`, `ChooseCrtMath`;
`dllmain.cxx`: `SetCrtMath`). Once, at start, before game code:
- netplay (room matches, watchers, replay playback) and the synctest call `_set_FMA3_enable(0)`:
  SSE2, which every x64 CPU runs, on every copy;
- the other modes (probe, which is offline SFV under the proxy; inert; the photo booth) keep
  the CRT's own choice;
- a development build's `-sfve_fma3=0|1` still chooses (`byTest`), so a test can run either
  path. A player build never reads the option (`kPlayerOptions`), and `ChooseCrtMath` ignores it
  for a player build too.
- One log line: `crt: msvcr120 math uses SSE2 in mode netplay (every copy of a battle that rolls
  back takes it; this CPU can use FMA3)`, or the test's or the CRT's reason.

**Training.** Training is never a room's battle: `SfvTraining.hxx` follows SFV's own Training,
which the netplay mode plays unchanged, with nothing saved, rolled back or compared. A player's
game is always in the netplay mode (its launcher passes `-sfve_mode=netplay`), and the CRT's
switch belongs to the whole process, so Training there takes SSE2 as well. That is harmless,
and switching between battles while the game's threads run is not worth what it would save.
Offline SFV without the launcher (probe) is left alone.

**Tests.** `BuildKindTests`: SSE2 in netplay and synctest, the CRT's choice elsewhere; a
player build's command line with `-sfve_fma3=1` (also given twice) still SSE2, the option noted
as ignored; a config holding it is not taken from it in a player build; the development
override both ways, and a value that is no path ignored.

**Tools.** `synctest.ps1 -fma3 0|1` (and it prints the `crt:` line); `runroom.ps1 -fma3 A,B[,W]`
per copy, so one run can play copy 0 on FMA3 and copy 1 on SSE2.

## 3. The layout id and what follows from it

`StateLayout::LayoutId` now starts from a hash of `kAddressRuleVersion`, and
`MakeLayoutRulesId` ends its text with `; address rule 2; rollback math SSE2`. Two builds that
hash, or compute, a fight differently then never share a room build, a start agreement or a
replay's build.
- `data/pick-space.json` embeds the room build, `.../0.6/55c1d32e7a18ea97/layout-72200b5db368767a`;
  its layout part must become the new id. No game is needed: the id is computed from tables
  alone (`MakeLayoutRulesId` lays the state out at made-up addresses). Build, run the tests:
  `RoomBuildTests` ("the room host's pick space was exported by a build with these rollback
  rules") fails and prints the new id; put it after `layout-` in the file's `build`, leave the
  pick space and its fingerprint as they are, and build again (the file is embedded). A
  `catalog-export` from the game gives the same line.
- The room host takes the build from that file, so it is rebuilt from the final source, as
  already planned for beta.8; until then it refuses rooms of this build.
- `kMatchBuild` stays `sfv-ember/13173604/0.6`.
- Replays recorded by earlier beta.8 builds carry the old room build, so playback refuses them
  (`-sfve_replay_any_build=1` plays them for diagnosis, but their checkpoints were hashed by
  rule 1 and will not match).

## 4. What the laptop runs must show

On one development build of the final source, then one set of runs:
1. `ctest` / the test programs: all pass once `data/pick-space.json` has the new layout part.
2. Rule 2: the S6b, R2 and R3 room runs again, every checkpoint equal, and a synctest PASS:
   ```
   powershell -File tools\runroom.ps1 -rematches 1 -padAnswers -skipAfter 90 -lag 30 -watchers 1 -p1 RYU:0:1 -p2 CNL:0:2
   powershell -File tools\runroom.ps1 -games 2 -rounds 3 -lag 30 -skipAfter 90
   powershell -File tools\runroom.ps1 -autoSkip -watchers 1 -rematches 1 -padAnswers
   powershell -File tools\synctest.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moveset supers -idle 1 -practice -frames 7000 -check
   ```
   A desync there that names a region is either a pointer packed off a boundary (the rule's
   assumption failing: look at the word with `stateregion.py --against`) or a divergence the
   old rule hid; the start agreement's repair lines may also name value words it now repairs.
3. The CRT path: every log has `crt: msvcr120 math uses SSE2 in mode netplay` (or `synctest`).
   Then the paths against each other:
   ```
   powershell -File tools\synctest.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -moves -practice -frames 7000 -check
   powershell -File tools\runroom.ps1 -games 2 -rounds 3 -lag 30 -fma3 1,0
   python tools\runreplay.py --lag 30
   ```
   The `-fma3 1,0` run plays one copy on each path: equal checkpoints mean the paths agreed on
   that fight; a desync there is the divergence this fix removes (its region lines say where).
   Replays made before this change cannot be compared with this build (above).
