# Beta.8: the hitbox viewer in SFV's Training

Ember draws both fighters' boxes, and their projectiles', over SFV's Training battle, as SF4
Ember's Training lab draws SF4's: green where a fighter can be hit, red where an attack hits,
blue where it throws, and an orange outline where an attack makes the opponent guard (its
proximity box). It reads the game after each tick and writes nothing. The switch is Training
select's Hitboxes row; the choice is kept in `training.json` as SF4's lab keeps its own.

Branches: `b8/hitboxes` (this repo) and `b8/lib-hitboxes` in the library (from 24ca041b9).
Every rva is build 13173604, read in the laptop's IDA database; the record layout was checked
against two of the maintainer's desync state files (below).

## Where SFV keeps the boxes

Every site is a `CodeSite` in `src/mod/SfvBoxOffsets.hxx` (included by `SfvOffsets.hxx`, so
`kSites` verifies them with the rest of the build table); the offsets are its `sfv::boxes` and
`sfv::camera` namespaces, with static_asserts that tie the strides to the sites' own bytes.

| What | Where | Evidence |
|---|---|---|
| Two box blocks per action part | `ActionPart+0xC0` is the action controller (its constructor rva 0x2EC7E0); the **attack block** at controller+304 = part **+0x1F0**, 5,360 bytes (constructor rva 0x2EBE60), the **body block** at controller+5664 = part **+0x16E0**, 6,712 bytes (constructor rva 0x2ED440). Both are inside the part's registered run (part+0xD8, 15,808 bytes), so a rollback restores them. | The script commands fill them: command type 5 through controller vtable slot 9 (rva 0x300BD0 -> add rva 0x309560) into +304, type 6 through slot 10 (rva 0x300FB0 -> rva 0x309730) into +5664. Chara Prepare (rva 0x37D9A0) places part+496 and part+5856 every tick. CharaActionPart and ProjectileActionPart (type 0x3A64D70) share the layout: the projectile clash (rva 0x47CAE0) reads a projectile part's +4632 and +9984, the same lists. |
| Records | Up to 32 records of 120 bytes from block+8; their count, a qword, at block+3848. | The adds index `a1[1 + 15k]` and count `a1[481]`; `8 + 32 * 120 = 3848`. |
| A record's fields | +48 the attack type (attack block) or the list bits (body block); +52 the body group; +88 the collision shape (its vtable, then x, y, w, h at the shape's +8: the record's **+96 left, +100 bottom, +104 width, +108 height**, world fight units); +0, +4, +12, +16 the script's own (local) x, y, w, h; +36 the hit id and +44 the hit limit, +112 the hits so far. | PlaceAttackBoxes (rva 0x3826D0) writes +96..+108 from +0..+16 with the position, scale and facing (`x = pos.x + s.x * x`, or `pos.x - s.x * (x + w)` mirrored); PlaceBodyBoxes (rva 0x3827B0) the same for the body block. The shape's constructor (rva 0x2ED880) stores vtable rva 0x2C4D6F8, whose slot 0 returns 3, the rectangle the overlap test (rva 0x4CCEC0) compares field by field. |
| The attack block's lists | Seven lists, one per type, at block+4000 + 136 * type: 16 record pointers and a qword count at +128. | ListAttackBox (vtable slot 9, rva 0x301310): `block + 136 * record+48 + 4000`, at most 16. Rebuilt every tick: slot 2 (rva 0x300090) zeroes the counts. |
| The body block's lists | Five groups of four lists from block+3992, 544 bytes a group; a record is in each list of its group (+52) whose bit (1, 2, 4, 8) its +48 sets. | ListBodyBox (slot 9, rva 0x301380): `544 * record+52 + 3992`, then the four bits. |

The viewer draws what the lists hold: the boxes the game tests on that tick, as SF4's viewer
draws the boxes its masks say are tested. A list entry that points at no record of its block is
left out and logged once a battle (`hitboxes: N list entries point at no record ...`).

### What each attack type is

From the attack detection (CharaManager AttackDetection, task 0x2030010, rva 0x3DDAB0 ->
rva 0x383760) and its order table (rva 0x2C51780, walked by rva 0x34E770 through rva
0x344A00: (body list 2, type 2), (1, 1), (2, 1), (0, 0), (2, 0), (0, 6), (0, 4)):

