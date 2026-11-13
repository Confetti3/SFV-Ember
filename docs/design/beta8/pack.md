# Beta.8 pack and loading (stream B)

What the mod, the Launcher, the Updater and `sfve_package_check` accept as selection pictures,
how the mod loads and falls back among them, how `scripts/package.ps1` ships them, and the
manifest the capture tools must write. Interfaces: `interfaces.md`.

## Path rules

One function, `sfve::assets::IsSfvSelectionAssetPath(path, space)`
(`src/ember/SfvSelectionAssetPath.cxx`, library `sfve_selection_assets`), over the pick space
embedded from `data/pick-space.json`. `sf4e::selection::IsSelectionAssetPath`, which the
library's updater and package check call, is defined twice (`SfvSelection.cxx` for the mod's
test target, `LauncherSelection.cxx` for Launcher, Updater and `sfve_package_check`) and is
one call to it in each, so no two programs can disagree.

Accepted, exactly (paths are relative to the package folder, backslash separated, the
fighter's code as the pick space writes it):

| Form | When |
|---|---|
| `assets\selection\manifest.json` | always |
| `assets\selection\<CODE>\costume-<N>\color-<M>-cutout.png` | the fighter has costume N (the game's costume id, with gaps) and that costume has colour M (the game's number; EX colours are 100 and up) |
| `assets\selection\<CODE>\vs<k>.png` | k from 1 to the fighter's V-Skill count |
| `assets\selection\<CODE>\vt<k>.png` | k from 1 to the fighter's V-Trigger count |

Everything else is refused: other extensions or case (`.PNG`, `.jpg` for a cutout or a
loadout picture), any suffix or trailing separator, forward slashes or doubled backslashes,
`..` or `.` segments, `:` streams, lowercase or longer/shorter codes, leading zeros or signs in
N, M and k, numbers past six digits, an embedded NUL. The comparison is exact and
case-sensitive, as SF4's rules are. With an empty pick space only the manifest is accepted.

The beta.7 updater does not know these names; it takes them only because the package's own
`PackageInventory.inc` lists them (below).

## Loading (`SfvSelection.cxx`)

Pictures load as SF4's do: `Request(key, files, side, policy)` over
`PackagedFiles(key)`, from `<mod folder>\assets\selection` (the root `Overlay.cxx` already
passes). `PackagedFiles` tries `<key>-cutout.png`, `<key>.png`, `<key>.jpg`; only the form the
package ships exists. Costume and colour numbers come from the catalog (`CostumeIds`,
`ColourIds`: the game's numbers and order, the same as the pick space's).

- `Character(fighter, costume, colour, large)`: the look's cutout, then that costume's first
  colour, then the fighter's first costume in its first colour; cropped to content; 512 px
  longest side for `large`, 128 otherwise. A look the catalog lacks is not asked for. Missing
  only for a fighter the catalog lacks (or with no costume). Absence is not logged (the pane has
  fallbacks).
- `Appearance(fighter, costume, colour)`: the exact look only. Missing at once for a costume or
  colour the catalog lacks; otherwise missing once the package is found not to hold that file.
  No fallback; the pane falls back to `Character`, then `FighterPicture::Large`
  (`PartCharacter`), then the banner.
- `Ultra(fighter, n)`: n = part * 2 + choice (`LoadoutPictureNumber`; V-Skill I 0, II 1,
  V-Trigger I 2, II 3), the file `<CODE>/vs<choice+1>.png` or `vt<choice+1>.png` (256x144, decoded at its own size). Missing
  for n outside 0 to 3, a choice the fighter lacks, or an unknown fighter. `LoadoutPicture`
  wraps it with the typed part and choice.
- `FighterPicture::Tile` / `Large` (`CharaSelectIconLeft` whole, `PartCharacter` whole) are
  specs in `SfvPictureSpec` and values in `SfvArt`. A Tile and a Face request of the same game
  texture are two cached pictures (the key is path and cut); each loads the object by path (the
  game keeps and returns one object), takes its own D3D reference and view, and only Face reads
  the alpha. Harmless; no shared load needed.

## Package steps (`scripts/package.ps1`)

```
scripts\package.ps1 -RenderStore D:\sfv-ember-renders [-MinColourCoverage 0]
    [-OldPackageCheck D:\keep\beta7\sfve_package_check.exe] [-NoRenders]
```

1. All existing checks first (build receipt, fingerprints, binary hashes, helper). A player
   package needs `-RenderStore`; without it the script stops, unless `-NoRenders` (a development
   or test package: warning, receipt says `renders.included = false`, repo inventory unchanged).
2. `Read-RenderStore` (before any staging): manifest fields and format; the manifest's
   `gameBuild` equals the build in this checkout's `data/pick-space.json`; every listed path is
   one the pick space allows (same rules as the C++); each file exists with the stated bytes and
   SHA-256 (case-insensitive hex); each file's `look` agrees with its path; no file in
   `<store>\pack\assets\selection` is missing from the manifest; every offered costume's first
   colour (the first entry of its `colours` array) has a cutout or a listed exception (a costume
   the pick space marks `"unshipped"` is not offered: no picture of it is wanted or taken, and an
   exception naming it is accepted, counted as `unshippedCostumes` in the receipt, and no longer
   needed); the cutouts
   reach `-MinColourCoverage` of the looks (default 0: beta.8 ships each costume's first colour
   only, colours being picked with swatch chips; the coverage is still reported in the
   receipt); every loadout picture of the pick space (180 now: each fighter's V-Skill and
   V-Trigger choices) has a file or a listed exception, else the package is refused; the file count and bytes fit the updater's zip reader (no zip64,
   16-bit entry count: under 59,800 files and 3 GB).
