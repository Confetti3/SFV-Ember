# Fighter select and stage cards (beta.8, stream C)

SFV Ember's pick screen (`src/ember/SfvSelectionScreen.*`) is now laid out as SF4 Ember's
fighter select (the library's `FighterSelector` and `AppearanceGallery`, which stay SF4's and
are not built for SFV): a board of the game's own select tiles, a look page, picture cards for
costumes, colours, V-Skills and V-Triggers, and stage cards. No library change; every word is
an existing catalog key (SFV may only overlay the library's messages), the game's own words
(fighter, costume, colour, stage and move names) or the loadout notes (stream D).

## Pages

All pages live on the screen's own `menu_.navigation`. Pad and mouse work on every page: the
body pages place each card with `PressEntry`, so the directions move by where cards are drawn,
the pointer focuses a card and a click is Select on it.

| Page | Id | Body | Select does |
|---|---|---|---|
| Board | `home` | tiles in the grid table's cells, art pane right | fighter: stores the fighter in their remembered look, Random off; then the look page, or closes when opened for the room's Fighter. Random: Random on (the board stays, or closes for the Fighter). The stored card (or Random while it is the pick) opens the look page |
| Look | `look` | today's value rows, the pick's preview right | Fighter: the board. Costume, Colour: their cards. V-Skill, V-Trigger: the loadout cards. Stage: the stage cards. Costume code: its choices. Done: closes |
| Costumes | `costumes` | a card per costume, pane right | opens that costume's colours |
| Colours | `colors` | a card per colour, pane right | stores costume and colour, back to the look page (or closes when opened for the look) |
| Loadout | `loadout` | V-Skill I/II on one line, V-Trigger I/II under | stores that choice; the page stays |
| Stage | `stage` | Random, then every offered stage, pane right | stores the stage; closes when opened for the stage |

Card ids: `fighter-<id>`, `random-fighter`, `costume-<n>`, `color-<n>`, `vskill-<n>` /
`vtrigger-<n>` (n is the game's choice number, 0 for I), `stage-<id>` (`stage-255` is Random).

Rules carried over from the rows screen:

- A fighter, costume or colour this player cannot ready keeps its place, dimmed, and cannot be
  chosen; the pane says why (`selection.not_owned`, SFV's "Not owned in this copy of Street
  Fighter V."). A locked fighter's tile is drawn at 40%.
- A stage the opponent lacks is dimmed and refused with `runtime.ready.stage_not_shared`.
- Only P1 chooses the stage (out of a room everyone does: `SfvShell.cxx`'s `stage` flag). On
  the look page the Stage row stays enabled for P2 so the stage cards can be read; there every
  card is unselectable, the pane and the status line say `selection.only_p1_stage`. A refused
  Select (`ChooseStage`'s Locked, OnlyP1, NotShared) shows its reason in the status line in the
  error tone.
- A loadout part with one choice cannot step, but its row still opens the loadout cards, so the
  move's picture and note can be read.
- An Arcade winner who keeps the fighter (`room::KeepsFighter`, new `fighterKept` on `Draw`):
  no other fighter and no Random can be chosen; the pane says `selection.winner_keeps_fighter`.
- The dummy's screen (Training select's Dummy row) has no Random card, no stage page, and does
  not use the remembered looks.

## Openings

`OpeningFor(start, pick, random, dummy)` (tested):

| Shell's `SelectionStart` | Pages, focus | Closes on |
|---|---|---|
| Board (Home's and Training select's rows) | board, stored card | nothing |
| Roster (the room's Fighter, X) | board, stored card | a fighter or Random chosen |
| Costumes (the table's Appearance) | board, look (costume), costumes (`costume-<n>`) | a colour chosen |
| Ultras (the table's and the bar's V-Skill and V-Trigger) | board, look (vskill), loadout (`vskill-<n>`) | nothing (stays) |
| LookUltra (a winner who keeps the fighter) | board, look (vskill) | nothing |
| Options | board, look (costume) | nothing |
| Stage (the table's Stage, Training select's Stage) | board, look (stage), stage (`stage-<id>`) | a stage chosen (or the same one) |

Opened on a page other than the board, Back from that page closes the screen (SF4's rule).

## The board and the grid table

`data/select-grid.json`, embedded and read by `src/ember/SfvSelectGrid.cxx`
(`ParseSelectGrid`, `CellOf`, `CodeAt`, `RowCount`, `RowOffset`, `FitToRoster`; the JSON shape
is in `SfvSelectGrid.hxx`). The parse is strict: unknown keys, a code twice, two entries in a
cell (Random's included), a column outside the grid or a negative row refuse the table. The
board fits the table to the catalog's roster once the catalog is filled: a fighter the table
lacks is appended after the last cell taken and logged (`pick screen: the select grid table
lacks ...`); a table that does not parse is logged and the board falls back to eight across.
Random gets the cell after the last one when the table gives none.

**Provisional:** the shipped table is the pick-space order, 16 columns, Random at column 13,
row 2. It must be re-cut from the screenshot of SFV's own select screen (laptop baseline run):
one `{"code", "column", "row"}` per fighter, `rowOffsets` (in tile widths, fractions allowed)
for rows the game staggers, and `random`. Two layout constants in `SfvSelectionScreen.cxx` may
need the screenshot too: `kTileShape` (the tile's height over width, from
`CharaSelectIconLeft`'s 116x144) and `kTileGap` (6 px between tiles, unscaled). Nothing else
changes when the table does; `SfvSelectGridTests` checks every pick-space fighter is in it once
and no cell is shared.

Not done here: the development dump of the setup asset's "+48 select-grid list"
(`SfvOffsets.hxx:477-478, 489-497`) on the `content` command. It needs the battle setup asset
that `src/mod/GameContent.cxx` finds and a line in `src/mod/dllmain.cxx:305`, both stream
A-mod's files; it should log the list's fighter codes in order so the table's order can be
checked against the game's.

Tiles: the game's `CharaSelectIconLeft` whole (`FighterPicture::Tile`), else the head's square
(`Face`), else the fighter's name. No Ember ground at rest; the focus's and the pointer's ground
behind it, the gold edge on the focused tile, the gold bar under the stored one.

## The art pane and pictures

The board's pane, the look page's pane and the card pages' panes are one drawing
(`DrawPickPreviewIn` in `SfvPickPreview.cxx`), on the detail pane's panel ground:

- The figure: the captured cutout (`SelectionArt::Character` through `MenuArt()`, stream B),
  else the shop's costume picture where `SfvPictureSpec` allows it (off), else the fighter's
  large HUD figure (`PartCharacter`, `FighterPicture::Large`), else the banner. While a picture
  loads its frame is the shape it will have.
- The fighter's name, costume name, the colour chips and the colour's label, then the loadout
  lines; the note (why it cannot be chosen) first, in the danger tone.
- Random's card: a question mark on a banner-shaped plate (gold while Random is the pick),
  `selection.random_card`, the rolled fighter as `selection.random_fighter`, and
  `selection.random_card.detail`.
- On the look page, the stage card shown while the Stage row has the focus is a button: a click
  opens the stage cards.

Card pictures: costume cards show the figure in the colour worn (or the costume's first
colour this player can ready); colour cards the exact look's cutout (`Appearance`), else the
colour's two-tone swatch as a square, else the figure; stage cards `StageSelectIcon_l`;
loadout cards the captured move picture (`LoadoutPicture`, stream B's `vs<k>.jpg` /
`vt<k>.jpg`), else the fighter in their look, with the game's I/II mark top right, SAVED top
left on the stored choice, then the part and numeral, the game's move name and the note
(`LoadoutNote`, stream D, drawn after `NoteUserText` so CJK glyphs load). Until streams B and D
land, every picture falls back as above and the notes are empty.

## Random and the remembered look

- `SfvPick`: `PickRandom` / `SetPickRandom` (session only; the dummy has none),
  `RememberedLook(fighter)` (the last look `SetPick` stored for each fighter: costume, colour,
  loadout, costume code), `LookFor(pick, fighter, remembered)` (what choosing the fighter
  stores, fitted by `FitLookWithin`), and `ToWire` (AddPickTo's wire pick, shared with Random).
- `SfvRandomFighter.cxx`, modelled on `sf4e__OverlayRandomFighter.cxx`: every game frame
  (`SfvShell`'s `RunEachGameFrame`) `NoteRandomFighter(PickRandom())` hands the runtime the
  stored pick and each fighter's remembered look as the wire carries them, and
  `FollowRandomFighter` takes each new `randomRoll` once: the stored pick becomes the rolled
  fighter in their remembered look, logged `Fighter select: Random rolled <CODE>`. A look edited
  after the roll stays. Training select's Start calls `RandomFighterPlayed` for a Random pick.
- `SfvShell`'s view: `selectedRandom`, and `fighterName` reads `selection.random_fighter` ("Random" and the fighter).

## Shell status

`status.shell.pick`: `page`, `focus` (the pick screen's own, the one last drawn), `dummy`,
`stored` (`PickText` of the stored pick) and `random`. The tools drive the pages with it.

## What the laptop run must check

Unit tests: `sfve_ember_tests` (`SfvSelectionScreenTests`: the nine existing cases and the new
board, art pane, look rows, openings, gallery, loadout, stage card and Random cases;
`SfvSelectGridTests`).

Game runs (English, then Japanese for the notes' glyphs):

1. `python tools/runshell.py --shots <dir>`: the room run with the new pick (X opens the
   board; Right and Select keep a fighter and close the screen). Shots: `4-pick` (board with
   the focused tile and pane), `4c-board`, `4c-board-random` (Random's pane), `4d-costumes`,
   `4e-colours`, `4f-loadout`, `4f-loadout-vtrigger`, `4h-stage` (P1's stage cards), and the
   line `copy 0: stage card stage-N: ... -> ...` must show the stage changed (the run fails if
   it did not); `4i-stage-p2` (P2's table row says only P1 chooses, Select does nothing).
2. `python tools/runshell.py --shots <dir> --unowned CNL,KEN:1,KEN:0:1`: `4b-not-owned-fighter`
   (CNL's tile dimmed in place, the pane's NOT OWNED words), `4b-not-owned-costume`,
   `4b-not-owned-color`.
3. `python tools/runtraining.py --part solo --shots <dir>`: the dummy's board has no Random
   card (`solo-3-dummy-board`), Select opens the dummy's look page (`solo-3-dummy`), its
   costume and loadout cards (`solo-3-dummy-costumes`, `solo-3-dummy-loadout`), and Training
   select's Stage opens the stage cards (`solo-3-stage`) where Select stores the stage (the line
   `solo: stage card ...` must show it changed).
4. By hand or mouse on one copy: click a stage card (stores it); on the look page focus Stage
   and click the pane's stage card (opens the stage cards); hover and click tiles on the board;
   choose Random, then Ready twice in a room and see `Fighter select: Random rolled` and a
   different fighter each game, in the look last used on them.
5. Compare `4c-board` with the native select screen screenshot: same cells, same row stagger.
   Re-cut `data/select-grid.json` (and `kTileShape` / `kTileGap` if needed) until they match.
6. Check every picture fallback at least once: a fighter with no captured cutout shows its
   `PartCharacter`; a colour with no cutout its swatch; a loadout card with no picture the
   fighter; smallest window (960x540) shows the pane's words with the picture shrinking.
