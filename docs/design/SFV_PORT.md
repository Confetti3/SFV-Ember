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
(library)    ember_neutral_x64 / ember_runtime_x64   Ember's code, built from the library checkout
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
| `GgpoGateModel` | the Ember library | one explicit gate instead of a bool; warnings never block, fatal latches |
| `AbortLatch` | the Ember library | no callback ever closes the session |
| `PacingController`, `GgpoDisconnectTolerance`, `StateHash` | the Ember library | time sync pacing and semantic hashing, for the next milestones |
| Callback rules from `GGPO_LIFECYCLE.md` | `RollbackSession` | main-thread only, preallocated save pool (`*len = 1`), rollback replays one native tick per GGPO frame |

These came over as copies first (`src/shared`, 2026-11-04); since 2026-11-05
SFV Ember builds them from the Ember library itself (EMBER_LIBRARY.md section 9).

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
5. Reseeds the battle's clock-seeded random generators at session start,
   and gives the match data the same start time on both copies: every round
   start reseeds each fighter's generator from it (`battledata::kStartTime`).
6. Zeroes what the battle code allocates (its operator new, `GameAlloc`), so
   fields an object leaves unset until first use hold the same bytes in every
   process (below).

### What peers compare

The state hash has two scopes (`HashScope`). The local one (synctest, load
checks) covers everything that must replay exactly in one process. The
peers' one, which `RollbackSession` keeps for every hashed frame and as the
per-region checkpoint lists, also leaves out what two healthy processes
legitimately hold differently: dwords masked for peers and regions excluded
from peers (both still saved, restored and synctest-checked). Checkpoint
lists are sorted by size and hash, so two processes that list the game's
regions in different orders produce the same list. Masks and exclusions are
part of the layout id.

Two linked copies used to disagree on about 30 regions from the first frame
while playing identically. The causes, and what each became:

| Differed | Why | Now |
| --- | --- | --- |
| The fighters' random generators (24-byte regions) and what follows from them (CharaUnit records) | every round start reseeds them from the match data's `_time64()` start time, each process's own clock | same start time on both copies (a real desync source: the fighters draw from these every tick and for random picks) |
| CharaUnit, CharaMotionPart, CharaStatusPart, the fighter and projectile ActionParts, HUD and camera records, TimeUnit, ProjectileGroupUnit handles, the flow record's own fields | heap leftovers: padding, arrays filled on first use, the unused half of an 8-byte slot, never written (or only at a round start) | allocations zeroed |
| A VfxUnit record | the address test took an address's high half plus the next value for an address in one process only | the 4 bytes into a found address are not tested |
| Battle frame +0x198, +0x19C, +0x1B4 | updates counted since its reset, including those each process runs before its layout binds | masked for peers (only the slow-motion schedule reads +0x198; slow motion is never set) |
| Input unit +0x2214 | QueryPerformanceCounter's low part at frame-sync start | masked for peers |

`perturb <frame> <region> <offset> <add>` nudges one copy's state on purpose,
so the peers' checkpoints should differ from the next checkpoint on (the
mock's two-session test shows that; in game, see Open work).

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

No rollback starts outside the fight (intros, round calls, the KO and
results): the flow there waits on presentation a replay does not repeat the
same way. The game reads no player input there (`GameAdapter::InputsMatter`,
SFV: `Fighting()`), so `RollbackSession` gives those frames neutral inputs
and tells GGPO to ignore theirs (`ggpo_set_inputs_ignored`, the GGPO port's
`ignored-inputs.patch`): an input for such a frame that differs from its
guess is not a misprediction. Every rollback therefore starts on a fight
frame; one may still replay across the fight's end (the flow record
handles that), and a guessed KO it takes back puts the fight back. Nothing
waits for confirmed inputs, so play runs at full speed throughout. The
synctest follows the same rule: ignored frames are played and replayed
with neutral inputs, and no check restores to one's start.

### Tested (2026-11-04, this PC)

- Unit tests (mock fighter): synctest at distances 1-8, injected bug classes
  found and located, two sessions over loopback, a round flow in the layout
  latching a KO where an offline run does (with rollbacks), no check or
  session rollback starting on a frame whose inputs are ignored.
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

