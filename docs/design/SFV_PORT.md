# Porting Ember to Street Fighter V

Started 2026-11-04. Status: framework and rollback core built and tested
against a mock game; nothing has touched the real game yet.

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
tests/       sfve_tests.exe  doctest, 17 cases
```

The seam is `GameAdapter` (`src/core/GameAdapter.hxx`): a layout of memory
regions, `Advance(inputs, resimulating)` that runs exactly one tick, and
`AfterLoad()` for post-restore fix-ups. `MockFighter` implements it today;
`SfvAdapter` will implement it over SFV.

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

The adapter therefore:

1. Builds its `StateLayout` from that region table when a battle starts.
2. Adds the state the battle ticks advance that the game's snapshot leaves
   out (the reference project found input-history counters, battle-frame
   counters, a tick counter, and the pawn/camera animation clocks; we confirm
   each with our own synctest).
3. Marks the animation clocks **restored but unchecked**: they depend on how
   ticks fall into rendered frames, which differs per machine, and hashing them
   gives hundreds of false desyncs a match.
4. Masks pointer words (heap and vtable addresses) out of the hash, learned
   from local matches and shipped with the mod so every peer masks the same
   words.
5. Runs exactly one battle tick on demand through the original battle-frame
   function, with both players' inputs written into the game's per-tick input
   record (after the game applies button config, so custom layouts and
   keyboards agree on both sides).

Round intros, round calls and KOs may need lockstep rather than rollback at
first (the reference project does this); revisit once the basic loop works.

### Driving the engine's own rollback from GGPO

The battle already does everything a rollback needs; the port supplies the
frame ids and the inputs, not a second save system:

| GGPO asks | The game does |
| --- | --- |
| save frame f | its per-tick Store tasks copy every region into its ring once a snapshot frame is open; we open frame f each tick, and the save callback records "ring frame f" |
| load frame f | its own Restore of ring frame f, which also runs its post-restore callbacks |
| advance K frames | one battle-frame update with K extra ticks (how its own rollback resimulates), each tick's inputs taken from GGPO's queue at the input-record hook |

Inside one rendered frame: GGPO calls (add local input, which may load and
queue K resimulated frames; synchronize; advance) only queue inputs and frame
ids. Then the original battle-frame function runs once with K extra ticks,
consuming the queue tick by tick. GGPO never reads save buffers (only the
pointer and checksum), so recording "ring frame f" in the save callback and
having the game fill it during the tick is safe.

What the game's snapshot leaves out is added by the adapter as extra regions
(saved and restored around the game's own Store/Restore), found with the
in-game synctest.

### Proof before networking

`RunSyncTest` saves every frame, rolls back `distance` frames every `distance`
frames, replays with the same inputs and compares each replayed frame's hash
with the original, naming the regions and first byte that differ. Against
`MockFighter` it passes at every distance 1-8, and it catches each injected
bug class (state outside the layout, a simulation that reads a non-input). The
same harness runs inside SFV in milestone 3: that is how missing state is
found.

### Tested so far (mock game, this PC, 2026-11-04)

- Save/load round trip, masked words, unchecked regions, layout ids.
- Synctest at distances 1-8; finds and locates both bug classes.
- Two `RollbackSession`s over loopback UDP in one process: 600+ frames,
  hundreds of rollbacks, every confirmed frame hash equal on both peers and
  equal to an offline run; input delay 2; an unmasked pointer shows up as a
  cross-peer mismatch while play is identical; a peer leaving ends the other
  session cleanly.
- Network policy: loopback only, IPv4/IPv6/mapped, allowed peers, name lookups.

Not tested: anything in the real game. The proxy and guard are built
(`xinput1_3.dll` exports match the game's ordinal imports) but have not been
run.

## Prior art

[sfvrproject/sfv-rollback](https://github.com/sfvrproject/sfv-rollback)
(GekkoNet, created 2026-10-30) is a working prototype on the same idea. It has
**no licence**, so it is read as a map only: no code, offsets, signature tables
or data files are copied. Every address we use is derived from the installed
exe with our own analysis. It targets the same exe build (`0x648AD3A4`).

## Milestones

1. **Framework and rollback core** — done: this repo, tests green.
2. **Static analysis and offline boot.** IDA database of the installed exe
   (on D:), our own offsets for: battle frame, input record, rollback manager
   (region table, Store/Restore, post-restore list), offline title route. Fill
   `SfvOffsets.hxx` with signatures; BuildCheck verifies them.
   *Done when* the mod loads in game, the guard logs zero allowed outbound
   connections, the game boots to the main menu offline, and BuildCheck says
   verified.
3. **One-process rollback in game.** `SfvAdapter` reads the region table, adds
   outside state, injects inputs and runs single ticks; in-game synctest in a
   local Versus match.
   *Done when* a full match passes synctest at distance 7 with mashing on both
   pads.
4. **Two instances on one PC** over loopback GGPO, then two PCs through the
   Ember helper.
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
3. Testing on this PC: launching SFV (inert mod first, guard only), and later
   two instances side by side.
4. Character availability offline (see Not doing).
