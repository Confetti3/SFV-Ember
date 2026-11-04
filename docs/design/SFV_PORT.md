# Porting Ember to Street Fighter V

Started 2026-11-04. Status (2026-11-04): two copies of SFV on one PC play
whole matches over loopback GGPO with simulated lag, rollbacks across KOs and
round ends included, and finish on the same screen. The in-game synctest
passes a full match with perturbed rollbacks every 7 frames. Not yet: two
PCs, the Ember helper, rooms, and cross-copy state hashes (see Open work).

## Goal

Ember-style rollback netplay for Street Fighter V (Steam app 310950): our own
code, built from Ember's netcode, running SFV fully offline from Capcom.

## The game on this PC

| | |
| --- | --- |
| Install | `G:\SteamLibrary\steamapps\common\StreetFighterV` (G: is the Intel NVMe; not C:) |
| Steam build | 13173604 |
| Exe | `StreetFighterV\Binaries\Win64\StreetFighterV.exe`, 64,162,720 bytes |
| PE timestamp | `0x648AD3A4` (2023-06-15), the final PC build |
| Engine | Unreal Engine 4, x64 |
| DRM | SteamStub wrapper (`.bind` section holds the entry point). `.text` is **not** encrypted (entropy 6.3 bits/byte), so static analysis works on the file as installed |
| XInput | `XINPUT1_3.dll` is a static import (ordinals 2 and 3), so a proxy of that name loads before the game's entry point |
| Network imports | WinINet, WS2_32, MSWSOCK, IPHLPAPI, NETAPI32, steam_api64 |

The game's online code (Capcom's servers, CFN, its own rollback netcode) is
never used.

## Offline from Capcom: three layers

1. **Network guard (built).** The proxy starts the mod from the exe's entry
   point, before any game code. The mod hooks every way out of the process
   (Winsock connect/send/lookup including `ConnectEx` from `WSAIoctl`,
   `WSAConnectByName`, the DNS client API, WinHTTP, WinINet) and refuses
   everything but loopback and peers a session explicitly allows. Name lookups
   fail for every name but `localhost`, so the game cannot even resolve
   Capcom's hosts. If the guard cannot be installed the process ends before
   the game starts. If the proxy is present without the mod, the proxy refuses
   to start the game.
2. **Firewall (for the maintainer to add).** An outbound block on both SFV exes. Windows
   Firewall leaves loopback alone, so this does not get in the way of the
   design below.
3. **The game's own offline route (milestone 2).** The title screen has a
   "continue offline" step; taking it skips the login entirely, so the game
   never tries to reach Capcom in the first place.

In the finished design the game process only ever talks to loopback: the
Ember helper (`sf4-net`, a separate exe) carries all internet traffic, as it
does for SF4.

## Architecture

```
src/proxy/   xinput1_3.dll   forwards XInput; starts the mod from the exe entry point
src/mod/     sfv_ember.dll   NetGuard, BuildCheck, SfvAdapter (game side)
src/core/    static lib      StateLayout, RollbackSession (GGPO), SyncTest, NetPolicy
src/shared/  static lib      modules copied verbatim from SF4 Ember
src/testbed/ static lib      MockFighter: deterministic fighter with switchable bugs
tests/       sfve_tests.exe  doctest
tools/       sfve.py, sfve_mcp.py, compare.py, diffstate.py: launch, drive and compare copies
```

The seam is `GameAdapter` (`src/core/GameAdapter.hxx`): a layout of memory
regions, `Advance(inputs, resimulating)` that runs exactly one tick, and
`AfterLoad()` for post-restore fix-ups. An adapter that keeps state as a
record in its own memory keeps it equal to the game after every Advance and
AfterLoad. `MockFighter` implements it for the tests, `SfvAdapter` over SFV.

### Carried over from Ember

| From Ember | Here | Why |
| --- | --- | --- |
| GGPO fork `adanducci/ggpo@c88b667` + the 17-patch vcpkg port | `vcpkg-overlays/ports/ggpo` (unchanged) | same netcode, same hardening (spectators, input repair, bounded decoding) |
| `GgpoGateModel` | `src/shared` (verbatim) | one explicit gate instead of a bool; warnings never block, fatal latches |
| `AbortLatch` | `src/shared` (verbatim) | no callback ever closes the session |
| `PacingController`, `GgpoDisconnectTolerance`, `StateHash` | `src/shared` (verbatim) | time sync pacing and semantic hashing, for the next milestones |
| Callback rules from `GGPO_LIFECYCLE.md` | `RollbackSession` | main-thread only, preallocated save pool (`*len = 1`), rollback replays one native tick per GGPO frame |

Copied files stay byte-identical so later Ember fixes can be diffed across;
see `docs/SHARED_FROM_EMBER.md` for the source commit.

What does not carry over: SF4's memento-based `SaveState`. SFV's battle already
keeps its own snapshot store (below), so save/load is a copy of known regions,
not a walk of engine objects.

Later, shared with SF4 without copying: the `sf4-net` Rust helper (rooms,
Iroh transport, identity, tournaments) is game-agnostic apart from names and
can serve both games; the room/session protocol and the UI shell follow once
the battle runs.

## How rollback works on SFV

SFV shipped with rollback netcode, so its battle code already does the hard
part: a rollback manager holds a table of `{pointer, size}` memory regions,
copies them on Store, writes them back on Restore and then runs a list of
post-restore callbacks. Offline it simply never opens a snapshot frame.

