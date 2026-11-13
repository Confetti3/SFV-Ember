# Beta.8 stream F: the Z29 (Cody) desyncs of 2026-11-11

Six matches between the maintainer and one friend on v0.1.0-beta.7 ended "the two games
diverged". Paired by checkpoint from both players' logs (named regions only):

| Checkpoint | P1 vs P2 | Differing regions |
|---|---|---|
| 1800 | Z42 (drawn small) vs Z29 (V-Trigger II) | `ProjectileUnit[23].ProjectileActionPart->0x3ED0+0x30` |
| 12780 | Z29 (colour 112, V-Skill II, V-Trigger II) vs KEN | `CharaUnit[0].CharaActionPart+0xD8` |
| 8940 | Z29 (same look) vs Z37 | `CharaUnit[0].CharaActionPart+0xD8` |
| 3360 | Z33 (colour 112) vs Z37 | `ProjectileUnit[23]+0x1D0` |
| 10020, 19320 | ZGF vs Z29, NSH vs Z37 | 20 and 29 regions (diverged earlier) |

Only the maintainer's `.state` files exist so far (copies, with the six replays, the desync
reports and her log, in `D:\sfv-re\b8-desync\`). Every address below is an rva of build
13173604 read in the laptop's IDA database.

## What her state files show

`tools/stateregion.py` (new) dumps one region of a state file with its fields named where the
code was read, pairs a region with the other player's file (`--against`), and `--fighters`
says what each fighter and live projectile plays (the script player's script number and frame,
the side, the facing).

- **12780 and 8940: inside Cody's Critical Art.** Cody (side 0) plays script 1200 at frame 200
  and 190. Its header: 388 frames, attack frames 181 to 194, interrupt 266. The replays show
  the motion: qcf, qcf + LP from frame 12571 (`D`, `D+R`, `R`, `LP`, twice; the fight starts at
  12407) and from 8746 (mirrored: he faces left). His opponent is at script 212, frame 0, in
  both. In both, a projectile of Cody's (`ProjectileUnit[22]`, side 0) is alive in its script
  50 (frames 14 and 8) on the floor (y 0). So the checkpoint lands 6 to 16 frames after the
  Critical Art's attack window, with a projectile it spawned. Cody's input is a steady
  crouching block for the 120 frames before the checkpoint; the opponent's inputs (KEN's
  presses at 12778, Z37's mash) are what made the copies roll back.
- **3360.** `ProjectileUnit[23]` is P1's (Z33's; projectiles 16 to 23 are P1's and 40 to 47
  P2's, by ActionUnit +256), script 50 frame 30, at (-7.596, 1.053, **-0.159**, w 1). Every
  other live projectile in these files lies on the fight's plane (z 0) with w 0 or w 1 from its
  owner. A z off the plane with w 1 is what the socket read writes (below). Z33 turned on its
  V-Trigger at 2733.
- **1800.** `ProjectileUnit[23]` is P1's (Z42's), 3 frames old (script 1), at (-5.304, 1.2, 0,
  w 0): placed from the fighter, not from a socket. Every P2 projectile is dead. Cody (P2)
  pressed V-Skill I (MP+MK) 40 times in this round, the last at 1762.
- No word in any region looks like stack garbage or an unmasked address (a scan for qwords
  between 2^32 and 2^40 finds only pairs of small ints).

## IDA findings

### The action part's region (`CharaActionPart+0xD8`, 15808 bytes)

ActionPart's registration (rva 0x2FED10, then 0x2E8D30) registers its controller at part
+0xC0: controller +24 (8: the script player now playing, a pointer to the `->0x3EC8` or
`->0x3ED8` timeline object), +32 (32), +64 (160), +224 (80), +304 (5360: 120-byte records,
each opening with an image pointer), +5664 (6712), +12376 (2392), +14768 (1056), +15824 (4:
**the facing**: 1 when the fight mirrors x, rva 0x2FB820, 0x2F9310), +15828 (4); then +15856
(4) on its own (`+0x3EB0`). These merge into the one region at part +0xD8.

The timeline objects (`->0x3EC8/0x3ED0/0x3ED8 +0x30`; 272 bytes, constructor rva 0x2ECAD0)
are the script players the frame meter reads: +0x40 the script header, +0x48 the script
number, +0x4C its frame (16.16), +0x54 its end, +0x58/+0x5C the attack frames, +0x60 the
interrupt, +0x64 the time scale, +0x68 the speed, +0x6C the phase bits.

**Outside the snapshot** (the controller's constructor, rva 0x2EC7E0): controller +15832..
+15856 is a `std::vector` of pointers (reserved for 32) into the playing script's command
records, which rva 0x302A30 dispatches by command type (+8) on each update; and part +0x3EB4
(controller +15860, -1 at construction) is written every tick by Chara Prepare (rva 0x37DED6),
by the action part's reset (rva 0x330C10, with the qword at +0x3EAC) and by rva 0x4705C0, and
read by rva 0x2FBB50 and 0x36AC30. Whether either carries meaning across a frame boundary (as
Z39's +0x61C did, EMBER_LIBRARY.md section 48) is not settled; the synctest below decides.

### Cody's own part: saved and hashed

Cody's character part is `CharaUniquePart_032` (type 0x3A68020, name set by rva 0x141A00), 264
bytes (factory rva 0x3AC0B0, constructor rva 0x3A7F80), vtable rva 0x2C5C3C0. Its
registration (secondary vtable slot 8, rva 0x3AFAB0) takes +232 (the item: 0 or 1), +236 (its
state: -1 none, 0 held, 1 and 2 in use), +244 (a release asked for) and +248 (its argument):
the runs the log names `+0xE8/8` and `+0xF4/8`, hashed as `game.N`.

The three words it leaves out are scratch within one tick, not state:
- +240 (the item to take) and +252 (drop everything) are set by the move scripts' commands
  (slot 35, rva 0x3B8FD0; slot 36, rva 0x3B7AC0 from the action part's update rva 0x31EDF0) and
  read by the part's tick (slot 2, rva 0x3BD6E0). Both are cleared every tick by slot 12 (rva
  0x3BED50), which the status update (rva 0x362850) calls from **Chara Prepare** (task
  0x2020020, rva 0x37D9A0). The commands run in Chara PreMain (0x204xxxx), and the tick runs
  in **CharaManager SyncStatus** (task 0x2050000, rva 0x3DDFE0 then 0x384550). So they are
  set and read within one tick and cleared before the next one reads them.
- +256 is "no V-Trigger running" (rva 0x377050: the status part's +260 > 0), recomputed at
  the start of the same tick that reads it (rva 0x3C2C20).

The item drop (state 2, rva 0x3C2C90) spawns projectile 200 or 201 (item 0 or 1) through the
spawn-request fill (rva 0x37F550, mode 1) at the fighter's position (CharaUnit +1968, through
its virtual +8) plus the vector at CharaUnit +0xDA0 (constructor default, rva 0x369E96; read
by rva 0x373B20) times the facing scale: no pose read. Slot 3 (rva 0x3BB4A0) only hangs the
item on the visual part.

`CharaItemPart` (rva 0x353F40) registers +192 (12, copied from the fighter's +664 and +672 at
registration), +204 (4) and +208 (8). Every fighter has one, nothing Cody-specific was found in
it, and it did not differ in any of the six checkpoints.

### The fight reads the model: the socket read

`SocketWorldPosition` (rva 0x3919D0; `sfv::kSocketWorldPosition`): the CharaVisualPart's actor
(+0xA90), its mesh component (actor +0x3A8), the mesh's `GetSocketTransform` in world space
(its virtual +856, through rva 0xE58F10), turned into fight units (x/100, z/100, -y/100, w 1).
The socket names come from the table rva 0x351250 reads. Its callers:
- every tick: Chara PreInput's bone 28 (rva 0x37D250, masked), the camera's handlers (rva
  0x31AF60, 0x32F790, 0x33C1C0), rva 0x3E3E60;
- spawns placed at a bone: any script's spawn whose record names a bone (record +0x88,
  resolved by rva 0x4CFB70 through rva 0x32BAA0; reached from the fighter's code rva 0x36AC30
  and the projectiles' rva 0x46B680, 0x474A10, 0x475BB0, 0x48D0A0 through rva 0x4D9020), and
  three characters' own parts: Z33's `CharaUniquePart_036` drops projectile 200 at socket 64
  when its V-Trigger ends (slot 2, rva 0x3BD900; spawn mode -1: the socket's position as is),
  and two more (rva 0x3BBEE0, 0x3BDD20).

It reads the pose of the last presented frame. A rollback's replayed ticks run with no
presentation in between, so a spawn on a replayed tick takes the pose of the newest frame
played, not of the replayed one; the game guards its own bone 28 read after a restore (rva
0x37B670) and nothing guards these. And the mesh's world scale is in the transform, so a
small fighter's sockets moved toward the mesh's origin: the size feature reached fight state.

### Cody's Critical Art

Script 1200, 388 frames, attack 181 to 194. Its cinematic sets the actor's whole-mesh
transform (rva 0xE5BD90) and moves the camera (presentation, unhashed). Effects and
objects it places at a bone go through rva 0x4D9020 and 0x4CFB70 as above. Which of its
values the differing action-part words are needs the other player's file or a reproduction.

### The Bugsink crash on beta.7 (`+0x11BBF35`, read of 0x2)

- rva 0x11BBE30 is the engine allocator's Free (called as `GMalloc` vtable +0x28; a binned
  allocator: a hash of pools at +265192, page size at +265208, a critical section per pool).
  `rbx` is the pool found for the freed address; it is null when no pool holds that address,
  and `movzx eax, word ptr [rbx+2]` at +0x105 faults. So something freed an address the
  allocator does not own: freed twice, or never its own.
- rva 0x1143DF0 is a virtual (vtable rva 0x306CC70) that takes a locked list (rva 0x112EEB0,
  under the critical section at +144), processes it (rva 0x1143CA0) and frees the list's
  buffer through `GMalloc` (the call at +0x71). It runs on a worker thread.
- Ember: its allocator hook zeroes exactly the size the game asked for (`h_GameAlloc`), and
  its flow record frees and rebuilds delegates through the game's own allocator
  (`FlowRollback::ApplyDelegate`). Nothing of Ember's touches that list or runs on that thread,
  so static evidence does not tie the crash to Ember. The dump's whole stack (which object
  owns vtable 0x306CC70) decides; fetch it from `vps:/srv/ember-drop/dumps/`.

## What changed in this branch

- **The fight never sees a small model.** `SfvAdapter::RunUpdate` brackets each battle update
  with `BeginFightUpdate` / `EndFightUpdate` (TinyFighters): a small side's mesh is given the
  game's own scale before the update, neither picture hook scales during it, and it is made
  small again after it, before anything is drawn. Nothing happens when no side is small.
  Socket spawns of a small side now land where they land at normal size.
- **The match version is 0.6** (`picks::kMatchBuild`), so beta.8 never meets beta.7 or older
  (0.5 read sockets of the small model; and beta.5 met beta.6 with the same id, Bugsink).
  `data/pick-space.json`'s build line is changed by hand to the new version (its fingerprint
  does not include the version and the layout rules did not change: the layout id stays
  `72200b5db368767a`). Re-export it on the laptop and check the file is unchanged, then
  rebuild the room host (`upgrade.sh rooms`). Beta.7 replays do not play on beta.8 except with
  `-sfve_replay_any_build=1` (development).
- **Scripted moves that reproduce it** (`input::MoveSet`, `-sfve_moveset`): `supers`
  (Critical Arts that play out, alone and after a V-Trigger, 480-frame blocks), `cody`
  (V-Skill and its punches, V-Skill twice and held, the V-Trigger with two more presses, its
  specials and heavy normals, a walk over what it threw, the V-Trigger left to run out with
  its drop, and the Critical Art qcf qcf + LP or + HP), `projectiles` (quarter circles and
  charges with punches, EX, dp, V-Skill tap and hold, V-Trigger presses and run-outs, Critical
  Art). Every block of moves faces the way its fighter does (the action part's +0x3E90, read
  through `SfvAdapter::FacingRight` at the block's first frame; the block's coin when it cannot
  be read). `-sfve_idle=0|1`: that player stands still and the other walks forward 40 frames
  at the start of each block, so its moves land. The system moves play what they played before.
- **Diagnostics** (development builds only): `-sfve_socket_log=1` logs every fight-time socket
  read (`SocketReads`, below: frame, replayed or not, side, socket, call site, position, and
  whether it was recorded, given the straight read, or missed); `-sfve_tiny=MASK` draws sides
  small in a battle that is no room's or replay's (a synctest's); `-sfve_replay_any_build=1`
  plays another build's replay.
- `tools/synctest.ps1` and `tools/runroom.ps1`: `-moveset`, `-idle`, `-socketLog`; synctest
  also `-tiny` / `-tinySides`. `tools/stateregion.py` as above.

*Tests:* `GameInputTests` (every set's blocks follow each other with no gap, nothing presses up
but the full circles, the system moves keep their 96-frame blocks and coin, a block's forward
is the fighter's facing at its first frame and stays for the block, an idle player presses
nothing and the other walks forward first, a script read out of order plays the same frames),
`TinyFightersTests` (the game's own scale for the fight, a normal side untouched, no hooks no
change), `SocketReadsTests` and `StateRegionsTests` (below),
`BuildKindTests` (a player build ignores the new options), `SfvLoadoutTests` (0.6). Not built
yet: the maintainer batches builds.

## The laptop runs of 2026-11-12, and the socket fix

**Runs** (`D:\sf4\interactive\sfv-b8-runs.log` on the laptop, sections D1 to D7): Cody's
synctests with the system moves, `-moveset supers -idle 1`, `-moveset cody -idle 1` and supers
with P1 drawn small: clean (0 failures, 1296 rollbacks, about 7,200 frames each). Two copies,
Cody supers, 30 ms lag, 960x540 and 1920x1080 (D7): PASS. Z37 against Z33 with projectiles
(D6): clean. **Z33 against Z37 with projectiles (D5): 2 failures, both `ProjectileUnit[23]+0x1D0`**
(frame 5265: the position and its copy at +0x20, x y z played -0.2023 1.0999 -0.2170, replayed
-0.2235 1.0839 -0.2300: off the fight's plane, as a socket places it). The socket log shows the mechanism directly: the same frame's
read placed differently played and replayed (frame 487, `b_R_Foot` from rva 0x4CFC99: 2.772,
0.237, -0.057 played; 3.271, 0.211, -0.045 replayed; frame 769, `b_L_Foot`: 7.364 against 7.576).
The reads run on the battle's job threads. The log filled its 20,000 lines by frame 988 with
the head reads of Chara PostVisual (rva 0x38F401, 0x38F69B, 0x38F6B7, from rva 0x3897C0
through 0x3A53E0: after the tick, not the fight's), now left out.

**Where the pose comes from** (IDA). Each battle update runs the tick (phase 2), then phase 4,
then phase 8 (rva 0x4DC6A0); phase 8 holds "CharaVisualPart PostUpdate Main Thread" (rva
0x3A06C0, registered by rva 0x39BD50), which sets every layer of the fighter's actor (actor
+1056, count +1064) to a motion and time from the fight state (rva 0xE5F6E0: +2448 the motion,
+2452 the time). The engine then evaluates the pose in the mesh's component tick (virtual
+0x7D0; SFV's layer class, rva 0xE62E70, hands the motion and time to the `KiwiChara` anim
instance, +896 and +904), and `RefreshBoneTransforms` (virtual +0xC80: rva 0x1C0B890, SFV's
override rva 0xE5CC90; with no tick function it evaluates at once, rva 0x1C09F00). The engine
calls the battle once a frame (`BattleDriver::OnBattleFrame`), so the first update after a
drawn frame reads the pose of exactly the state it starts from, on every copy; every update
after a restore, and every further update in the same engine frame, reads an older pose.

**Why not a refresh before each replayed tick.** It would have to run the mesh's component
tick between replayed updates. That tick advances a looping layer's time by a fixed 1/60 per
call (rva 0xE62E70, `+2452 += 0.016666668` when +2469 is set), sets material parameters
(`OpacityRate`, `StippleClipValue`) and is written for the engine's tick function (parallel
evaluation and its completion); extra calls during a rollback would leave the picture after
it different from the one without the fix, and make the pose depend on how many frames were
replayed. The first replayed tick would also need the visual task run again for the restored
state, which starts and stops the actor's effects (CharaVisualPart +0x6D8). So the faithful
value is taken from where it already exists: the straight read of the same state.

**The fix** (`mod/SocketReads`, always on; the hook on rva 0x3919D0 is a verified site).
- `SfvAdapter` knows when the models show the state the next update starts from: set at every
  engine frame (`SeeBattleFrame`), cleared by every update and every restore (`AfterLoad`).
- At the start of every update, on the game thread, it hashes each fighter's regions (all its
  hashed runs and sub-objects, `CharaUnit[i]...`, masks and address zeroing as the state hash
  has them: `StateLayout::HashLiveRegions`), and publishes the two hashes and the freshness
  before the update's tasks start.
- A fight read (any caller but the ten that are not the fight's) on a fresh pose is recorded
  under (side, socket name, that fighter's state hash). A read on a stale pose with a recorded
  key gets the recorded position written over its output; without one it keeps the live value
  and is counted as missed. A fresh read that finds its key with another value is counted as
  conflicting (and keeps the newer): that would mean the fighter's own state does not decide
  its pose. 8,192 reads kept, the oldest dropped; cleared at each new battle.
- **Why it is deterministic.** A served value is what a straight play of that same fighter
  state reads, and a straight play reads the same on every copy (D7 and the sweeps). Every
  copy therefore gives a replayed read the value both copies' straight plays give it. A read
  is changed only on a stale pose; the update after a drawn frame with no restore is
  untouched, and nothing in the picture changes, so the pose after a rollback is what it was.
  What it does not cover: a state that never had a fresh update on this copy (a remote
  fighter's frames first simulated after a misprediction that changed that fighter); those
  keep the live read and show in the missed count. The key is the reading fighter's state
  alone, so a misprediction of the other player's input does not make the read miss.
- **Cost.** Two hashes of about 40 KB each per update (every update, replayed or not); the log
  says the average every 1,800 updates (`sockets: N updates keyed, X us per update to key both
  fighters; ...`, also when a battle ends), with the counts. A read itself costs a lock and a
  map lookup; they happen at spawns only.
- `tools/synctest.ps1 -check` ends with PASS or FAIL: the run reached `-frames`, 0 failures, no
  conflicting socket read.

*Tests:* `SocketReadsTests` (the site in the build table, the reads that are not the fight's,
the name key, a stale read takes the straight read of the same state, socket and side only,
conflicts, the capacity), `StateRegionsTests` (a hash of some regions follows those only).

## Two windows, one ulp: the fighters' LOD (2026-11-12, F5)

**What the runs showed.** With the record above, the synctests passed: F1 (Z33 against Z37,
projectiles) 7,282 frames, 0 failures (was 2), `18000 updates keyed, 50.3 us per update; 544
fresh recorded, 196 stale given the straight read, 129 stale of a state never read fresh, 0
conflicting`; F2 to F4 (Z37 against Z33, Cody's supers, small P1) passed too. Two copies on the
laptop, Z33 against Z37, 960x540 against 1920x1080, 30 ms lag (F5), diverged at checkpoint
10440: `ProjectileUnit[20]+0x1D0` (and the same values in its visual part) has y and z one ulp
apart (`3f86d881`/`3f86d880`, `be23388a`/`be23388b`), x equal. F5 ran without `-socketLog`,
so there is no line per read; its `sockets:` lines say copy 0 (960x540) served no stale read
at all (every read it made was fresh) and copy 1 (1920x1080) served 22 and missed 22 in some
11,000 updates. A stale pose is a frame or more of motion away (centimetres), not one ulp, so
the two copies' fresh reads themselves differed: the same state, the same PC (the same CPU and
code), only the window different.

**Why** (IDA). A socket's position comes from the bones the mesh evaluated, and the engine
evaluates only the bones the mesh's LOD needs: `RefreshBoneTransforms` (rva 0x1C0B890) rebuilds
its required bones from `PredictedLODLevel` (rva 0x1C0B3E0 with the component's +1384), and the
LOD update (rva 0x1C8B830, from rva 0x1C12100) sets `PredictedLODLevel` to `ForcedLodModel -
1` when `ForcedLodModel` (+1376) is above 0, otherwise to the renderer's LOD for the mesh's
size on screen (the mesh object's +24) plus the global LOD bias, raised to `MinLodModel`
(+1380) if that is set. The offsets are the ones `USkinnedMeshComponent`'s registration (rva
0x20F7A60) gives those properties. SFV never sets `ForcedLodModel` or `MinLodModel` on a
fighter's components (no write to +0x560 or +0x564 in its actor code, rva 0xE00000..0xE80000;
the writes at those offsets in the battle code are to other objects); it only sets
`MeshComponentUpdateFlag` (+0x5AC) on some (rva 0xE4E7C0, 0xE4FD90, 0xE50160, 0xE50420,
0xE51B60). So the LOD a fighter is evaluated at follows the window, and with another LOD
comes another set of required bones; the most likely way that moves a bone by an ulp is that
the animation's decompression and the bone composition run over another set of tracks
(packed for another bone count). Not yet observed: the `meshes:` lines below log each
copy's LOD, which settles it. No update-rate optimisation cvar exists in this build (`a.URO.*` is not
in the image); the meshes' `bEnableUpdateRateOptimizations` was not read (a bitfield whose
offset the registration computes at run time) and is the next suspect if LOD 0 is not enough.

**The fix: one LOD for the fighters.** Each battle update, before the update and on the game
thread, every skeletal mesh component of both fighters' actors (the mesh at actor +0x3A8, the
second at +0x400, the layers at +0x420 with their count at +0x428, each checked as the game
checks one: the skeletal mesh cast flag of its class, rva 0xE58F10, and not the unusable flag,
rva 0xE5F7D0) gets `ForcedLodModel = 1`: LOD 0 whatever the window, the DPI or the frame rate
(`KeepFighterMeshesAtLodZero`, `socket_reads::ForceLodZero`; a checked write, `TryWrite`). The
engine's next LOD update takes it, so the same bones are evaluated by the same code on every
copy. The first time a battle finds a fighter's components, it logs what the engine had picked
(`meshes: fighter K: N skeletal mesh component(s): mesh LOD 1 (forced 0, min 0, update flag
1), now LOD 0; ...`): the two copies' lines at 960x540 and 1920x1080 are the cause's evidence.
If something sets the value back, it is written again and logged (a few lines). The picture:
both fighters drawn at full detail in a small window too (a little more GPU work there).

**Why not round the read instead.** Rounding the position (to 1e-4, say) makes two values a
few ulps apart equal only when no rounding boundary lies between them; when one does, they
round apart and the copies differ as before. With some ten thousand reads a match and values
spread over the stage, that happens: rounding lowers the odds, it does not remove them. The
same goes for comparing with a tolerance. LOD 0 removes the cause: equal inputs, equal code
path, equal bits.

**The record still has a job, and its misses.** LOD 0 makes the fresh pose the same on every
copy; it does not make a stale pose fresh, so the record-and-serve stays for replayed frames
and the frame after a rollback. A miss (the reading fighter's state never had a fresh update
on this copy) keeps the stale live read, and two copies can differ there in real play: the
stale pose is the newest drawn state, which differs with each copy's rollbacks. In the
synctest the misses are the perturbed (wrong-input) replays, which are thrown away, so they
cost nothing there; between two PCs they are the reads of a fighter whose own state a
misprediction changed (F5's copy 1: 22 in some 11,000 updates). Closing them exactly needs the
pose evaluated for the replayed state, which the component tick cannot give safely (above);
the count in the `sockets:` lines is the measure of the risk left.

## Beta.9: the pose record keyed by what decides the pose

**Why.** Beta.8 keyed a recorded read by a hash of all the reading fighter's regions, command
history included. On the copy that guessed the other player's input wrong, every read of a
corrected frame then missed, whatever the pose: the 2026-11-13 matches missed far more than the
lab runs (the maintainer's process counted 662 misses against 532 served by the end of its
two Akira mirror battles, 26 and 44 of them in those; laptop runs 14 to 28 a run). It also
keyed a partner unit's reads (Akira's CharaUnit[2] and [3]) by its owner's state and side, so a
partner's socket shared its owner's key.

**What decides the value** (IDA, build 13173604). The read (rva 0x3919D0) builds the socket's
FName (rva 0x11AC010), asks rva 0xE58F10 for the socket's world transform (the actor's mesh,
its virtual +0x358 with space 0) and writes the translation as x * 0.01f, z * 0.01f, y *
-0.01f, 1. The transform is the pose (the bones the engine last evaluated) times the mesh's own
world transform. The pose comes from the two layers the visual task hands a motion and a time
(rva 0x3A06C0 -> 0xE5F7D0 -> 0xE5F6E0, from the visual part's layer block +2144..+2400), and
that block from the fighter's CharaMotionPart (rva 0x33FB00 -> 0x3A40D0, 0x3A4210). The mesh's
place comes from the actor's position (the visual part's +2400, through the actor's virtual
+1744) and, in a cinematic, the whole-mesh transform (rva 0xE5BD90: SetWorldScale3D and
SetWorldLocationAndRotation on the mesh). The layer block and +2400 are outside the snapshot;
the motion part, the facing and the CharaUnit's position runs are in it.

**The keys** (`SocketReads.hxx`, computed by `SfvAdapter::KeyFighterUnits` before each update,
on the game thread):
- the unit: its place among the battle's CharaUnits (partners included);
- the pose key: a raw XXH3 of the unit's CharaMotionPart runs (+0xC0/13092, +0x33E8/192),
  its facing (action part +0x3E90) and its side's Tiny pick. Raw: the key is compared within
  one process only, where a motion asset's pointer names that asset;
- the root key, kept per reading: the CharaUnit's +0xF8/40 run (with the hitstop) and its
  +0x764/300 run without the bone 28 words [88, 112).
All of it is restored with the state, so the first update after a restore keys the restored
state, not the models' leftover one. Hashing about 13.6 KB per unit costs a few microseconds,
against 28 to 30 us for beta.8's two 40 KB masked hashes (the 2026-11-13 `sockets:` lines).

**Serving a stale read.**
1. A reading of the same unit, socket, pose and root: its value as written (exact, the
   straight play's).
2. A reading of the same pose at another root whose mesh had the same rotation and scale
   bits: rebuilt at the current mesh translation, only when certain. The engine adds the
   mesh's translation last (UE4's `FTransform` multiply: rotate, then `VectorAdd` of the
   translation), so on each axis the socket is fl(r + T) with r the same for both readings.
   `CertifiedSum` finds the least and greatest float r with fl(r + T_recorded) ==
   W_recorded (rounding is monotonic, so a binary search over the floats' order), adds each
   to T_now in single precision, and serves the result only when both give the same float:
   then every possible r does, and that is the value the other copy's fresh read computes.
   It holds in the same binade or into a coarser one (except when the run of r ends on a tie
   that rounds the other way), and never into a finer one (a mesh near x = 0). Only while
   the models stand where the update's state put them (`SocketUpdate::placed`: no restore
   since the last update, whose visual task placed them), as the mesh transform is read live.
   The mesh's transform is read with `NAME_None` through rva 0xE58F10 (UE4's
   `GetSocketTransform` starts from the component's own transform; the virtual itself was not
   found), and the socket's again with its name: both are kept only when they give exactly
   what the game's read wrote.
3. Otherwise the read keeps the live value and is counted: "this pose never read fresh", or
   "read fresh at other roots only" with why (the models stand where a restored state left
   them, the transforms could not be read, rebuilding is off, no certain rebuild).

**What a miss does.** No value both copies compute alike exists for a pose this copy never
evaluated: the copy that guessed right reads the engine's evaluation, and the nearest recorded
time of the same motion, or a reference pose, is another pose. So the live value stays, as in
beta.8; the counts measure it.

**Checks.** A fresh read whose pose and root are kept with another value is conflicting (the
keys miss something: a looping layer's time runs on the engine's clock, rva 0xE5F6E0 and
0xE62E70, if a spawn ever reads one). A fresh read for which a certain rebuild from another
root gives another value turns rebuilding off for the battle and counts as conflicting too, as
do two readings whose certain rebuilds disagree. `tools/synctest.ps1 -check` fails on any.

**Bounds and counts.** 8,192 poses (unit, socket, pose key), as before, each with up to four
roots, the oldest replaced; a battle's spawns at a bone use far fewer (the 2026-11-13 battles
kept 500 to 3,400 entries under beta.8's finer keys). The counts are the battle's (they were the
process's): logged every 1,800 updates and as the battle ends, then cleared.

**Partners.** Every CharaUnit the battle has (up to `sfv::unit::kMostCharaUnits`, 8) is kept at
LOD 0 and keyed on its own; beta.8 did neither for Akira's partners, so their socket reads
followed each copy's window size. Only Akira (Z41) was seen with partner units (the
maintainer's logs since 2026-11-09: no other battle had a CharaUnit[2]); RegionNames already
named their regions `CharaUnit[2]...` and `[3]` (action, motion, visual and material parts and
the unit's runs; no status or command part). The state the snapshot leaves out of a part
(`sfv::kPartRunsOutsideSnapshot`) is now added for every fighter unit, not the first two
(`OutsideState::fighterParts`, 8 units): Akira's partners have neither part it names today, so
nothing is added in practice, but a helper unit with a status part would no longer be left
out. The made-up layout `LayoutRulesId` hashes has 8 units' rows now, so **the layout id
changes and `data/pick-space.json` must be re-exported**.

**The partners' projectiles were not named** (2026-11-13 Akira mirror, state
`1794589647-24364-1380`, and the bind's `regions:` lines: 341 unowned against 261 in other
battles). `RegionNames` looked units up to index 63; the partners' projectile groups add units
past it, whose 8 action parts (15808), 8 timelines (224), 8 unit runs (208) and 9 visual parts
(44 and 136 bytes) stayed `game.N`. So a projectile visual part's runs, presentation the rules
leave out of the hash because they drift on replay (its actor pointer and a frame counter,
`kRunRules` `ProjectileVisualPart` +0xC0 and +0xF0), were hashed whole for those units and
compared with the other copy: a candidate cause of the two Akira desyncs, which came right
after P1's V-Skill II (MP+MK, script 875). Units are now looked up until the battle has no
more (bound 256); naming changes no layout id, but what such a battle hashes changes, so it
rides the next match version too.

**Match version.** The values served change, so it rides the next match version (0.7).

## Beta.9: the action part's words outside the snapshot (IDA, 2026-11-13)

- **Part +0x3EB4** is a copy of the facing (+0x3E90, in the snapshot) that Chara Prepare takes
  every tick (rva 0x37DED6: `mov eax, [rbx+3E90h]; mov [rbx+3EB4h], eax`, on every path once
  the part exists), as does rva 0x4705C0; rva 0x2FBB50 (`== 1`) and 0x36AC30 read it later in
  the tick. It is the facing as the tick began, recomputed from restored state by the tick's
  first fighter task: nothing carries across a frame unless a reader runs before Chara Prepare
  in a tick (not checked). Not added to the snapshot.
- **The command vector at part +0x3E98** (controller +15832) is a queue of heap copies of
  script commands (56-byte objects, vtable rva 0x3231618) that rva 0x342AC0 pushes (from rva
  0x36AC30 through 0x35F590, from 0x34A500 through 0x35C010, and from 0x4843B0). The
  controller's virtual rva 0x30D530 runs them (rva 0x302A30 with mask 0x80000010) and frees
  them (rva 0x2F3330); the part's reset (rva 0x305500) frees what is left. Whether a push can
  come after that tick's run, leaving the queue full between two updates, was not settled.
  If it can, a restore keeps the abandoned timeline's commands and the replay runs them. Its
  bytes must never be saved and restored (they point to copies the run frees); the fix would
  be to empty it on a restore. `SfvAdapter` now logs every update that ends with a fighter
  unit's queue not empty (`actions:` lines, the first 8 a battle, and a count when the battle
  ends): a synctest or a two-copy run with Cody's or Akira's moves settles it.
- The command part's own fields past its registered runs (the input buffer and cancel-window
  area that target combos use) were not traced: its factory was not found in a light search.

## A pointer's high half in the hash (2026-11-12, S6b)

**What the run showed.** A plain room run (RYU against CNL, a watcher, 30 ms lag; the first
battle of the run) desynced at checkpoint 60 with no rollback, and the start agreement had
seen it at frame 0: P2's start state differed from P1's in one region,
`CharaUnit[1].CharaActionPart->0x3EC8+0x30`, with "4 differing left as they are (addresses,
masked words)" and the hash still apart after the repair. At 60, `diffstate.py` named the same
region alone: run +0x10 and +0x50 `a 0000013d00000000 b 0000000000000000`.

**The fields** (IDA: SetScript rva 0x307840, SetScriptFrame rva 0x3099E0; the timeline object
from +0x30). Run +0x10 is the object's +0x40, **the script header pointer** (into the loaded
script data, looked up by rva 0x2E9110), and run +0x50 (+0x80) holds the same pointer again.
+0x48 the script number, +0x4C the frame and +0x50 the frame before it (16.16), +0x54..+0x60
the end, the attack frames and the interrupt, +0x6C the phase bits; +0x70, +0x74 and +0x78 are
cleared by SetScript, +0x74 set to 1 (16 with its flag). The words at run +0x38..+0x48 that
changed between P2's frame 0 and frame 60 are those phase bits and flags moving with the
script; at 60 they equal P1's and the watcher's.

**The cause.** The three copies' pointers are their own heap addresses: P2 0x0000013D000042D0,
P1 0x00000217806F42D0, the watcher 0x000001E5AF0F42D0; every other word of the region is
equal. The hash zeroes addresses by testing 8-byte windows every 4 bytes and resuming after
each address it zeroes. P2's script data lies just past a 4 GB boundary (0x13D00000000), so
the pointer's low half is small, and the window 4 bytes before it, a zero dword then
0x000042D0, reads 0x000042D000000000: an address to the value test. Zeroing that window and
resuming 4 bytes later left the pointer's high half, 0x13D, in P2's hash; P1's window there
reads 0x806F42D000000000, no address, so its pointer was zeroed whole. Nothing of the fight
differed (the only other difference at 60, `inputunit.counters` +0xA4, is the sync clock the
peers' hash already leaves out), and nothing is stale from an earlier battle: it is the hash's
reading of one heap layout. A block placed at a 4 GB boundary gives every pointer into its
first 32 KB a small low half, so a process whose heap lays out that way meets it again and
again; the earlier action-part and timeline desyncs (section 56) are worth re-reading with
the new masking once the friend's files arrive.

**The fix** (`StateLayout::AddressAt`, used by the hash's scan and by `AddressUnits`, the start
agreement's view of addresses). Where a window 4 bytes off an 8-byte boundary of the real
address and the aligned window right after it both pass the value test, the layout asks a
second test, whether each is an address of this process's committed memory (the mod passes
`VirtualQuery` as `CommittedAddress`), and the mapped one is the address. Both or neither, or
no second test: the first window, as before, so a pointer packed 4 bytes off a boundary and
followed by a small value is still zeroed whole. The second test runs only on such overlaps,
which are rare. The scan now tests only windows that start in the chunk it is reading and
reads 11 bytes past the chunk (was 7), so the aligned window after any tested window is always
there. A real pointer is mapped in its own process and the bogus window (an address above
64 TB here) in none, so every copy now zeroes the same bytes. Zeroing the pointer at bind
instead is not possible: the fight reads the script header through it.

`tools/diffstate.py` and `tools/stateregion.py --against` mask the same way; for a file of
another process they stand in "within 64 GB of one of its regions, or in the game's image" for
the committed-memory test. With it, S6b's P2 and P1 states at 60 differ in no named fight
region. `tools/stateregion.py` now runs under `python -I`.

*Tests* (`StateRegionsTests`): the S6b pointers hash equal, and `AddressUnits` marks the
pointer's two dwords and not the window before it; without the second test the old reading
(the copies apart); a pointer packed 4 bytes off a boundary with a small value after it is
still zeroed when it is the mapped one, the value after it still counts, and with neither
mapped the first window is taken as before.

**To run:** the S6b room run again (`runroom.ps1 -rematches 1 -padAnswers -skipAfter 90 -lag 30
-watchers 1`, RYU against CNL): the start agreement reports the same start state on all three
copies, and every checkpoint compares equal.

**Superseded the same day (R2 and R3, from the between-round skip runs).** The committed-memory
test made the masking depend on each process's heap, and equal values then hashed apart.

- **The data.** An unowned 632-byte run (no known owner, the one section 53 saw) holds pairs of
  small ints: counters near 0x14A..0x213 next to pad-like bits. As 8-byte windows they pass the
  value test (`0x00000173000003E8`, `0x000001EE000003E8`), and at its 4-mod-8 positions both the
  window and its aligned neighbour pass.
- **R2** (`-games 2 -rounds 3 -lag 30 -skipAfter 90`): a fighter's copy, heap at 0x172F..0x1738.
  Once a counter read 0x173, its process found the aligned window "mapped" and zeroed another
  pair of dwords than the other copy (heap at 0x242..0x243).
- **R3** (`-autoSkip -watchers 1 -rematches 1 -padAnswers`): the watcher, heap at 0x1EE, on a
  counter at 0x1EE.
- **The effect.** "The two games diverged" at 3000 and 3480 with every byte of the two states
  equal. The only difference was the sync clock the peers' hash leaves out. `battledata.round1.stats`
  and the `game.N` lines in R2's `diff.txt` were the same masking and a name mispairing, as
  below.
- **The fix.** `AddressAt` decides by the values alone. When both windows pass, the aligned one is
  the address if the first window's low half is below 0x10000 (`kSmallLowHalf`). That is the zero
  or small value in front of an aligned address, as in S6b, R2 and R3. Otherwise the first window
  is the address, so a pointer packed 4 bytes off a boundary (its low half is rarely that small)
  is still zeroed whole.
- **Removed.** `SetAddressTest`'s second test and the mod's `CommittedAddress`.
- **Tools.** `diffstate.py` and `stateregion.py --against` mask by the same rule. `diffstate.py`
  now pairs a state file's unowned runs (`game.N`, a place in that process's region list, which
  differs between processes) by size and fewest differing bytes. With both, R2's two states at
  3000 differ only in `inputunit.counters` +0xA4.
- **Tests** (`StateRegionsTests`): the S6b pointers still hash equal with the aligned window
  marked; R2's counter table masks the aligned window and hashes alike over a copy at another
  address; the packed pointer with a small value after it keeps its own window.
- **Rerun:** R2 and R3, and the S6b run above.

**Superseded again (rule 2, `rollback-fixes.md`).** Only words on an 8-byte boundary of the
real address are tested, so no two windows overlap and `kSmallLowHalf` is gone; the S6b case,
its tail (a pointer after a dword of 0x10000 or more) and the counter table all mask by place.

## Ranked hypotheses

1. **Projectiles placed at a socket on a replayed tick: shown by D5, fixed above** (3360 and
   section 48's Z33 and NCL synctest hits).
2. **Cody's Critical Art leaves something outside the snapshot in his action part** (12780 and
   8940: medium). The controller's command list (+0x3E98) or part +0x3EB4, or a value the
   Critical Art takes from the model. The synctest below either reproduces it on one PC (a
   rollback defect, and the learn lines name the words) or does not (then two copies).
3. **1800 (medium-low):** Luke's 3-frame-old projectile's timeline; either it met Cody's rock
   placed at a socket (hypothesis 1), or the same action-part cause (2). The socket log says
   whether the rock is placed at a socket.
4. **Something of one PC only** (CPU, driver, the cinematic on the engine clock): only if every
   run below plays clean. Ruled out already: Cody's own part (saved and hashed), colour 112
   (looks are not fight state), different builds or looks (both beta.7, identical looks).

## The runs that settle it (laptop; each game launch with the maintainer's OK)

**After the LOD fix, re-run** (the two copies must PASS, with the `meshes:` lines of both
copies kept: they show the LOD each window had picked):
```
powershell -File tools\runroom.ps1 -p1 Z33:0:112:1:1 -p2 Z37:3:0:1:1 -moveset projectiles -idle 1 -practice -lag 30 -res0 960x540 -res1 1920x1080 -rounds 3 -seconds 99 -games 2 -socketLog
powershell -File tools\runroom.ps1 -p1 Z33:0:112:1:1 -p2 Z37:3:0:1:1 -moveset projectiles -idle 1 -practice -lag 30 -res0 960x540 -res1 2560x1440 -rounds 3 -seconds 99 -games 2 -socketLog
powershell -File tools\synctest.ps1 -p1 Z33:0:112:1:1 -p2 Z37:3:0:1:1 -moveset projectiles -idle 1 -practice -frames 7000 -check
```
Then `findstr "meshes:" %APPDATA%\SFVEmber\logs\sfv_ember.log %APPDATA%\SFVEmber\logs\sfv_ember.1.log`.
If the pair still differs with both copies' meshes at LOD 0, the next suspect is the meshes'
update-rate optimisation (above), then their update flags (logged in the same lines).

**After the socket fix, re-run** (each must PASS):
```
powershell -File tools\synctest.ps1 -p1 Z33:0:112:1:1 -p2 Z37:3:0:1:1 -moveset projectiles -idle 1 -practice -frames 7000 -socketLog -check
powershell -File tools\synctest.ps1 -p1 Z37:3:0:1:1 -p2 Z33:0:112:1:1 -moveset projectiles -idle 1 -practice -frames 7000 -check
powershell -File tools\synctest.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moveset supers -idle 1 -practice -frames 7000 -check
powershell -File tools\synctest.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moveset supers -idle 1 -practice -frames 7000 -tiny -tinySides 1 -check
powershell -File tools\runroom.ps1 -p1 Z33:0:112:1:1 -p2 Z37:3:0:1:1 -moveset projectiles -idle 1 -practice -lag 30 -res0 960x540 -res1 1920x1080 -rounds 3 -seconds 99 -games 2
```
In the first, the `socket:` lines of a replayed frame say "the straight read's taken" with the
stale value beside it, and the `sockets:` lines give the counts (conflicting 0) and the
microseconds per update. Then the 45-fighter sweep (step 6 below), with `-check`.

Build the development build of this branch. From the sfv-ember checkout:

1. Cody's Critical Art on one PC (hypothesis 2), P2 standing still so it connects:
   ```
   powershell -File tools\synctest.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moves -practice -frames 7000
   powershell -File tools\synctest.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moveset supers -idle 1 -practice -frames 7000 -socketLog
   powershell -File tools\synctest.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moveset cody -idle 1 -practice -frames 7000 -socketLog
   powershell -File tools\synctest.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moveset supers -idle 1 -practice -frames 7000 -tiny -tinySides 1
   ```
   A `learn:` line in `CharaUnit[0].CharaActionPart+0xD8` names the words that differ after a
   replay; `tools\stateregion.py` has their fields. The `moves:` lines say which move each
   block played and that its facing was the fighter's.
2. Z33's V-Trigger running out (hypothesis 1), and Z37's projectiles:
   ```
   powershell -File tools\synctest.ps1 -p1 Z33:0:112:1:1 -p2 Z37:3:0:1:1 -moveset projectiles -idle 1 -practice -frames 7000 -socketLog
   powershell -File tools\synctest.ps1 -p1 Z37:3:0:1:1 -p2 Z33:0:112:1:1 -moveset projectiles -idle 1 -practice -frames 7000 -socketLog
   ```
   Expect `socket: frame N, P1, socket ..., from rva 0x3BDB02` when the V-Trigger ends; the
   same frame logged again as `replayed` with another position is hypothesis 1 shown.
3. Two copies (cross-PC timing, the camera, the cinematic), at two window sizes, 30 ms lag:
   ```
   powershell -File tools\runroom.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moveset supers -idle 1 -practice -lag 30 -res0 960x540 -res1 1920x1080 -rounds 3 -seconds 99 -games 2 -socketLog
   powershell -File tools\runroom.ps1 -p1 Z29:0:112:2:2 -p2 KEN:3:0:1:2 -moves -practice -lag 30 -res0 960x540 -res1 1920x1080 -rounds 3 -seconds 99 -games 2
   ```
4. The six replays, for which copy diverged (section 56's procedure). Put them in the copy's
   replay folder, start it with `-sfve_room=1 -sfve_replay_regions=<checkpoint - 600>` and
   play each with the pipe's `replay watch <file>` and `dump <checkpoint>` during it; compare
   the dump with her `.state` (`tools\diffstate.py`), and the logged region hashes with her
   desync report. On the beta.7 build for an exact replay of all six; on this branch's
   development build with `-sfve_replay_any_build=1 -sfve_socket_log=1` for the socket reads
   (the four without a small side play the same fight on both builds; 1800 and 10020 do not,
   as the small side's model is no longer what the fight reads):
   `20261111-234736-05e36374` (1800), `20261111-234830-d940bbba` (10020),
   `20261112-005305-95e0fcb0` (12780), `20261112-010436-4320b95e` (8940),
   `20261112-010744-3dae6bbd` (3360), `20261112-010905-881bf5ae` (19320).
   Then `-sfve_replay_lag=1..7` and `-sfve_replay_jitter=SEED` around the frames.
5. When the friend's `.state` files arrive:
   `python tools\diffstate.py mine.state theirs.state`, then
   `python tools\stateregion.py mine.state "CharaUnit[0].CharaActionPart+0xD8" --against theirs.state`
   (and the two projectile regions). The differing words' fields settle hypotheses 2 and 3.
6. The 45-fighter sweep again with `-moveset supers -idle 1` and with the system moves; target
   45 of 45 clean.
