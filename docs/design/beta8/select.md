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

## Costumes the game ships nothing for

Each fighter has one costume slot the game's content list names by the bare text id
`ID_SYS_BSet_0224` (folder 0C) and loads as a plain grey model: 45 in all (the photo booth's
capture, `D:\sfv-re\b8-capture\unshipped-costumes.txt`; RYU:15, KEN:10, CMY:17, CNL:25, Z36:8 ...).
None is ever offered or played:

- **Data, not a table in the screen.** `data/pick-space.json` marks them (`"unshipped": true`,
  `picks::Costume::unshipped`); costume numbers stay the game's (they were sparse already). The
  mod embeds the file and marks the game's lists from it by code and number (SfvCatalog's
  `PickSpaceOf`), so every copy of a release marks the same costumes; the room host embeds the
  same file. `catalog-export` (`PickSpaceJson`) also marks every costume the game names by a bare
  text id, so a re-export carries them, and the catalog logs any difference between the marks
  and the game's names when it fills.
- **Rules** (`SfvPickSpace`, the same in the mod and the room host): `ValidPick` refuses an
  unshipped costume; `FitPick` moves a pick on one to the fighter's first costume it ships (never
  one of these: the test checks no fighter's first costume is); `Problem` refuses a fighter with
  none it ships; `CostumeLimit`, `ColourLimit` and `CostumeCount` count only shipped ones (the
  current data's values do not change).
- **Never offered:** `CostumeIds` leaves them out (the costume cards, the costume row, Left and
  Right, previews), and the catalog's ready rules ignore them (`FighterLocked`, `FitReadyLook`,
  `FighterNotOwned`).
- **Stored, remembered, dummy, Training, Random, Eleven:** every stored pick is fitted
  (`SetPick` / `SetDummy` through `FitOwnPick`, which runs `FitPick` first); remembered looks are
  stored picks; Random's roll is `RolledPick` / `RandomCandidates` through the catalog's
  `normalizePick`; Eleven's look goes through `FitLookWithin`; Training plays the stored picks.
  The costume codes (`picks::kLoadCodes`) are buttons held at load over the fitted costume.
- **Received picks** are not fitted but refused, so no copy can play one: the sender's runtime
  will not send a pick `validPick` refuses (library `SessionClient::PreBattle_SetChara`), the
  room host rejects one (`SessionServer` handlers, `validPick` over its embedded pick space) and
  keeps the seat's previous pick, a room match with one is a match problem (`FindMatchProblem`),
  and every copy's launch refuses it (`LaunchPicks`: UnsupportedMatch). Both copies therefore
  load the same, shipped look.
- **Ids:** the marks are not in `picks::Fingerprint`, so `kMatchBuild` (0.6), the fingerprint,
  the build id in `data/pick-space.json` and the layout id are unchanged. A release that
  changes the marks bumps `kMatchBuild`.
- **Pack:** `scripts/RenderStore.ps1` (package.ps1's render checks) wants a first-colour cutout
  of every offered costume only; the 45 exceptions in the manifest are accepted and counted
  (`unshippedCostumes`), and a picture of an unshipped costume is refused.

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

**The table is cut from the game's own select screen** (the laptop's capture
`D:/sfv-re/b8-j0/select-native-1.png`, the game's 1920x1080 frame at the top left of the
image, and its crops): two rows of slanted tiles split by one tall Random tile spanning both.
Row 0: columns 0-10 left of Random (Rose ... Ryu, outward to inward), Random at column 11,
columns 12-22 right of it (Ken ... Akira). Row 1: columns 0-11 (Luke ... Nash), columns 12-22
(M. Bison ... Oro); the game's ELEVEN, last on the right, is not a fighter of ours and its cell
(23, 1) stays empty. `rowOffsets` [0, -0.54]: the second row sits 38 px (0.54 of the 70 px
pitch) left of the first.

