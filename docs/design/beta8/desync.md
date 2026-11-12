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
  read but the every-tick ones (`SocketProbe`: frame, replayed or not, side, socket, call
  site, position); `-sfve_tiny=MASK` draws sides small in a battle that is no room's or
  replay's (a synctest's); `-sfve_replay_any_build=1` plays another build's replay.
- `tools/synctest.ps1` and `tools/runroom.ps1`: `-moveset`, `-idle`, `-socketLog`; synctest
  also `-tiny` / `-tinySides`. `tools/stateregion.py` as above.

*Tests:* `GameInputTests` (every set's blocks follow each other with no gap, nothing presses up
but the full circles, the system moves keep their 96-frame blocks and coin, a block's forward
is the fighter's facing at its first frame and stays for the block, an idle player presses
nothing and the other walks forward first, a script read out of order plays the same frames),
`TinyFightersTests` (the game's own scale for the fight, a normal side untouched, no hooks no
change, the socket read's site in the build table, the every-tick call sites),
`BuildKindTests` (a player build ignores the new options), `SfvLoadoutTests` (0.6). Not built
yet: the maintainer batches builds.

## Ranked hypotheses

1. **Projectiles placed at a socket on a replayed tick** (3360: high; section 48's Z33 and NCL
   synctest hits: same). The pose of the newest presented frame, not the replayed one. Fix
   candidates, for the maintainer to choose once the run below shows it: (a) record each
   fight-time socket read on the frame's first simulation and give the replay the recorded
   value (deterministic for a fighter's own inputs, not for a frame first simulated on guessed
   inputs); (b) place such spawns at the fighter's position plus a fixed offset (deterministic,
   but not the game's placement); (c) refresh the mesh's pose from the restored motion state
   before a replayed tick (exact, but it runs Unreal's animation on the battle's thread).
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