| Type | Drawn as | Evidence |
|---|---|---|
| 0 | hit (red) | strikes the body boxes in list 0 (rva 0x34A500 too) |
| 1 | hit (red) | strikes list 1 and list 2; also meets the other side's type 1 (rva 0x34DBD0, 0x34DF60: the projectile clash). The recorded fireball's 0.4 m box is type 1. |
| 2 | hit (red) | tested first, against list 2; never seen in the recorded states |
| 3 | not drawn | never found tested |
| 4 | proximity (orange outline) | tested against list 0 without using up a hit (rva 0x300020 skips types 4 and 6); in both recorded states a type 4 box reaches 2 m ahead of a projectile (a fireball in the air, Cody's rock on the floor), the shape of a proximity guard box |
| 5 | not drawn | met against the opponent's type 0 boxes with a priority (rva 0x34ED80): a box that catches attacks |
| 6 | throw (blue) | tested against list 0 like a throw (order table), and against a projectile's group 4 (rva 0x34E3E0) |

A listed type 3 or 5 is logged once a battle (`hitboxes: an attack box of type N is listed`).
Every body group is drawn as a hurt box (green): groups 1 to 3 are tried before group 0 (rva
0x34D1B0), and only group 0 was seen in the recorded states. SFV's view has no throwable
(yellow) boxes of its own and no skeleton.

## The camera and the picture

| What | Where | Evidence |
|---|---|---|
| Where the camera looks, and its eye | CameraUnit (type 0x3A648A0) **+0x170** and **+0x17C**, three floats each, in the engine's units | AimCamera (rva 0x33FE90, the camera unit's update, reached from its vtable rva 0x2C4F160 slots 83 and 84) takes both from the fight's space through FieldUnit's matrix and writes them as (x * 100, -z * 100, y * 100). The direction it turns into the rotation at +0x188 (rva 0x31A930) is target minus eye. |
| The field of view | CameraUnit **+0x194**, horizontal, degrees | AimCamera: `2 atan(tan(vertical / 2) * 1.7777778)` of the vertical one at +0x104. |
| The fight's frame in the world | FieldUnit (type 0x3A64B78, index 0) **+0x1C0**, 4x4, rows first; its inverse at +0x200 | AimCamera multiplies the camera by it before the units (`x m0 + y m4 + z m8 + m12`, ...) and reads the eye back through +0x200. |

`ember/SfvBoxes` folds all of it into the library's `BoxView`: view = fight frame x engine
units x the camera's look (right = up x ahead in the engine's left-handed axes), projection =
the horizontal field of view across the width and the game's 16:9 down the height, the depth
the distance ahead (what `training::Project` divides by). The camera is read after each tick,
with the boxes, on the game thread; nothing of it is called.

## Evidence: two recorded states

`tests/SfvBoxesTests.cxx` rebuilds the blocks of the maintainer's state at checkpoint 3360 of
2026-11-11 (Z33 against Z37, the six-desync night of `desync.md`) record by record, at their
addresses, and checks the decoder and the camera against them:

- Every listed record in both state files (3360 and 12780) has the rectangle's vtable
  (image + 0x2C4D6F8), and its world rectangle equals its local one placed at the fighter (P1
  at 3360: local -0.35 + position -7.02 = -7.372; at 12780 a mirrored P2 at 1.34 has its
  local -0.35..0.35 box at 0.99..1.69).
- At 3360 each fighter lists four body boxes (head, legs, body in lists 0 and 1, +48 = 3; a
  tall 0.5 x 1.5 m one in list 2, +48 = 4); the viewer draws each once.
- The camera at 3360 looks at (-4.86, 1.48) on the fight's plane from 4.75 m in front of it
  with a 65.0 degree field of view (39.43 vertical). Projected at 1920x1080: the target at the
  screen's middle, both fighters' feet at y 1016 (under SFV's gauges, as `DESIGN.md` says the HUD
  covers the feet), P1's at x 150 and P2's right edge at x 1879, P1's head at y 423. The state at
  12780 is inside Cody's Critical Art, whose cinematic camera puts the feet below the screen.
- No FieldUnit is in a state file; both cameras' targets lie on the fight's plane (z 0), as an
  identity frame gives. The run logs the frame (below).

## What changed

