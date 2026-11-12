# Eleven (beta.8)

Eleven is SFV's own random fighter: each game it plays as another fighter, in Eleven's silver
look. SFV Ember left it out (EMBER_LIBRARY.md section 15: ELEVEN has no battle setup, and a
launch with ELV itself hangs at a black screen). Beta.8 puts it back on the fighter board as a
pick, in its own cell, with no library change and no change to the pick space, the room host's
data or the build id.

## What SFV itself does (laptop IDA, `StreetFighterV.exe`, rva)

- **Eleven is resolved before the battle loads, never inside it.** The select screen's result
  writer (0x5F7F80, which also sets `bIsDecidedCharaEleven_Left` and `_Right` false on its
  widget) compares the decided code with `ELV`. For ELV it calls 0x5506A0 for the fighter Eleven
  will play, then writes the mode data's fighter record (setters at vtable +40, +56, +64 as
  `VersusLaunch::FillCharPkg` does) for that fighter. A battle never sees `ELV`, which is why
  Ember's launch of ELV hung.
- **How it picks the fighter (0x5506A0):** the fighter table's codes (0x54E8E0), less `RND` and
  `ELV`, less every code the asset's filter object (asset +280, vtable +8) refuses (the
  ownership filter the select screen uses), then one of the rest by the C runtime's `rand()`:
  uniform, per PC, not synced. With none left it plays RYU. Ember replaces this with the
  runtime's Random roll, which is the same shape (a uniform draw over the fighters this player
  may play) and which the Ready carries to the opponent.
- **Its costume and colour** are the select state's own for the ELV cell (an object read through
  0x174E230: costume +16, colour +20), checked for the substitute fighter by the generic rule at
  0x5873F0 (the costume must exist for the fighter, the colour is kept only if the fighter's
  special-costume table allows it, else 0). Its V-Skill and V-Trigger are the player's saved ones
  for the substitute (the 56-byte per-fighter table, 0x78A6F0 / 0x78C710 / 0x78C640), not drawn.
- **No string, flag or material name for "silver" exists in the exe** (searched, UTF-16 and
  ASCII: only the `bIsDecidedCharaEleven_*` fields, `PlayDecideCharaVoice(IsEleven)` and
  `ELEVENTH`). The look is therefore data: ELV's own costume and colour rows in the spec
  (`ELEVEN with one costume`, section 15), and whatever the game's assets do with the
  `bIsDecidedCharaEleven_*` flags on the versus screen. Nothing in the exe applies a material
  override to a battle fighter for Eleven.

## What Ember does

Eleven is **Random's roll in Eleven's look**. Random (`SfvRandomFighter`) already gives each game
a fighter the runtime rolled, which every Ready carries as a concrete pick, so the room, the
opponent's copy, each watcher and the replay see the same fighter, costume, colour, V-Skill and
V-Trigger, and nothing is random at launch. Eleven keeps all of that and changes only the look
the rolled fighter wears and how the loadout is chosen.

| Piece | Where |
|---|---|
| The pick value | `SfvPick`: `PickEleven()` / `SetPickEleven(bool)`. Eleven sets Random on with it (`PickRandom()` is true), so the runtime rolls each game; choosing Random again or any fighter turns Eleven off. Not saved between runs; the dummy has none. |
| The resolve step | `SfvEleven`: `ElevenPick(fighter, seed)` is a pure function. `ElevenSeed(salt, roll)` mixes the runtime's roll number (`RuntimeSnapshot::randomRoll`) with this run's salt. |
| Taking a roll | `SfvRandomFighter::FollowRandomFighter`: while Eleven is the pick every roll is taken (even a repeat of the stored fighter), stored as `ElevenPick`, and logged (`Fighter select: Eleven rolled RYU costume 0 colour 2`). Switching between Random and Eleven takes the roll in hand again. |
| The Ready the runtime remakes | `NoteRandomFighter` hands the runtime Eleven's looks for the *next* roll in place of the remembered ones, so a Ready parked over a WIN, which the runtime rebuilds from the new roll (`RolledPick`), still carries the Eleven look and the loadout drawn for that roll. |
| The tile | `data/select-grid.json` `eleven` {column 23, row 1}: ELEVEN's native cell, last of the bottom right group. `SfvSelectGrid` reads it (`kElevenCellCode`, `CodeAt`, `RowCount`, `FitToRoster` steps over it). `SfvSelectionScreen` draws the card `eleven-fighter` (`BoardRows`), its art from the game's Hud textures `/Game/Chara/ELV/Hud/CharaSelectIconLeft` and `CharaSelectIcon_l` (`SfvArt::ElevenImage`), the art pane `PreviewFocus::Eleven`, titles `ELEVEN`, a middle dot, then the fighter (`ElevenTitle`; the game's own name for it, else "ELEVEN"). No new message id: the detail line is Random's (`selection.random_card.detail`: "A new fighter for every game ..."), the name is the game's. |
| The spec's look | `GameContent` reads ELEVEN's spec row as a fighter's (`ContentLists::eleven`, outside `fighters`): `SfvCatalog::ElevenSpecOf()` is its first costume's number and that costume's first colour. |
| The log | At the catalog fill: `catalog: ELEVEN lists costume N colours ...`. |

### The look rule (`ElevenPick`)

1. Start from the fighter with the spec's Eleven costume and colour numbers (`ElevenSpecOf`).
2. Fit it as any look of a fighter this player may play is fitted (`FitLookWithin`): where the
   fighter lacks that costume or colour it is the fighter's first costume this player can ready
   and its first colour; a look this player cannot ready (not owned with the gate on, not
   installed here) is never offered. So with the gate on **Eleven never resolves to a look the
   player does not own**; with it off the spec's rule alone applies, as for any pick.
3. The V-Skill and the V-Trigger are each drawn from the choices the fighter has (one or two),
   from the seed: this differs from the game, which plays the saved ones. It is the brief's
   "random V-Skill and V-Trigger", and it is part of the pick that travels, so both copies play
   the same.
4. Normal size, no handicap. (Nothing about model size is touched.)

### Same on every copy, and nothing random at launch

There is no room seed in the runtime: the roll is the player's own (the runtime's generator),
and what makes both copies agree is that the **concrete pick travels in the Ready** (the wire's
`ConfirmedCharaConditions`), as it does for Random. The loadout is drawn on the player's copy only
(seed from the run's salt and the roll's number); the opponent's copy, the room host, every watcher
and the replay (`SfvReplays`'s header is written from the launched picks, so it records the
concrete fighter, costume and colour) read the concrete values. A launch (`LaunchPicks`) only ever
sees a fighter of the catalog, so the launch's checks are unchanged.

