# Frozen interfaces (beta.8 step 0)

Branch `b8/base`. Each header below is final: a later stream fills the bodies, and changes a
signature only with the other streams that use it. Stubs return "nothing yet" (false, empty,
no-op) so everything compiles and links from step 0.

**Selection asset paths** (`src/ember/SfvSelectionAssetPath.hxx`, namespace `sfve::assets`).
`IsSfvSelectionAssetPath(path, space)` and its embedded-pick-space overload, the builders
`ManifestPath`, `CutoutPath(code, costume, colour)` and `LoadoutPicturePath(code, part,
choice)`, and `EmbeddedPickSpace()`. The rules, the four path forms and the library they
live in (`sfve_selection_assets`, linked by the mod, Launcher, Updater and
`sfve_package_check`) are written in the header. Stream B fills `IsSfvSelectionAssetPath`
and replaces both `IsSelectionAssetPath` stubs (`SfvSelection.cxx`,
`LauncherSelection.cxx`) with calls to it; the builders and `EmbeddedPickSpace` are already
real.

**Fighter pictures** (`SfvPictureSpec.hxx`, `SfvArt.hxx`). `pictures::FighterTile(code)`
(`CharaSelectIconLeft`, whole) and `pictures::FighterLarge(code)` (`PartCharacter`, whole),
and `adapter::FighterPicture::Tile` / `Large`, which `FighterImage` already maps to them.
Stream B owns these files and their tests; stream C draws them (the board's native tiles,
the art pane's fallback).

**Loadout picture** (`src/ember/SfvSelection.hxx`, `sfve::adapter`).
`LoadoutPicture(art, fighter, part, choice)` and `LoadoutPictureNumber(part, choice)`
(part * 2 + choice, the number `SelectionArt::Ultra` is asked with). The wrapper is real;
stream B makes `SelectionArt::Ultra` in `SfvSelection.cxx` answer it from the package's
`vs<k>.png` / `vt<k>.png`, and stream C's loadout cards call `LoadoutPicture`.

**Loadout notes** (`src/ember/SfvLoadoutNotes.hxx`, `sfve::adapter`). `LoadoutNoteKey`
(real), `LoadoutNoteFor(code, part, choice)`, `LoadoutNote(fighter, part, choice)` and
`LoadoutCatalogText(tag)` (real: the embedded `.po` text, or none), over `locales/loadout/<tag>.po`,
embedded by the same function that embeds `locales/` (`sfve_embed_locales` in
`CMakeLists.txt`; the table is `LoadoutCatalogs`, entries `{tag, parts}` with each catalog in per-entry
parts, because one literal of 180 entries can pass MSVC's limit). Stream D fills the
lookup (active language, then English) and writes the 180 English notes and 13 draft
translations; stream C's loadout cards call `LoadoutNote` and draw it with `NoteUserText`.

**Select grid** (`src/ember/SfvSelectGrid.hxx`, namespace `sfve::grid`). `SelectGrid`
(columns, per-row offsets, one `Entry` per fighter code, Random's cell), `ParseSelectGrid`,
`EmbeddedSelectGrid`, `CellOf`, `CodeAt`, `RowCount`, `RowOffset` and `FitToRoster` (a
catalog fighter missing from the table is appended and returned for the log). The JSON
shape is written in the header; `data/select-grid.json` is a provisional table in pick-space
order, 16 columns, Random last, embedded like the pick space. Stream C fills the `.cxx` and
re-cuts the table from the game's own select screen.

**Random fighter** (`src/ember/SfvRandomFighter.hxx`, `sfve::adapter`).
`NoteRandomFighter(random)`, `FollowRandomFighter(snapshot)` and `RandomFighterPlayed()`,
modelled on SF4 Ember's `FollowRandomFighter`. Stream C fills them together with SfvPick's
per-fighter remembered look and Random flag.

**Photo booth** (`src/mod/BoothMode.hxx`, `src/overlay/FrameGrab.hxx`). `BoothMode` (a
`BattleMode`; a pass-through until filled), the pure `booth::ParseSteps` /
`booth::ParseCommand` and `booth::Command`, and the frame grab API (`EncodeBmp`,
`QueueGrab`, `CurrentStatus`, `DepthReport`, `OnPresent`, `InstallDepthProbe`,
`ParseDepthChoice`). The pipe contract is `booth-protocol.md`. Stream A-mod fills all of it
and adds `ModMode::Booth` and its `MakeMode` case; PhotoBoothTests is its test file.

**Present tap** (`src/overlay/Overlay.hxx`). `overlay::PresentTap` and
`overlay::SetPresentTap(tap)`: called on the render thread before the overlay draws, for
every real Present, with the swap chain as an opaque pointer. Real already (a no-op until a
tap is set); stream A-mod sets it to `grab::OnPresent`.

**Fighter scale now** (`src/mod/TinyFighters.hxx`). `bool ApplyFighterScaleNow(float p1,
float p2)`: scales each side's fighter mesh at once on the game thread, for the booth's
background plate. Stream A-mod fills it (per-side visual part memory, factor parameter on
`tiny::ScaledScale` / `ApplyToPart` defaulting to `kTinyScale`).

**HUD call** (`src/mod/HudText.hxx`). `bool HudCall(method, number, text)` and
`SetHudCallLog(on)`: the booth's probe for the call that hides the HUD. Stream A-mod fills
them (remember the movie pointer in the hook; call through the original).

**Embedded data and tests.** `data/pick-space.json` and `data/select-grid.json` are embedded
as bytes (`sfve_embed_text_file`; ASCII only). `sfve_ember_tests` gains
`SelectionAssetPathTests`, `SfvSelectGridTests`, `LoadoutNotesTests` and `PhotoBoothTests`
(one passing case each) and the defines `SFVE_SELECT_GRID_PATH` and
`SFVE_LOADOUT_LOCALES_DIR` for tests that read the files.