**Library** (`b8/lib-hitboxes`):
- `ui/HitboxOverlay` (new, neutral): `HitboxShapesOf` (pure: the boxes a view and the parts
  give on a screen) and `DrawHitboxOverlay` (ImGui's background list). SF4's
  `DrawTrainingBoxes` (TrainingPanel) now calls it with its Hitboxes page's parts; its colours,
  kinds, skeleton and order are unchanged (the HitboxOverlay test compares it with the drawing
  as it was, call for call).
- `training::BoxKind::Proximity` (an orange outline, no fill).
- `Capabilities::hitboxes` (off; SF4 leaves it off, its lab has the switch): Training select's
  Hitboxes row (`ShellView::offersHitboxes`, value from the game's `hitboxesShown`), Select
  turns it over, Left/Right set it; `ShellAction::showHitboxes` goes to
  `ShellGame::ShowHitboxes` (default: not kept), nothing to the runtime or the preferences.
- `training_select.hitboxes_detail` in all 14 catalogs; the row's label is SF4's
  `training.hitboxes`. The Chinese text's one new character (橙) is in GB2312 level 1, which the
  subset font already holds.

**SFV** (`b8/hitboxes`):
- `mod/SfvBoxOffsets.hxx` (sites and offsets above); `mod/FighterReader`: Bind also finds
  each projectile's action part, the camera and FieldUnit; `ReadBoxes` copies both blocks of
  both fighters and of every projectile out, and the camera.
- `ember/SfvBoxes` (pure conversion and projection), `ember/SfvHitboxes` (reads after each
  tick of the battle the frame meter follows, only while on; publishes the view; draws it;
  keeps the choice in `%APPDATA%\SFVEmber\training.json`, `"meter": {"hitboxes": true}`,
  written whole by the library's `MeterOptionsJson`).
- `SfvFrameMeter` calls the reader after its own (one battle watcher); `SfvHud` draws the boxes
  behind every window, never over the game's pause menu; `SfvShell` fills the row and keeps
  the choice; `SfvGame` sets `caps.hitboxes`.

The log: `hitboxes: on|off (training.json)` at the first ask, `hitboxes: on|off, saved`, and per
battle `hitboxes: first tick: P1 n boxes, P2 n, k projectiles out; camera used: eye (...),
target (...), field of view f; fight frame rows (...)`, then only what is unexpected.

## Tests

`SfvBoxesTests` (types to kinds; the recorded blocks: 11 boxes, each body record once, the
fireball's hit and proximity boxes and the throw; the recorded camera's screen positions; the
fight's frame moving the view with it; cameras that cannot be one; stray entries, other
shapes, empty rectangles, undrawn types, a list's count past 16), `EmberAdapterTests`
(`caps.hitboxes`). Library: `HitboxOverlay` (SF4's drawing unchanged), the shell journey
`TrainingSelectHitboxes`, `RuntimeCapabilities` (`TestHitboxesCapability`). Nothing was built:
the SFV build must use the library at `b8/lib-hitboxes` (it needs `ui/HitboxOverlay` and the
new `Capabilities`/`ShellView` fields).

## Not verified

- The type meanings marked so above: 2 (never seen), 3 and 5 (not drawn), and 4 as the
  proximity box (its shape and its test say so; no run has shown it with a guard).
- Body groups 1 to 3, and whether SFV ever lists a group 4 (throwable) box on a fighter.
- FieldUnit's matrix in game (expected the identity or a translation; logged once a battle).
- The picture at an aspect other than 16:9: the projection assumes the game's 16:9 picture
  fills the window.
- Cinematic cameras (Critical Arts, V-Trigger cut-ins): the boxes follow CameraUnit's camera;
  if a cinematic draws through another, they will be misplaced for its length.
- A shell save of `training.json` on the render thread (as SF4's lab saves its own).
- Mid-Training: the row is on Training select; to change the choice, leave Training.

## What a Training screenshot run must show

On the laptop, a development build, 1920x1080, RYU against RYU in Training (and one run at
960x540), with Hitboxes turned on in Training select first:

1. **Standing:** each fighter's green boxes (head, body, legs and the tall middle one) over its
   own model, feet on the floor line, P1 left, P2 right; nothing red.
2. **st.HP (startup, active, recovery):** a red box in front of RYU only on its active frames,
   reaching where the fist is; a P2 hit when it overlaps P2's green boxes.
3. **Hadoken:** a red box on the fireball and an orange outline ahead of it; P2 holding back
   blocks while the outline overlaps it (the proximity box shown).
4. **Throw (LP+LK):** a blue box in front of RYU on the throw's active frames.
5. **Walk to the corner and back:** the boxes stay on the models while the camera pans; a
   jump: the boxes rise with the fighter.
6. **Pause menu:** no boxes over it. **Ember's menu open:** the boxes behind it.
7. **Turn it off in Training select, start Training:** no boxes; the log says `hitboxes: off,
   saved`, and `training.json` holds `"hitboxes": false`.
8. **The log:** `hitboxes: first tick: P1 4 boxes, P2 4 ...`, `camera used`, the fight frame
   rows (identity or a translation), no `point at no record`, no `not rectangles`; any `type 3`
   or `type 5` line names a move to look at.
9. **A Critical Art:** whether the boxes leave the models during its cinematic (the item
   above).
