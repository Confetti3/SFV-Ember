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
  and each state's phase, bound phase function (as an rva), sequencer and own
  fields, kept as a record the adapter refreshes after every update and load
  and writes back after a load. Writing it back moves the current-state
  shared pointer (use counts kept right) and rebuilds a state's phase
  delegate the way the game binds one (a closure from the game's allocator,
  freed by the delegate). Bind checks the delegate shape, the state list and
  the class sizes (read from the flow builders' allocations); classes whose
  own fields hold pointers keep only their base. If a check fails later the
  record stops touching the game and logs why.

Outside the fight (intros, round calls, the KO and results) frames still run
in lockstep: the flow there waits on presentation a replay does not repeat
the same way. The synctest mirrors that: before a lockstep frame it checks
the guessed frames since the last confirmed one, and no check restores a
state before a lockstep frame.

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

- In game, two copies linked through Ember's helper (`sf4-net`, Iroh, the
  loopback bridge), from a cold start with no input at all
  (`tools/runauto.ps1`): both boot past the offline dialogs to the main
  menu (about 20 s), link (host's grant and setup, joiner's setup), launch
  the same battle from the main menu with the players' picks (host P1,
  joiner P2) and fight it with rollback: 3,000 frames with 30 ms lag, 336
  rollbacks, the same frame on both screens.

### Boot and match start without menus

- **Offline dialogs** (`OfflineDialogs`). Boot's failed EULA download
  ("Failed to retrieve data. [80003]", and the other CDN downloads' codes)
  is dropped at the error-dialog service, which is only one listener on the
  game's error broadcast: boot still sees the code and goes offline, and it
  waits only while that service counts an open dialog. The offline notice
  (`Dialog_INFO_0011`) is dropped by its id; the title then waits for its
  boot window's "Login", which offline comes only when that notice closes,
  so the mod makes the same two writes that command makes. If the title
  never waits that way the notice is shown after all. `-sfve_dialogs`.
- **Game thread** (`GameScene`, `GameThread`). The scene manager's tick is
  hooked: it runs queued work once a frame between two frames of a state,
  and gives the current scene state (status `scene`).
- **Launch** (`VersusLaunch`). SFV's battle jump (`BattleJumpSetupState`,
  the game's way from a battle into training) started from the main menu
  with a target of the mod's own: local versus, versus CPU or training.
  Before its first step the mod gives the battle flow a loader (leaving the
  main menu resets the flow, and that step reads the previous battle's);
  after its second step it writes the fighters, costumes, colours and stage
  into the new mode's data object through the object's own setters. The
  game loads and enters the battle itself (about 4 s).
  `launch versus RYU:0:1 CNL -`. The game's ownership check is skipped
  while developing and turned on (`-sfve_ownership=1`) in the public
  version (the maintainer, 2026-11-04).
- **Linked start** (`MatchLink` setups, `HelperLink`). Each side brings a
  setup (`-sfve_fighter=CODE[:costume[:colour]]`, the host also
  `-sfve_stage=CODE`); the host's travels in the grant, the joiner's
  answers it. Once both are known and the link is ready, both copies launch
  the same battle on the main menu.

### Open work

- **Cross-copy state hashes.** About 30 game regions hash differently between
  two copies from the first frame even while play stays in sync (pooled
  effect objects listed in a different order, per-process leftovers). Desync
  detection between peers needs per-word masks learned from such runs; the
  match data and the flow record already match mid-fight.
- **Save cost.** A save copies and hashes about 1 MB (0.7 ms on this PC,
  logged on every checkpoint line); a 4-frame rollback saves 4 times. The
  round results (557 KB) change only at a round's end and are the obvious
  first saving.
- After the match: the game's results menu waits for a player; linked copies
  need the same choice (rematch or main menu) made for both.
- Picking: fighters and stage come from the command line or the pipe; an
  in-game way to host, join and pick (overlay) is next.
- Two PCs over a real network (one PC so far), rooms.

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

### Plan for the full port (2026-11-04)

How SF4 Ember is split (sf4-current): the `sf4-net` helper only carries
traffic (Iroh QUIC, admission, Raft room recovery, a loopback UDP bridge for
GGPO, HTTP to the bridge server). Rooms, the match protocol, GGPO, results and
chat all run in C++ in the game process; GGPO only ever talks to a loopback
port the helper relays. SFV Ember keeps that split, so the network guard's
loopback-only rule stays true in the finished design.

| Step | Work | Where |
| --- | --- | --- |
| A. Rollback quality | lazy hashing (done), pacing by frame stretching (continuous rift, `PacingController`), presentation during replays (sound, effects), learned cross-copy hash masks for desync detection, auto input delay from the helper's probe | sfv-ember |
| B. Helper link | `HelperProcess`/`HelperClient` and the IPC framing from Ember; `sf4-net` started with an SFV game id so SFV and SF4 peers can never meet (ALPNs, invitation prefix, ports, handshake magic); GGPO through the helper's loopback bridge; SFV input size in the packet bound | sfv-ember + a game id option in `sf4-net` (Ember repo) |
| C. Rooms and match flow | the room/session layer made game-neutral (match data without Dimps types), SFV's character select driven from it, results from the round judge | shared session code, SFV adapters |
| D. Overlay | ImGui on DX11 (`IDXGISwapChain::Present`/`ResizeBuffers`), the Ember shell, controller navigation | sfv-ember |
| E. Services | Ember ID, Discord, the bridge server accepting `game = "sfv"`, spectators (GGPO spectator backend), replays (SFV's own format) | Ember helper and server |

## Not doing

- Any character, costume or entitlement unlock. Offline, SFV may treat
  characters as unavailable (the reference project bypasses the game's
  free-play check for every character); whether and how ownership is checked
  offline is the maintainer's call, as it was for SF4's DLC gate. (The development
  build skips the launch's ownership check by the maintainer's decision; the public
  version keeps it, see open question 3.)
- Anything that talks to Capcom, ranked, CFN or Fighting Chance/Fight Money.

## Open questions for the maintainer

1. Project name and repository: local folder `sfv-ember` only for now, no
   GitHub repository. Name it, and say whether it should be pushed.
2. The firewall rule (layer 2) — commands are in the summary message.
3. Character availability offline (see Not doing). Decided 2026-11-04:
   development skips the game's ownership check in the launch; the public
   version runs it (`-sfve_ownership=1`). The check judges both players by
   each copy's own entitlements, so for the public version two players who
   own different content still need a rule: (a) pick only what both own
   (the setups would carry ownership), or (b) each copy checks only its own
   player's pick and shows the opponent's as picked, as online play does.
4. The optional privacy-policy consent in SFV's first-run prompts was
   declined; the Options screen only offers to revoke it. Nothing needs it
   offline.