### The remembered looks stay the player's

`SfvPick::SetPick` remembers the look last stored for each fighter (Random's per-fighter memory).
While Eleven is the pick it does not: Eleven's looks belong to Eleven, so playing Eleven never
changes the look Random or a fighter choice later opens in.

## Does the silver look work?

**Not established, and not claimed.** The exe has no silver material, flag or costume id for
Eleven (above); the look is in the game's asset data, which the exe's analysis cannot read. What
Ember does is apply the look the spec lists for ELEVEN (its costume and colour numbers) to each
fighter wherever the fighter has it, which is how the game's own writer treats the ELV cell's
costume and colour (costume and colour from the ELV cell, checked per fighter). Whether that is
silver depends on the numbers the spec gives ELEVEN:

- If ELEVEN's costume and colour are numbers every fighter shares and the game draws silver
  from them, the launched fighters are silver.
- If they are the ordinary first costume and colour (0 and 0), Eleven ships as **"a random fighter
  each game, in a fixed look"**: each fighter's default look, with a drawn loadout. That is the
  documented fallback; nothing is unsafe about it, since the pick is always a look of the
  catalog this player can ready.

Either way the launch writes only a costume and colour of the fighter's own spec through
`FillCharPkg`, as every launch does: no material override is applied to a launched fighter, which
nothing in the exe shows how to do safely.

## What a laptop run must check

1. The log line `catalog: ELEVEN lists costume ... colours ...` (EmberHost, at the main menu): which
   numbers the spec gives ELEVEN. This decides whether the look is silver (see above). If ELEVEN
   has a costume and a colour that the rolled fighters also have, step 3 should show it.
2. The board: ELEVEN's tile stands in column 23 of the second row (the last tile of the bottom
   right group), its picture loads from `/Game/Chara/ELV/Hud` (if the game has no such texture
   the tile shows the name `ELEVEN` instead; the `art:` log line says "the game has no texture
   there"), the pad reaches it from Z40 (Right) and from the first row's last tile (Down),
   Select stores Eleven (the card's gold foot, the art pane's ELEVEN name and rolled fighter),
   and the Random tile is not the stored card while it is.
3. In a room: with Eleven stored each game plays a different fighter (the log `Fighter select:
   Eleven rolled <CODE> costume N colour M`), both copies show the same fighter, costume and
   colour on the versus screen and in the battle, and a winner who keeps the fighter keeps the
   rolled one (the runtime's rule). Look at the fighter: silver or not.
4. The rematch over the WIN (parked Ready): the next game's fighter and loadout match the next
   roll, on both copies, with no `desync` log.
5. The replay of an Eleven game shows the concrete fighter and look, and the same loadout.
6. Pick space, build id and `data/pick-space.json` are unchanged (no fighter or costume was
   added): `catalog-export` must not need re-running for this change.

## Tests

`tests/SfvElevenTests.cxx`: ELEVEN is read as the spec lists it and is no fighter of the pick space
(the fingerprint ignores it); the pick is the same for a fighter and a seed; the look is the
spec's where the fighter has it and fitted where it does not; every loadout choice of every
fighter comes up and none is out of range; for each fighter this player may play, with the gate
on, the look is one they can ready and their own fitting leaves it alone; the seed is a pure mix
of the roll and the salt; Eleven takes each roll once in the Eleven look, keeps remembered looks,
leaves the dummy alone and switches with Random. `SfvSelectGridTests` (ELEVEN's cell is read, shares
no cell, counts as a row, is stepped over by an appended fighter, and the shipped table has it at
column 23, row 1); `SfvSelectionScreenTests` (the board's cards, the opening, the look page's
title). No build or test was run for this change (the desktop is not building); the first laptop
build must compile and run them.
