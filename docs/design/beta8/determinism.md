# Beta.8: determinism items from the comparison review

The owner asked for the comparison review's determinism recommendations (items 4.1, 4.2, 4.5
and 4.9 of the review) built in Ember's own design. Nothing here comes from the
other project's code, tables or text: each item is our own design over facts read in our IDA
database (build 13173604, the laptop's `D:\sfv-re\StreetFighterV.exe.i64`) and our own code.
Every address is an rva of that build.

Branch `b8/determinism` (from `b8/integrate` 4b78a19). Not built, not run: the laptop runs below
say what must be checked.

## 1. The words that must never be masked (4.1)

No rule changed; the layout id stays `72200b5db368767a`, so `data/pick-space.json` needs no
re-export for this branch.

- **The timeline objects** (`->0x3EC8/0x3ED0/0x3ED8 +0x30`, 272 bytes, constructor rva
  0x2ECAD0 zeroes them; registration rva 0x315A30 registers +0x30..+0x110 in 4-byte pieces, so
  +0x108 and +0x10C are in the game's snapshot):
  - +0x48/+0x4C (run +0x18): script number and frame (already named);
  - +0x108 (run +0xD8): the **data revision**, written once by CharaActionPart's registration
    (rva 0x32F570) from the battle data's u16 at +76; fighter code compares it with constants
    2..0x1A (rva 0x35F5A0 and others): constant for a battle;
  - +0x10C (run +0xDC): the **condition flags**. The script's flag command sets and clears bits
    (rva 0x2E4340), SetScript zeroes it (rva 0x307840), and the command runners (rva 0x302780,
    0x30D570, 0x30D850, 0x30DF50) test each command record's require mask (+0) and forbid mask
    (+4) against it; a failing command is skipped (rva 0x302926..0x302943). It decides which of
    a script's spawns, hitboxes and branches run: fight state.
- **CharaStatusPart's +0xC0 run** (372 bytes):
  - +0x158 (run +0x98): the **dizzy variant**, picked by rva 0x35F810 from the fighter's draw
    (CharaUnit +0xC50, the 24-byte generator pair) modulo a 4-entry weight table, or 3 when
    status dword 203 is set; handed to the visual part (rva 0x3A42E0);
  - +0x15C: the **dizzied flag**, set by rva 0x35F5A0's Stun case, cleared by rva 0x35DD20,
    0x343E00 and 0x38C480; the stun add (rva 0x346250) accumulates only while it is 0.
- `tools/stateregion.py` names them (`timeline_fields`, new `status_part_fields`, which also
  names +0x110's V-Trigger hold from SfvBattleLayouts.hxx), with this evidence as comments.
- `SfvBattleLayouts.hxx` has a "Never masked" block before the projectile group's rule listing
  these words and the generators: a difference there is a real divergence.

When the friend's `.state` files arrive: `python tools\stateregion.py mine.state
"CharaUnit[0].CharaActionPart->0x3EC8+0x30" --against theirs.state` (and `->0x3ED0`,
`->0x3ED8`, and `CharaUnit[n].CharaStatusPart+0xC0`); a difference at run +0xD8/+0xDC means
the copies ran different script branches, and run +0x18 says which script and frame.

## 2. Start state agreement before frame 0 (4.2)

`core/StartAgreement.hxx` (game-neutral, tested against MockFighter), driven by
`NetplayMode::PrepareStart` / `AgreeStart`.

### When and what

After Bind, the reseed (`ReseedRandom`), the input-record check and a Practice room's gauges
(everything `StartSession` did before the session's start), the battle is frozen at the state
the session will save as frame 0. `StartSession` is now three steps:

1. `PrepareStart`: the room's settings (`RoomBattle::SessionConfig`, which also releases the
   runtime's port), the reseed and the gauges, then `BeginStartAgreement`.
2. `AgreeStart`, every frame (the battle stands still, as it did while the match was not
   known), until the agreement finishes.
3. The session's start as before, with the replay header's `startHash`.

A battle left meanwhile (`Leave`, `NewBattle`) drops the agreement and its socket.

### Who follows whom

P1 is the reference: its session streams to the watchers and its state is the room's. P2 and
**every watcher** follow it. So a watcher starts from the same state as both fighters by the
same repair, not by a hash it could only compare: a watcher that differs would otherwise watch
another fight until the end of the game. P1 waits for P2 (required) and for each watcher it
streams to, but for a watcher only a second after P2 is done (`watcherGraceMs`), and a
watcher gives up after 3 s (P1 drops a watcher that has not synchronized 5 s after a fighter,
`SpectatorPolicy::SyncDeadlineMs`). Fighters give up after 8 s; the session then starts as it
is, and a state that still differs shows at the first checkpoint, as before.

### The exchange

The follower asks, the reference answers from a copy of its state taken at Begin; it keeps
nothing per question, so a lost datagram is simply asked again (up to 16 questions open,
retried after 100 ms):

1. **Summary**: layout id, peers' hash of the whole state, region count. Equal hash: done
   (the usual case: one round trip). Another layout id: nothing compared (the room's build
   check makes this unreachable between two beta.8 copies).
2. **Region list** (36 entries a datagram): identity, peers' hash, size, the reference's index.
   A region's identity is a hash of its name and size when the name is its owner's
   (RegionNames: `CharaUnit[1].CharaStatusPart+0x238`, `battledata.results`; the same in every
   process); SFV's unowned `game.N` names are table positions, which two processes order
   differently, so they have no identity. Pairing (`PairRegions`): an identity seen once on
   each side pairs; the rest pair by equal size and hash, then by a size only one leftover
   region has on each side; anything else is logged as unpaired and left alone.
3. **Bytes** of each paired region whose hash differs, in 768-byte chunks, each with the
   reference's address units (one bit per 4 bytes, `StateLayout::AddressUnits`: the same scan
   the peers' hash zeroes addresses with, over the region with masked words zeroed first).
4. **Apply** (`ApplyRegion`), 4 bytes at a time, only where the bytes differ and none of these
   holds: this copy's bytes hold an address there, the reference's do (an address of its
   process, meaningless here), the 8-byte word is masked (`kRunRules`, a flow record's
   leftovers: never compared), or the dword is left out of the peers' hash (`IgnoreForPeers`:
   a clock reading, each process's own). Unchecked regions and regions not compared with peers
   are not listed at all (presentation, each copy's own). Then `GameAdapter::AfterLoad` runs,
   as after a GGPO load (the game's post-restore callbacks, the flow record written back), and
   the state is hashed again.
5. **Done** with the result and the new hash, repeated until P1 acknowledges it (at most 1 s).

### The transport, and why not the room link

The datagrams use the match's own UDP path: the session's local port, bound for the agreement
after `ReleaseRuntimePortToGgpo` and closed before `ggpo_start_session` binds it, sending to the
peer's loopback port (the match's bridge, or the other copy on one PC). The pre-battle message
the brief names travels on the room client's link, but the library's `SessionClient::Step`
(read only for this work) hands unknown message types to nothing ("got unrecognized message
type"), and `BattleVerifier` sees only `BattleHashV2`; carrying region bytes there would need a
library change. The GGPO path needs none, and it is the path whose bytes matter:

- The helper's bridge (`rust/sf4-net/src/bridge.rs`) forwards any datagram from the registered
  local port, up to the match's `max_packet` (`GgpoMaximumPacket` = 1,064 bytes; a larger one
  ends the bridge). Every datagram here is at most 900 bytes (static_asserts).
- Our GGPO port drops any datagram whose type it does not know (`reject-invalid-udp-messages.patch`,
  whose own comment anticipates "the GGPO port for NAT/relay registration before GGPO binds it").
  Ours carry 0xE1..0xE6 at GGPO's type byte (offset 4), so a late one is dropped there; GGPO's
  sync packets reaching the agreement are refused by magic, version, type and the match token
  (the room's seed).

### Reports and logs

- `start:` lines on every copy: the reference's hash and peers, each peer's result; a
  follower's hash, P1's, the pairing, and per repaired region the dwords taken and kept and the
  hashes before, after and P1's.
- A follower that found differing regions files a problem report through the desync path
  (`ReportStartRepair`: the same spool and courier, titled "SFV start state repaired: build
  ..." or "... for a watcher", its own Bugsink group), with its state **before** the repair as
  the frame-0 state file for `tools\diffstate.py`.
- `tools/runroom.ps1` no longer counts the repair's `desync report:` lines as desyncs.

### Replays

The replay header gains `startHash` (16 hex digits, written only when known): P1's peers' hash
of the start state as the copies agreed on it (P1's own on P1; the hash P1 answered on P2 and
on a watcher). A playback has no peer: it compares its own start state with it before its
first frame (`NotePlaybackStart`): equal is logged; a difference is logged ("replays: desync:
the playback starts from state ...") and counted as a differing checkpoint, so the playback
ends as failed, as a later checkpoint's difference already makes it. It is not refused: the
existing rule is to play on and report, and the frame-0 checkpoint the recording kept is
compared the same way already. A header without the field (beta.7 and older) compares nothing.

### Version

The protocol changes what happens between Ready and the first frame, so copies with and
without it must not meet. `picks::kMatchBuild` is already `sfv-ember/13173604/0.6` for beta.8
(beta.7 is 0.5) and beta.8 has not shipped, so it is not bumped again.

### Tests (`sfve_tests`, `tests/StartAgreementTests.cxx`)

- a follower with two dwords changed takes P1's bytes, keeps its own clock (peers' dword) and
  heap pointer (masked word), and the two then play 600 frames with equal peers' hashes;
- an equal start: nothing written; lost datagrams (every third) are asked again;
- a watcher and P2 both repaired, P1 waiting for both; P1 waiting only the grace for a silent
  watcher; both sides giving up at the time limit; another layout compared no further;
- pairing by identity, by hash and by a unique size (regions listed in another order);
- the apply step leaves either side's addresses, masked words and peers' dwords alone;
- datagrams round-trip, a cut or another match's datagram and a GGPO-shaped one are refused,
  and every type byte is one GGPO does not use;
- **two loopback peers over real UDP, P2's state perturbed before frame 0**: the agreement
  repairs it on the ports the sessions then bind, and both GGPO sessions' checkpoints are
  equal (`LoopbackPair` gained a hook that runs before the sessions start).

`ReplayTests` (the header's `startHash` round-trips; absent reads 0), `DesyncReportTests` (the
repair's title and comment), `BuildKindTests` (a player build ignores the two new options).

## 3. The random draw audit (4.5)

`mod/RngAudit`, development only: hooked only with `-sfve_rng_audit=1` (a player build reads
the option as absent); the site is in the build table, so its signature is checked always.

**IDA.** The battle's generator class (vtable rva 0x2C4F078, `sfv::rng::kVtable`) draws in its
slot 3 (+24), rva 0x32B2B0, 19 bytes: `state = state * 1664525 + 1013904223` at +8, returning
the state rotated by 16. That vtable slot is its only reference. Slot 4 (rva 0x32B280, a float)
and slot 5 (rva 0x32A830, a coin) call slot 3. New code site `sfv::kRandomDraw` with the first
16 bytes `69 41 08 0D 66 19 00 05 5F F3 6E 3C 89 41 08 C1`.

**What it does.** While a bound battle's update runs (`SfvAdapter::RunUpdate` marks it), each
draw's generator is placed against the layout (`rng_audit::Ranges`, sorted region ranges): its
state dword inside a region (and reseeded by `ReseedRandom` only if its vtable word is in the
same region, 8-aligned), the global generator, or outside. Outside, or inside but not
reseeded, is logged once per generator per battle with the return rva of the draw's caller;
each battle's end logs the counts:
`rng audit: the battle ends: N draw(s) in its updates, A from generators in the rollback state,
B from the global one, 0 from generators outside it and 0 from generators not reseeded`.
No draw is changed, and nothing reseeds per draw: the battle's task order across job workers
is not fixed (EMBER_LIBRARY.md section 56), so a draw index would not be deterministic.

**What it cannot see.** The LCG steps written inline on the global generator's state: rva
0x391C30 (called from the camera's and other handlers, rva 0x31B760, 0x33BE60 and others) and
the visual code's rva 0x3954F0, 0x3956A0, 0x395860, 0x395AD0, 0x395B20, 0x395D00 and 0x395EB0
all step `dword 0x3966540`, which is `sfv::rng::kGlobal + kState`, the layout's `global.rng`:
covered. Three more inline LCGs (rva 0x115B20, 0x118ED0, 0xAD38D0) step their own variables
outside the battle's code ranges; not traced.

Expected: none outside, as the review found (Rose's pick draws from the fighter pair). If one
appears: add its object as a run in `kPartRunsOutsideSnapshot` and reseed it in
`ReseedRandom`, which changes the layout id (re-export `data/pick-space.json`, rebuild the room
host).

`tests/RngAuditTests.cxx` (`sfve_ember_tests`): the placing (a fighter pair, a region holding
only a state, the global, outside, a state past a region's end) and the site in the table.

## 4. Checks (4.9)

### InputUnit +0x2178

IDA: the input unit's message handler (rva 0x438E40, a message with flag bit 1, at a round's
start) stores at rva 0x438EBD the dword at +6360 of the match data the battle's context holds
(rva 0x2F85B0 returns its owner's +272; that object's +6408 is the rounds judged,
`battledata::kRoundCount`), or -1 when the unit's +0x10C is set. +6360 lies inside the part of
the match data the game's snapshot holds (+6176..+6404), and +0x2178 inside our hashed
`inputunit.counters`. So it is not a per-machine global in our setup unless +6360 itself
differs, which the snapshot would show. Bind now logs, on every copy:
`adapter: input unit +0x2178 = V at bind (its +0x10C O; the battle data's +6360 S)`
(`sfv::inputunit::kRoundStartValue/Override/Source`). If two copies' lines ever differ, the
start state agreement repairs it before frame 0 and logs it.

### The pawn and view target animation clocks

SFV_PORT.md said the adapter restores "camera and pawn animation clocks" unhashed. **The pawn
and view target clocks were never in the layout**; the doc was out of date (the camera's are:
its runs are the game's own snapshot, restored and unhashed by `kRunRules`).

IDA: the pawn and the view target are Unreal actors reached only through the engine:
KiwiGame's Blueprint-callable `GetBattlePawn` (native rva 0xD137A0, through rva 0x5203A0: the
player controller's pawn, checked against a KiwiGame class) and `GetBattleViewTarget` (rva
0xD13D20, through rva 0x5205A0: the game mode's +1200). The battle's code reaches the view
target only through rva 0x2F9E70, whose callers are the camera's functions (rva 0x318FB0,
0x319460, 0x3198D0, 0x319B70, 0x319BE0, 0x319CB0, 0x335610, 0x3364A0, 0x33A5A0): they write the
camera unit's view into it (its +792..+800, flags at +816) every update, from the restored
camera. The animation clocks are Blueprint variables, whose offsets the exe does not define
(no native code in the actor range rva 0xE00000..0xE80000 uses +0x9E0), so they cannot be
added under the "no guessed offsets" rule, and they are presentation either way: nothing of
the fight reads them, and the two-copy runs (section 47, the D and F runs) match with them
left alone. Not added; SFV_PORT.md and `StateRegions.hxx`'s comment corrected. No layout
change.

### The cost line

Not present after the socket-read work (that logs only the keys' cost every 1,800 updates).
Now every 600 updates played forward (`SfvAdapter::LogCosts`):

`costs: the last 600 updates: tick T us; replayed tick R us (n); save S us (n), load L us (n),
hash H us (n), the restore's callbacks C us (n)`

Tick and replayed tick are `Advance` (forward and after a rollback, including the socket
keys); save, load and hash are timed inside `StateLayout` (`SaveTo`, `LoadFrom`, `HashSaved`,
`HashLive`; `TakeCosts`), so they stay right whatever the session does with its saves (the
coming skip-saves and hash-on-confirm work changes `RollbackSession`'s callbacks, not these);
the callbacks are `AfterLoad`. Averages per call, counts in brackets.

## What the laptop run must check

Build the development build of this branch (with the other beta.8 branches as the integrator
merges them), then:

1. Unit tests: `sfve_tests` (StartAgreementTests, ReplayTests, RollbackSessionTests unchanged)
   and `sfve_ember_tests` (RngAuditTests, DesyncReportTests, BuildKindTests).
2. The audit, one copy (expect `PASS` and `0 from generators outside it`):
   ```
   powershell -File tools\synctest.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -moves -practice -frames 7000 -rngAudit -check
   powershell -File tools\synctest.ps1 -p1 Z39:0:1:1:1 -p2 Z29:0:112:2:2 -moveset supers -idle 1 -practice -frames 7000 -rngAudit -check
   powershell -File tools\synctest.ps1 -p1 Z33:0:112:1:1 -p2 Z37:3:0:1:1 -moveset projectiles -idle 1 -practice -frames 7000 -rngAudit -check
   ```
   The `costs:` lines give the breakdown (one every 600 updates).
3. The agreement, two copies; every copy must log `start: the same state after N ms`, the
   `adapter: input unit +0x2178` lines must be equal, and 0 checkpoints differ:
   ```
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 30 -games 2 -rngAudit
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 30 -games 2 -watchers 1
   ```
4. A repair: copy 1's start state perturbed; copy 1 must log `start: repaired ...` (the run
   fails otherwise), 0 checkpoints differ, and a start state repair report must be spooled
   (`%APPDATA%\SFVEmber\desync-reports`, Send problem reports on) with a frame-0 `.state`:
   ```
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 30 -games 1 -startPerturb "global.rng,0,1"
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 30 -games 1 -startPerturb "CharaUnit[1].CharaStatusPart+0x238,0,1"
   powershell -File tools\runroom.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moveset supers -idle 1 -practice -lag 30 -games 1 -res0 960x540 -res1 1920x1080 -startPerturb "global.rng,0,1"
   ```
   Then `findstr "start:" %APPDATA%\SFVEmber\logs\sfv_ember.log %APPDATA%\SFVEmber\logs\sfv_ember.1.log`.
5. A replay of one of those battles (Replays screen, Watch now): the log must say `replays: the
   playback starts from the recording's start state`.

## Not verified

- Nothing was compiled or run (no build on the desktop; the laptop was busy). MSVC /W4 was
  reviewed by eye only.
- That the helper's bridge carries the agreement's datagrams before GGPO binds (read from
  `bridge.rs`, not run) and that QUIC's datagram size admits 900 bytes on every path (the room
  admits `GgpoMaximumPacket`, 1,064, so it should).
- The audit's hook on a 19-byte function through MinHook (its first instruction, 7 bytes, is
  relocatable; not run).
- Whether `AfterLoad`'s post-restore callbacks behave at frame 0 exactly as after a load (they
  run only when a repair wrote something).
- What +6360 of the match data is; only that +0x2178 copies it.
- The three inline LCGs outside battle code (rva 0x115B20, 0x118ED0, 0xAD38D0).