The adapter (`SfvAdapter`) therefore:

1. Builds its `StateLayout` from that region table when a battle starts.
2. Adds what a battle tick changes that the game's snapshot leaves out: the
   battle frame's counters, the input unit's frame counters and the global
   random generator, plus everything a round's end changes (below).
3. Restores but does not hash the presentation regions (camera and pawn
   animation clocks, which follow how ticks fall into rendered frames) and
   the replay unit's tick counters (they follow the replay recorder, which
   stops at a round's end outside the snapshot). Pointer words are hashed as
   zero.
4. Runs one GGPO frame as exactly one original battle-frame update with the
   game's extra-tick (its own replay) count forced to zero, both players'
   inputs written into the per-tick input record after button config is
   applied. A rollback replays by calling the same update again.
5. Reseeds the battle's clock-seeded random generators at session start.

### Round ends

The game's own rollback never crossed a round's end, so its snapshot leaves
out what a KO changes. A KO on a guessed input, removed by the rollback that
corrects the guess, left the battle stopped. Now part of the layout:

- the rule unit's judge state (`ruleunit.judge`), which goes from 2 to 3 when
  it judges a KO or time-out;
- the match data's round results (round count, round records, the round-over
  reason the Fight state polls) and the flags the Fight and Finish states set
  (`battledata.*`);
- the round flow itself (`SfvFlow`): FlowUnit's current and requested state
  and each state's phase, sequencer and own fields, kept as a record the
  adapter captures before every save (`GameAdapter::Capture`) and writes back
  after a load. Writing it back moves the current-state shared pointer (use
  counts kept right) and rebuilds a state's phase delegate the way the game
  binds one (a closure from the game's allocator, freed by the delegate).

Outside the fight (intros, round calls, the KO and results) frames still run
in lockstep: the flow there waits on presentation a replay does not repeat
the same way. The synctest mirrors that (lockstep frames are never rolled
back; entering lockstep checks the guessed frames before it).

### Tested (2026-11-04, this PC)

- Unit tests (mock fighter): synctest at distances 1-8, injected bug classes
  found and located, two sessions over loopback, a round flow in the layout
  latching a KO where an offline run does (with rollbacks), lockstep floors.
- In game, one copy: the synctest with perturbed (wrong-input) rollbacks every
  7 frames passes a whole two-round match, rolling back across both round ends
  (the flow switched back and forth, delegates rebuilt). Phase functions are
  hashed as rvas, so a wrong rebinding would fail it.
- In game, two copies over loopback GGPO, 30 ms simulated lag each way, input
  delay 1, mashing from frame 600: 12,000 frames, about 1,500 rollbacks of up
  to 3 frames, two of them across a round's end; both finish the match on the
  same frame of the same win pose. The screens are not byte-identical (hair
  and cloth physics and small differences spread over the frame, about 5% of
  pixels).

### Open work

- **Cross-copy state hashes.** About 30 game regions hash differently between
  two copies from the first frame even while play stays in sync (pooled
  effect objects listed in a different order, per-process leftovers). Desync
  detection between peers needs per-word masks learned from such runs; the
  match data and the flow record already match.
- Pacing (frame stretching instead of holding whole frames), two PCs through
  the Ember helper, rooms.
## Prior art

[sfvrproject/sfv-rollback](https://github.com/sfvrproject/sfv-rollback)
(GekkoNet, created 2026-10-30) is a working prototype on the same idea. It has
**no licence**, so it is read as a map only: no code, offsets, signature tables
or data files are copied. Every address we use is derived from the installed
exe with our own analysis. It targets the same exe build (`0x648AD3A4`).

## Milestones

1. **Framework and rollback core** — done: this repo, tests green.
2. **Static analysis and offline boot** — done. IDA database of the installed exe
   (on D:), our own offsets for: battle frame, input record, rollback manager
   (region table, Store/Restore, post-restore list), offline title route. Fill
   `SfvOffsets.hxx` with signatures; BuildCheck verifies them.
   *Done when* the mod loads in game, the guard logs zero allowed outbound
   connections, the game boots to the main menu offline, and BuildCheck says
   verified.
3. **One-process rollback in game** — done, with one gap: the synctest
   match ran with one pad idle and the other mashing only in the perturbed
   replays, not "mashing on both pads". `SfvAdapter` reads the region table, adds
   outside state, injects inputs and runs single ticks; in-game synctest in a
   local Versus match.
   *Done when* a full match passes synctest at distance 7 with mashing on both
   pads.
4. **Two instances on one PC** over loopback GGPO — done (see Tested). Next:
   two PCs through the Ember helper.
5. **Ember integration**: rooms, Ember ID, overlay, spectators, replays.

## Not doing

- Any character, costume or entitlement unlock. Offline, SFV may treat
  characters as unavailable (the reference project bypasses the game's
  free-play check for every character); whether and how ownership is checked
  offline is the maintainer's call, as it was for SF4's DLC gate.
- Anything that talks to Capcom, ranked, CFN or Fighting Chance/Fight Money.

## Open questions for the maintainer

1. Project name and repository: local folder `sfv-ember` only for now, no
   GitHub repository. Name it, and say whether it should be pushed.
2. The firewall rule (layer 2) — commands are in the summary message.
3. Character availability offline (see Not doing).
4. The optional privacy-policy consent in SFV's first-run prompts was
   declined; the Options screen only offers to revoke it. Nothing needs it
   offline.
