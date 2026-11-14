# beta.9: four socket reads taken out of the pose (socket anchors, 2026-11-13)

## Why

The fight reads a fighter's skeletal mesh (`sfv::kSocketWorldPosition`, `src/mod/SocketReads.hxx`) at a
number of call sites. The pose is evaluated once per drawn frame, so on frames replayed after a rollback
the two players' copies can read different values. Every call site was classified from the return rva the
hook sees. Four feed fight state: each one's value becomes the position of a projectile unit (script 50,
kind 200) or of the claw or mask object, and that position is kept in hashed state. All the others are drawn
only (effects, camera, interface). The record and the pose hold (`pose-hold.md`) try to make the pose itself
agree on both copies; for these four reads the fight is taken out of the pose instead: the hook gives a value
computed from fight state alone, which is the same on both copies by construction.

## The sites

| Return rva | What | Socket asked | Offset (forward, up) |
|---|---|---|---|
| 0x3BDB02 | CharaUniquePart_036 (Z33): item dropped when V-Trigger I ends | Sock_Item_R or Sock_Item_L | 0.20, 1.00 |
| 0x3BDF3B | CharaUniquePart_038 (Z35): item dropped when V-Trigger II ends | Sock_Item_L or Sock_Item_R | 0.20, 1.00 |
| 0x3BC294 | CharaUniquePart_015: a dropped prop | b_C_Head | 0.00, 1.60 |
| 0x3A4B79 | CharaUniquePart_008 (BLR): launch point of the claw or the mask when knocked off | claw: b_R_Hand, b_L_Hand; mask: b_C_Head | hand 0.30, 1.20; head 0.05, 1.60 |

At 0x3A4B79 any other socket name takes the head's offset and logs once a battle.

## The substitute

```
base   = the owning CharaUnit's fight position, 4 floats at CharaUnit +0x780 (sfv::fighter::kFightPosition)
dir    = -1 when the fighter faces left (CharaActionPart +0x3E90 == 1), else +1
out.x  = base.x + dir * forward
out.y  = base.y + up
out.z  = 0
out.w  = 1.0
```

The read's own output is x along the stage, y up, z depth (0 on the fight plane), w = 1, in fight units, world
space, already mirrored for facing; the substitute has that shape. The position and facing are read as the
update begins (`SfvAdapter::KeyFighterUnits`, into `UnitKeys`), from the game's restored and hashed state, not
in the middle of the update: a mid-update read could see the unit's movement task before or after it ran. The
substitute is not put on the 1/128 grid: it needs none, it comes from state alone.

The hook (`h_Socket`, `SocketReads.cxx`) applies it in a battle a session plays (`BattleLatch().Latched()`: the
same condition the grid uses; Training and the player's own battles keep the engine's read), when
`-sfve_socket_anchor` is on (default). The engine's read still runs first and its value is overwritten, so
whatever the original returns is left as it was. No recording, no serving, no miss and so no pose hold for
these four reads. When the unit, its position or its facing cannot be read, the read takes the record's path
as before, is counted (`anchor unresolved`), and one line is logged a battle.

## What is visibly different

An item let go as V-Trigger I or II ends, the dropped prop, and the claw or mask knocked off start from a fixed
point near the hand or the head (offsets above, from the fighter's fight position) instead of the bone's own
place that frame. The point does not follow the arm or head through the pose.

## Room build and pose holds

The layout rules text carries `socket anchors <version>` (`socket_reads::kAnchorVersion`, 1) or `socket anchors
off`, and `pose hold off` now by default (`kPoseHoldByDefault` is false; `-sfve_pose_hold=1` in a development
build turns it on). The layout id changed, so `data/pick-space.json` is owed a new export from the game
(the pipe's `catalog-export`); until then `RoomBuildTests`' pick space check fails.

## Logs

- `-sfve_socket_log=1`: `socket: frame F[ replayed], unit u (Pn), socket S, from rva 0x...: x y z from the
  fight's position`, one per anchored read.
- The `sockets:` lines end with `N given from the fight's position, M anchor unresolved`.

## What a game run must show

- Rooms c, d, e of `D:\sf4-wt` field runs (`D:\sfv-re\b9-pass\s3`) and the lagged-replay variants with
  `-sfve_pose_hold` at its default (off): no desync; `anchor unresolved` 0 and `given from the fight's position`
  above 0 in a match that uses Z33's, Z35's, the prop's or BLR's objects.
- In the socket log, each anchored read's value the same on both copies, including on replayed frames, and
  equal to the fighter's position plus the offset (facing mirrored).
- The unit's position at +0x780 is four floats in fight units (assumed from the virtual +0x88 and the run
  layout, not read in IDA this session): the logged value near the fighter's feet plus the offset.
- Z33's, Z35's and the prop's projectiles and BLR's claw and mask start where the note says and look right.
- The old pose record's counts for the other reads (the spawn bone resolver, rva 0x4CFC99) as before.
