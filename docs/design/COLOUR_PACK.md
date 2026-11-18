# The colour pack (0.1.1)

**Off by default.** The maintainer, later on 2026-11-18: 0.1.1's zip carries every colour itself, as
1573e07 packaged it; a later upgrade package can leave the colours out, since installs will have them by then.
`package.ps1` puts every picture of the render store in the zip unless it is given `-ColourPack`, so a default
package has no `colour-pack.json` and no `sfve-colours-*.zip`: the launcher logs "this package names none", the
game reads every colour from the package, and promote's `colours` phase has nothing to host. Everything below
is what `-ColourPack` does; it stays built and tested for when it is used.

The first plan, 2026-11-18: SFV Ember 0.1.1 ships every colour of every costume, but as a download on first
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
2. Take `<data>\colour-packs\fetch.lock`, open with no sharing, for the whole fetch. Another launcher's fetch
   holds it (a quick restart while the old launcher is still ending): nothing to do, so two fetches never
   touch the same files.
3. `<data>\colour-packs\` (`%APPDATA%\SFVEmber\colour-packs`, never the install folder, which may be read-only)
   keeps only `<id>\`, `<id>.download` and the lock: older packs and an interrupted extraction are removed.
4. `<id>\` is there: done. Otherwise download the URL to `<id>.download` with the updater's HTTP download
   (redirects followed; the transfer stops past the descriptor's size), unless an earlier start kept one;
   check the size, then the SHA-256, extract with the updater's zip reader (an unsafe entry name refuses the
   whole pack) into `<id>.incoming\`, and rename that to `<id>\` in one step, tried for about two seconds while
   another program (the search indexer, a virus scanner) has a new file open. `<id>\` exists only complete.
5. Any failure, offline included: a log line, the leftovers go, and the next launcher start tries again. A
   download whose SHA-256 matched stays when a later step failed, and the next start installs it (checking it
   again) without downloading it. The game starts and plays meanwhile; it never waits for the pack.

Closing the launcher (the game exited) cancels a fetch under way after the read in progress; a step still
waiting on the network ends at its 20 s timeout, and until then the launcher's process stays, holding the
lock. Making the download cancellable mid-wait (the library's async transport) is open. Progress is a log line at each quarter;
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
- The colour gallery's cards are each colour's own picture (`Appearance`, `AppearanceFiles`: the look's
  cutout, the pack's then the package's, no other colour's), so they share the pane's cached pictures; a
  colour with none (no pack yet) shows its swatch chip instead, as 0.1.0's cards did. No palette is drawn beside
  or under a picture, in the cards or the pane.
- Only the pack the package names is ever read; an old pack's folder is never a source.

## The release pipeline

- `package.ps1 -ColourPack` writes the pack beside the zip and records it in the receipt (`renders.colourPack`:
  id, bytes, SHA-256, URL, cutouts) and `BUILD_INFO.txt`. It refuses a `-ColourPackRepo` that `Launcher.exe`
  does not name (its `stableRepo`). The build PC's job brings the pack back by its name, or reports none.
- The build stage checks the pack against the descriptor in the zip (size, SHA-256, name, URL on the public
  repository's `colour-packs` release) and scans its entries for forbidden strings, as it scans the package. The
  job, the descriptor and the receipt must all name the pack or all name none, and the zip's `Launcher.exe`
  must name the pipeline's public repository.
- Promote's `colours` phase (outward, before the pushes): creates the `colour-packs` pre-release if the
  repository has none, uploads the pack unless an asset of that name is there, and checks the hosted asset's
  size and digest either way. `confirm` downloads it again and compares. A package that names no pack marks the
  phase done with nothing to host. `publish` refuses while the phase is not done (`-Skip colours` included), so
  a release never goes public naming a pack nobody can download.

What the maintainer does at a release: nothing new. The first release that runs the `colours` phase creates
the hosting release; later releases with an unchanged pack upload nothing.

## Sizes (2026-11-17 capture, boothpack 1.1)

With `-ColourPack`: zip about 48 MB (0.1.0: 57.4 MB, whose first colours and RGB loadout pictures took
24.5 MB; now 15.3 MB). Pack about 80 MB: 78.5 MB of cutouts and the 1.7 MB manifest.

By default (every colour in the zip): about 127 MB, the 48 MB above plus the 78.5 MB of cutouts (PNGs do not
deflate) and a larger manifest, about 6,600 entries: within the updater's zip reader, which takes no zip64
(under 65,535 entries and 4 GB).