What the board draws from the table, measured on the capture's pixels (1080 lines; constants at
the top of `SfvSelectionScreen.cxx`): tiles 70 px apart, each frame 64 px wide and 142 tall,
leaning 0.2 px sideways a line (the left side's foot to the right, the right side mirrored),
rows 151 px apart; a cell right of Random's column stands 0.69 pitches further right
(`kRandomGap`: 0.66 on the game's first row, 0.73 on its second, so a tile lands within 3 px of
the game's); the Random plate fills the room between the two sides, 6 px clear of the tiles
beside it, from 60 px under the first row's top to 47 px above the last row's foot (108 px wide
at its top, 39 at its foot, as the game's). The board is scaled as a whole to its area and
centred on Random, as the game centres it. `SfvSelectionScreen::BoardColumn` is the placement
across; `SfvSelectionScreenTests` checks it against the capture's tile centres (within 4 px),
and `SfvSelectGridTests` checks the table's shape (two rows, 11/11 and 12/11 around Random,
the tiles beside Random and at the ends).

Pad: Left and Right along a row; Up and Down to the nearest tile by where it is drawn (the
second row's offset makes Down from a first-row tile its right-hand neighbour below). The moves
across Random are set on the entries (`SfvSelectionScreen::LinkRandom`, tested), both ways:
Right from Ryu and Left from Ken land on Random, and Random's Left and Right are Ryu and Ken;
on the second row Right from Nash and Left from M. Bison land on Random too. Up from Nash and
Down from Ryu stay positional (row to row). The dummy's board has no Random card and no links.
The status' `pick.code` names the fighter whose tile has the focus, so the tools find a
fighter on the board by its code.

Not done here: the development dump of the setup asset's "+48 select-grid list"
(`SfvOffsets.hxx:477-478, 489-497`) on the `content` command. It needs the battle setup asset
that `src/mod/GameContent.cxx` finds and a line in `src/mod/dllmain.cxx:305`, both stream
A-mod's files; it should log the list's fighter codes in order so the table's order can be
checked against the game's.

Tiles: the game's `CharaSelectIconLeft` (`FighterPicture::Tile`) at its own 116x144 on the
frame's centre, mirrored on the right side (the game draws `CharaSelectIconRight` there, which
`SfvPictureSpec` does not offer yet: stream B's to add; until then the Left picture flipped),
else the head's square (`Face`), else the fighter's name. A thin frame edge as the game's; the
focus's and the pointer's ground behind the tile, the gold frame on the focused tile (as the
game's cursor), the gold foot under the stored one.

## The art pane and pictures

The package holds each costume in its **first colour only** (the owner's call for beta.8),
plus the V-Skill and V-Trigger pictures; colours are chosen with the game's swatch chips.

The board's pane, the look page's pane and the card pages' panes are one drawing
(`DrawPickPreviewIn` in `SfvPickPreview.cxx`), on the detail pane's panel ground:

- The figure (`ChooseLookPicture`, tested): the look's own picture where the package has it
  (`SelectionArt::Appearance`); else its costume's picture, which is of the costume's first
  colour (`PictureColour`); else the shop's costume picture where `SfvPictureSpec` allows it
  (off), the fighter's large HUD figure (`PartCharacter`) or the banner. Any picture that is not
  of the look's own colour carries the colour's tag at its foot: the colour's swatch chip and
  its number ("05", "EX01") on a dark plate, so a costume picture never passes for another
  colour. The costume's first colour itself is shown untagged (its picture is that colour).
- The fighter's name, costume name, the colour chips and the colour's label, then the loadout
  lines; the note (why it cannot be chosen) first, in the danger tone.
- Random's card: a question mark on a banner-shaped plate (gold while Random is the pick),
  `selection.random_card`, the rolled fighter, and `selection.random_card.detail`.
- On the look page, the stage card shown while the Stage row has the focus is a button: a click
  opens the stage cards.

Cards: costume cards show each costume in its picture's colour (its first), labelled with the
costume's name; colour cards are the colours' swatch chips (the game's two colours each, square,
as large as the card holds), one this player cannot ready dimmed and struck through, with the
pane beside them showing the costume tagged with the focused colour; stage cards
`StageSelectIcon_l`; loadout cards the captured move picture (`LoadoutPicture`, `vs<k>.png` / `vt<k>.png`), else the
fighter in their look, with the game's I/II mark, SAVED on the stored choice, the part and
numeral, the game's move name and the note (`LoadoutNote`, drawn after `NoteUserText`).

Elsewhere (the library's room seats and cards) a look is drawn through `SelectionArt::Character`,
which falls back to the costume's first-colour picture; those places show no chip, and the
costume picture alone is the accepted rule there.

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
   Check the mirrored right-side tiles against the game's Right pictures, and that each
   picture's slant matches its frame (the frame's lean is the capture's border; the picture's
   is the texture's own, about 22 px over 140 rows per EMBER_LIBRARY.md section 37).
6. Check every picture fallback at least once: a fighter with no captured cutout shows its
   `PartCharacter`; a colour with no cutout its swatch; a loadout card with no picture the
   fighter; smallest window (960x540) shows the pane's words with the picture shrinking.
