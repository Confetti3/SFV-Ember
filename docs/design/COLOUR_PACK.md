# The colour pack (0.1.1)

The maintainer, 2026-11-18: SFV Ember 0.1.1 ships every colour of every costume, but as a download on first
run, not in the zip. The zip stays what 0.1.0's was (each costume's first colour and the loadout pictures); the
other 5,506 cutouts (78.5 MB of the 2026-11-17 capture, boothpack 1.1) are one archive the launcher fetches once.

## What ships where

| | Zip (`sfv-ember-<version>.zip`) | Colour pack (`sfve-colours-<id>.zip`) |
|---|---|---|
| Cutouts | every offered costume's first colour (830) | every other colour (5,506) |
| Loadout pictures | all 180 (3.7 MB: small, and the loadout cards need them at once) | none |
| Manifest | `assets\selection\manifest.json`: the store's, with the zip's files | `manifest.json`: the store's, with the pack's files |
| Descriptor | `colour-pack.json` at the package's top | |

The two are a partition of the render store (`docs/design/beta8/pack.md`): `Read-RenderStore` checks the whole
store as before (`-MinColourCoverage 1` in a release), then `scripts/ColourPack.ps1` splits it by the pick
space's first colour of each costume. The zip has every first colour by construction, so its coverage check is
the store's first-colour check; the full set is the store's coverage check.

## The pack and its descriptor

- **One archive, content-addressed.** Stored entries (PNGs do not deflate), sorted, every time stamp
  1980-01-01: the same pictures make the same bytes on the build PC. The pack id is the first 16 hex digits of
  the archive's SHA-256, and the asset is named `sfve-colours-<id>.zip`. Not `sfv-ember-...`: the updater takes
  any asset with the package's prefix as the package.
- **Hosted in one release of its own**, tag `colour-packs` on the public repository, a pre-release that is no
  version (the updater and the pipeline's version checks pass over it). A release whose pack is unchanged names
  the same asset, which is there already: nothing is uploaded again, and no release depends on an earlier
  release staying up. Packs of older releases stay, so older installs keep theirs.
- **The descriptor**, `colour-pack.json`, is written by `package.ps1` and travels inside the zip, so the
  updater's hash of the package covers it:

  ```json
  { "format": 1, "id": "<16 hex>", "bytes": 82000000, "sha256": "<64 hex>",
    "url": "https://github.com/Confetti3/SFV-Ember/releases/download/colour-packs/sfve-colours-<id>.zip" }
  ```

  `src/ember/SfvColourPack` reads it for the launcher and the game alike: format 1, the id is the SHA-256's
  prefix, the URL is an HTTPS release download of the product's own repository (`stableRepo`) ending in the
  asset's name. Anything else is no pack. A package without the file (a development package, `-NoRenders`, a
  first-colour store) has no pack, and everything below does nothing.

## The launcher fetches it (`src/launcher/ColourPackFetch`)

Outside the game's network guard, which is unchanged: the game never downloads. One thread, started once as
the launcher prepares the first start (a test's copies, `--instance N`, leave it to the player's copy, whose
data folder they share), so the download overlaps the game's own start-up:

1. Read the descriptor beside `Launcher.exe`. None: nothing to do.
2. `<data>\colour-packs\` (`%APPDATA%\SFVEmber\colour-packs`, never the install folder, which may be read-only)
   keeps only `<id>\`: older packs and an interrupted fetch's leftovers are removed.
3. `<id>\` is there: done. Otherwise download the URL to `<id>.download` with the updater's HTTP download
   (redirects followed; the transfer stops past the descriptor's size), check the size, then the SHA-256,
   extract with the updater's zip reader (an unsafe entry name refuses the whole pack) into `<id>.incoming\`,
   and rename that to `<id>\` in one step. `<id>\` exists only complete.
4. Any failure, offline included: a log line, the leftovers go, and the next launcher start tries again. The
   game starts and plays meanwhile; it never waits for the pack.

Closing the launcher (the game exited) cancels a fetch under way. Progress is a log line at each quarter;
there is no window: the launcher has none while the game runs, and the shell offers the product no notice of
its own yet.

## The game looks in the pack first (`PictureSources`)

`SelectionArt::Character`, `Appearance` and `Ultra` (`SfvSelection.cxx`) ask `PictureSources` for a picture's
files instead of the library's `PackagedFiles`: for one picture, the pack's file, then the package's, both by
the canonical paths of `SfvSelectionAssetPath` (both folders hold the package's layout). The pack is a source
when the package names one, whether or not it has arrived yet: the decoder skips a candidate that does not
exist and logs nothing for it. So:

- A look in the pack shows the pack's picture. A look the pack lacks (a first colour, or no pack at all) shows
  the package's, and `Character`'s fallback to the costume's first colour is unchanged: with no pack, exactly
  0.1.0's behaviour.
- A pack that arrives mid-session is picked up by every picture asked for afterwards; one already shown keeps
  its first-colour stand-in until the next start (the cache keeps what it decoded).
- Only the pack the package names is ever read; an old pack's folder is never a source.

## The release pipeline

- `package.ps1` writes the pack beside the zip and records it in the receipt (`renders.colourPack`: id, bytes,
  SHA-256, URL, cutouts) and `BUILD_INFO.txt`. The build PC's job brings it back by its name.
- The build stage checks the pack against the descriptor in the zip (size, SHA-256, name, URL on the public
  repository's `colour-packs` release) and scans its entries for forbidden strings, as it scans the package.
- Promote's `colours` phase (outward, before the pushes): creates the `colour-packs` pre-release if the
  repository has none, uploads the pack unless an asset of that name is there, and checks the hosted asset's
  size and digest either way. `confirm` downloads it again and compares.

What the maintainer does at a release: nothing new. The first release that runs the `colours` phase creates
the hosting release; later releases with an unchanged pack upload nothing.

## Sizes (2026-11-17 capture, boothpack 1.1)

Zip about 48 MB (0.1.0: 57.4 MB, whose first colours and RGB loadout pictures took 24.5 MB; now 15.3 MB).
Pack about 80 MB: 78.5 MB of cutouts and the 1.7 MB manifest.