- **Cross-copy state hashes** (see "What peers compare"). Verified in game
  (2026-11-04, the build with the masks, two linked copies, 30 ms lag, 30 s
  rounds): every checkpoint list and every overall hash equal on both copies
  for 4,500 frames across a round end. Then `perturb 4300 global.rng 0 1` on
  one copy only: the fighters' positions diverged, and the peers' checkpoints
  differed from 4560 on (the first checkpoint after the change reached hashed
  state; until then the generator's draw had not shown). The fighters' pawn
  regions (positions) are not hashed, so a desync that only moves a fighter
  shows once it reaches hashed state, not on the frame itself.
  0 ms, other fighters and seeds (KEN vs CMY): all 76 checkpoints equal,
  frame 0 included once the reseed also cleared each fighter's last random
  draw (the game snapshots it with the generator, fighter+0xC50, and the
  reseed had left each process's pre-battle draw). That run had no
  rollbacks (2 frames of delay hide 0 ms).
  **Rollback runs (60 ms, input delay 1, about 490 rollbacks of up to 4
  frames).** RYU vs KEN, seeds 31337/4242: checkpoints differed from 2400 on
  in `battledata.results`. The cause was the battle statistics (StatsUnit,
  rva 0x4BA550): each frame's time goes into a per-player bucket chosen by
  fighter-state predicates, a frame replayed after a restore can land in a
  neighbouring bucket (two counters 1000 apart, the sum equal), and at a
  round's end the rule unit copies the statistics into the round record.
  Only UI text and a mode score read them, so the StatsUnit region and the
  records' copies are restored but not hashed (the records split into
  hashed 64-byte headers and unhashed statistics). A fighter's combo move
  count (CharaStatusPart+0x2C0/+0x2C4: both fighters starting a move on one
  frame race to write it) feeds only the statistics too and is masked.
  Restoring CharaActionPart's unregistered flags (+0xE0) was tried and
  changed nothing.

  **How rollback correctness is checked now** (2026-11-04, after the runs
  above kept finding one cause at a time):
  - The in-game synctest is the main tool (`tools/synctest.ps1`): both
    players mash, every rolled-back stretch is first played with wrong
    inputs, and rollbacks come at random spacing and depth up to 8 frames
    from a seed (`-sfve_schedule`), the way a network's do; learning mode
    tallies every region that differs. Two linked copies are the acceptance
    check (`tools/runauto.ps1`); `-sfve_hash_every=1` keeps every frame's
    hash so `tools/compare.py` names the first differing frame.
  - Every region of the game's snapshot is named by its owner
    (`RegionNames`, from docs/design/SFV_REGION_MAP.md: two IDA passes and an
    adversarial check of both): `CharaUnit[1].CharaStatusPart+0x238`, not a
    table index; 268 regions in a versus battle. Hash rules are by owner and
    run (`sfv::manager::kRunRules`), not by size.
  - A projectile's group released a dead projectile back to its free pool
    only once its UE particle actor had finished, on the engine's frame
    clock: a replay (later in wall-clock time) released it earlier than the
    played frames, and two PCs at different frame rates would release it on
    different battle frames without any rollback; the pool is last in, first
    out, so the next spawn then used a different projectile object. In the
    modes that roll back the release now follows the battle tick's own
    visual states (ProjectileRelease); a dead projectile's fade-out is cut
    short.
  - Result: randomised synctests (RYU vs KEN, KEN vs RYU, CMY vs CNL, four
    seeds, about 4,300 rollbacks) show no difference; two linked copies at
    60 ms with delay 1 match at all 76 checkpoints (RYU vs KEN, CMY vs CNL,
    CMY vs RYU, KEN vs RYU). A run that looked like the fight falling two
    ticks behind (KEN vs RYU, mash 1234, schedule 41) was the harness:
    learning mode went on from a replayed state but kept the played
    timeline's saves, so a presentation-only difference (a fighter's
    material instances) turned into a mixed-timeline comparison. Learning
    now re-saves replayed frames, as a session does. After that, five
    learning synctests (KEN vs RYU, RYU vs KEN twice, CMY vs CNL, KEN vs CMY;
    about 5,300 rollbacks) show no difference.
  - Restores pass the game's post-restore callbacks the restored state's
    frame id ({tag, input frame - 1}, as the game's own load does) and write
    back the input frame the InputUnit mirrors into the game instance. Traced
    in IDA: offline, nothing rewrites the InputUnit's counters or ring after
    a restore; its QPC sync, delay and own rollback are online-only.
  - Inputs reach the game restricted to the bits a pad can produce
    (`GameAdapter::InputBits`, SFV 0xFFF), in sessions and in the synctest's
    wrong-input passes.
  - Left out of the hash, after tracing their readers in IDA: the camera
    (nothing in the fight reads it), projectile visuals and fighters'
    material instances (UE objects recreated on restore), the battle
    statistics, and two bone positions from the UE pose (a fighter's bone 28
    and the point derived from it), which replayed ticks keep from the
    restore. The fighters' centre (screen-edge pushback, push order, facing)
    and positions are fight state and hashed; learning synctests and two
    linked pairs at 60 ms (RYU vs KEN, CMY vs CNL) agree with them hashed.
    Unowned regions (HUD element pools, the per-character unique parts, an
    AttackDataPart on a unit not enumerated yet) still use table indices.
  A boot crash seen after Steam restarted was
  Steam's (its registry named a dead client process; restarting Steam fixed
  it), not the mod's. `perturb` finds `scan`'s `game.N` names only while
  the layout is the one `scan` read (it is rebuilt between rounds).
 A save copies and hashes about 1 MB (0.7 ms on this PC,
  logged on every checkpoint line); a 4-frame rollback saves 4 times. The
  round results (557 KB) change only at a round's end and are the obvious
  first saving.
- Match end and results (done): the results menu is skipped for linked
  matches; the mod decides for both copies at the same session frame,
  rematch or back to the main menu (`matchend`), and reads the game's
  judgement (`MatchResult`: winner, rounds won, each round's finish). A
  broken linked session leaves for the main menu. The launch sets the
  rounds and the round clock; linked matches use the host's.
- Ember's interface: SFV will show Ember's own shell through a shared
  Ember library (docs/design/EMBER_LIBRARY.md); the D3D11 overlay host
  is ready for it (`src/overlay`).
- Changing fighters between matches needs Ember's room (a second match
  generation on the link), which comes with Ember's shell.
- Lockstep outside the fight used to cost speed at high ping (intros, KOs
  and round calls waited for confirmed inputs: about 12 fps at an 80 ms
  round trip). It is gone: inputs there are ignored (above), so nothing
  waits. Tested in game: a linked best-of-3 with 30 s rounds at full speed,
  580 rollbacks, the same result read on both copies and the same leave
  frame.
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