3. Stage each render under `assets\selection\<CODE>\...` and the manifest as
   `assets\selection\manifest.json`.
4. Generate the package's `PackageInventory.inc`: the repo's file unchanged plus
   `SF4E_PACKAGE_OPTIONAL("assets\\selection\\...")` lines (doubled backslashes, the
   library's syntax) for the manifest and every render, sorted, CRLF, no BOM.
5. Provenance receipt `build-provenance.json` gains `renders`: `included`, `manifestSha256`,
   `pipeline`, `captureGameBuild`, `captureModRevision`, `files`, `cutouts`, `loadoutPictures`,
   `loadoutPicturesPossible`, `looks`, `looksExcepted`, `bytes`, `colourCoverage`,
   `minColourCoverage`. `BUILD_INFO.txt` repeats the counts, manifest hash and pipeline.
6. Validation: this build's `sfve_package_check.exe --validate`, then, with `-OldPackageCheck`,
   the kept earlier beta's copy on the same folder (its compiled rules plus the package's
   inventory), then the zip download check, as before.

Coverage: `colourCoverage = cutouts / (looks - exceptions)` over every offered costume x colour
of the pick space (875 costumes in the current space, 45 of them unshipped).
`tests/RenderStoreTests.ps1` (ctest `sfve_render_store_tests`) runs `scripts/RenderStore.ps1`'s
checks on small stores. The first colour of every costume and every loadout picture are required
whatever `-MinColourCoverage` says. A cutout exception removes its look from both sides.

With first colours only, `Character` falls back from the missing look to the costume's first
colour (the intended path), and `Appearance` for any other colour is missing, so the pane uses
`Character` then `PartCharacter` then the banner.

## Manifest format (the capture contract)

`<store>\pack\manifest.json`, UTF-8 JSON, next to `<store>\pack\assets\selection\...` which
holds the files. Paths in JSON use forward slashes and are relative to `pack\`.

```json
{
  "format": 1,
  "gameBuild": "13173604",
  "modRevision": "8530906",
  "pipeline": "runbooth 1.0 / boothpack 1.0",
  "generatedAt": "2026-11-12T03:00:00Z",
  "exceptions": [
    { "fighter": "RYU", "costume": 3, "reason": "capture failed: costume does not load" },
    { "fighter": "RYU", "part": "vtrigger", "choice": 2, "reason": "no usable frame" }
  ],
  "files": [
    {
      "path": "assets/selection/RYU/costume-0/color-0-cutout.png",
      "sha256": "<64 hex>",
      "bytes": 48213,
      "look": { "fighter": "RYU", "costume": 0, "colour": 0 },
      "frame": 300,
      "stage": "TRN"
    },
    {
      "path": "assets/selection/RYU/vs1.png",
      "sha256": "<64 hex>",
      "bytes": 31844,
      "look": { "fighter": "RYU", "part": "vskill", "choice": 1 },
      "frame": 640,
      "stage": "TRN"
    }
  ]
}
```

Fields:

- `format`: the number 1.
- `gameBuild`: string of the game's build, exactly the second segment of the pick space's
  `build` ("sfv-ember/**13173604**/0.5/..."). A mismatch refuses the package.
- `modRevision`: git revision (7 to 40 lowercase hex) of the mod that captured.
- `pipeline`: non-empty string naming the tools and versions.
- `generatedAt`: ISO 8601 UTC, informational.
- `exceptions` (optional): pictures deliberately left out, each with required `reason` text. A
  cutout exception names `fighter` and `costume` (in the pick space) and optionally `colour`
  (omitted means the costume's first colour). A loadout exception names `fighter`, `part`
  (`"vskill"` or `"vtrigger"`) and `choice` (1 or 2) that the fighter has. An excepted picture
  must not also have a file.
- `files[]`, each exactly once: `path` (one of the two per-file forms above, in the pick space,
  case-exact), `sha256` (hex of the file's bytes), `bytes` (positive, equal to the size), `look`
  (cutout: `fighter`, `costume`, `colour`; loadout: `fighter`, `part` = `"vskill"` or
  `"vtrigger"`, `choice` = 1 or 2, the file's k), all of which must agree with `path`; `frame`
  (the fight frame captured, number) and `stage` (string) are required and only recorded.
- The manifest is shipped as `assets\selection\manifest.json`, so it must stay ASCII-safe
  JSON and carry nothing private. Cutouts are 256x384 palette PNGs and loadout pictures 256x144 RGB PNGs, both as SF4's importer and
  its Ultra pictures are made (Lanczos, `ImageOps.fit` for the loadout ones, no palette for them); the pack is what the
  game draws, not the 512x768 masters (those stay in the store).

## Not verified

No build, no ctest, no game run, no run against beta.7's `sfve_package_check` or a real render
store (the function was exercised against a synthetic 6,606-file store; a full package run needs
the laptop build).
