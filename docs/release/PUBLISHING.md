# Publishing an SFV Ember release

`scripts\publish.ps1` publishes a release in five stages. It runs on the desktop (where `gh` is signed in and
`ssh vps` works). The Windows build runs on the build PC over SSH: a Windows PC with Microsoft's compiler, or a
Linux PC that cross-compiles it with clang-cl (config `laptop.os`, "The build PC" below). The Linux room hosts build
in the Windows build PC's WSL, or on the Linux build PC itself. The VPS only receives files: every root step stays
with the owner. Nothing becomes public before
the last stage, and the last stage publishes only after it has checked everything again.

## The stages and what each proves

| Stage | Where it runs | What it leaves (state folder) | What it proves |
|---|---|---|---|
| `plan` | desktop, read-only | `plan.json`, `notes.md` | The label is newer than every published release, and no release, draft or tag has it yet. The source commit descends from the last release's tag and fast-forwards the public `main` and the private release branch (`release/beta.N` for a beta, `release/x.y.z` for a final). Every worktree on that commit or branch is clean, and so is the pipeline's own checkout. `CMakeLists.txt` makes this version. `data/pick-space.json`'s room build agrees with `kMatchBuild`, and the plan records whether it changed since the last release. A file `kRollbackCodeRules` covers (`src/mod/SfvLayoutRules.hxx` lists them) that changed since the last release needs that version moved. The README's library pin names the library commit, which is on the library's public branch or fast-forwards it. The notes exist and use none of the words `notesForbidden` lists (the option public text never mentions). No forbidden string (the owner's rules file, plus this PC's user, profile and computer names) appears in the notes, the Discord draft, or any commit, tree, blob or path being released, raw or UTF-16. That covers both repositories. Every released commit has the last release's identity and no `...-by:` trailer. |
| `build` | build PC (Interactive task queue) | `build\files\*`, `build\evidence.json` | Both build PC checkouts are fast-forwarded to exactly the plan's commits and trees, are clean, and have their fingerprints recorded before the build. A development build and a player build ran, with their tests (ctest's own count must equal the receipt's list), then the helper and `package.ps1 -VersionLabel <label> -RenderStore <laptop.renderStore> -MinColourCoverage 1` (every colour of every offered costume, bar the render manifest's exceptions). Back on the desktop: both receipts name the plan's commits, the checkouts' fingerprints, the right kind and passing tests. The fingerprints did not move during the job. The zip, its `.sha256` and the receipt inside it agree with each other. Every packaged binary is the player build's, by hash. `sfv_ember.dll` carries the plan's room build and match line. The packaged receipt's `renders` say the pictures are included and cover every look (`colourCoverage` and `minColourCoverage` 1). By default the zip carries every colour and the job, the zip and its receipt name no colour pack. With `-ColourPack`, the pack the job brought back (`sfve-colours-<id>.zip`, named in `result.json`) is the one the zip's `colour-pack.json` and receipt name: its size, its SHA-256 (whose start is its id) and its URL on the public repository's `colour-packs` release, which the zip's `Launcher.exe` names; all three name it or none does. No file or file name in the package or the pack carries a forbidden string or a name of the desktop or the build PC. |
| `rooms` | build PC: its WSL, or the Linux build PC itself (`server1` slot not written yet) | `rooms\out\*`, `rooms\kit.tar`, `rooms\evidence.json` | `sf4-net` (`--profile server`) and `sfve-room-host` are built from `git archive`s of the plan's two commits, checked by hash. Both are Linux binaries named in `SHA256SUMS`. The room host answers `helper_unavailable` for its own build and `other_build` for another, and embeds exactly the plan's build. They hold no build-machine path or forbidden string, and need no newer glibc than the server's (`rooms.maxGlibc`, when set). The kit is the last release's stage folder, checked against its own `SHA256SUMS`, with the library's `upgrade.sh` at the plan's commit and `room-build/` replaced. It is staged on the VPS without sudo, and the stage prints the owner's command. A pair built earlier can be adopted instead (below); a room build the last release already has must be adopted, never skipped, so promote always has hashes to compare. |
| `draft` | desktop → GitHub | `draft\draft-state.json`, `draft\evidence.json` | The stage creates a draft by API (a pre-release for a beta label such as `0.1.0-beta.9`, a release for a final label such as `0.1.0`) (it keeps the id) and uploads the zip and its `.sha256` to that id. It reads the draft back: tag, title, pre-release or not, notes and both assets by size and SHA-256 digest. It refuses when any other release has the tag or a release as new or newer is published. |
| `promote` | desktop → VPS (read-only), GitHub, git remotes | `promote\state.json`, `promote\downloaded\*`, `promote\evidence.json` | Every run first checks the whole chain again, then rechecks the build from its files, with the zip scanned again. It checks the draft by the recorded id: the only release with the tag, unchanged assets and notes, nothing newer published, no tag elsewhere. Then the server gate: the live VPS must offer the room build. Then the colour pack, if the zip names one, goes to the `colour-packs` release (below). Then it pushes the library (if needed), the private release branch and the public `main`, each only as a fast-forward. It publishes the release by id at the source commit; a final is also made the repository's latest release. Finally it reads the release back, checks that the tag is on the commit, downloads the zip, `.sha256` and the colour pack, and compares them. |

`status` prints where each stage stands and checks every evidence file again.

### How the evidence holds together

- `plan.json` is written once. Every later stage reads it through the ledger (`ledger.jsonl`, append-only), which
  holds the SHA-256 of each evidence file as it was written. Evidence files are read-only.
- Each evidence file names the plan's SHA-256 and id, the source and library commit and tree, the SHA-256 of
  every file it vouches for, and the SHA-256 of each evidence file it relied on. Draft relies on build and rooms;
  promote relies on all three. Every stage verifies that chain before it starts, so a file replaced later no
  longer matches the ledger or the evidence that came after it.
- `-Redo` moves a stage, and every stage that relies on it, to `superseded\<time>\` and records that in the ledger.
  Nothing is overwritten. While a draft made from that work still exists on GitHub, the stage cannot be redone.
- The draft is always addressed by the id the draft stage recorded. A second draft with the same tag (an older
  attempt) stops promote, as does any release as new or newer.
- The zip carries its own receipt (`build-provenance.json`), which promote reads again. The published assets are
  compared with the build's files by GitHub's own digest and by downloading them again.

## Outward steps

Each outward step is logged in `outward.log` (START / DONE / FAILED / SKIPPED). Each can be skipped with
`-Skip <name>` and does nothing in `-DryRun`:

| Step | Stage | What it does |
|---|---|---|
| `vps-stage` | rooms | copies `kit.tar` to `vps:~/ember-sfv-bridge.incoming-<label>.tar`, unpacks and checks it there, keeps the old folder as `~/ember-sfv-bridge.before-<label>` and moves the kit into `~/ember-sfv-bridge` (`scripts/publish/vps.sh`, no sudo) |
| `draft` | draft | creates the draft and uploads the two assets |
| `colours` | promote | uploads the colour pack, when the zip names one, to the `colour-packs` release (made as a pre-release the first time), unless an asset of its name is there already; the hosted asset is checked by size and digest either way. Skipping it leaves `publish` refusing until a run does it |
| `push-library` | promote | fast-forwards the library's `feat/ember-library` to the pinned commit (only when the plan says it is needed) |
| `push-origin` | promote | pushes the source commit to the private origin's release branch (`release/beta.N`, or `release/x.y.z` for a final) |
| `push-public` | promote | fast-forwards the public `main` to the source commit |
| `publish` | promote | publishes the draft by id with `target_commitish` = the source commit |

Pushes are never forced and never carry tags (`--no-follow-tags`). Each push starts only when the branch is
exactly where the plan saw it, and when `git push --dry-run` shows exactly that range (old tip..release commit, or a
new branch). A branch that moved since the plan stops the run, even if the release would still fast-forward it.
`push-public` and `publish` also require the library commit the README pins to be on the library's public branch,
even when `push-library` was skipped. `push-public` comes before `publish` because GitHub creates the tag from the
commit, which has to be in the public repository by then.

Right before `publish`, promote reads the draft again by id and checks that no tag for the label has appeared. If
an earlier run published the release but lost the reply, the next run sees the release published, with the tag on
the planned commit, and goes on to confirm. The server check runs on every promote run until the release is
published.

The pipeline itself is pinned: every stage after the plan refuses unless the pipeline checkout is at the commit
the plan recorded and clean. The plan and promote check that the remotes push where the config says. The public
remote must push to `github.com/<publicRepo>`, the private origin must not push there, and the library remote must
push to `github.com/<libraryGithubRepo>`.

The colour pack (`docs/design/COLOUR_PACK.md`) is off by default: `config.json`'s `colourPack` (a boolean, absent
means false) is the one setting. Off, the build PC's job runs `package.ps1` without `-ColourPack`, so the zip carries
every colour, names no pack, and the `colours` phase is done with nothing to host (a dry run included). On, the job
runs it with `-ColourPack -ColourPackRepo <publicRepo>`, and the build stage refuses a result whose job, zip and
receipt do not all name a pack. The pack is every colour but each costume's first, which the zip then does not carry,
in one archive the launcher downloads on its first start. `publish` refuses until the `colours` phase is done, so
`-Skip colours` can never publish a release whose pack is not hosted. It is named by its SHA-256, so a release whose pictures
did not change makes the same file, finds it on the `colour-packs` release and uploads nothing. That release is a
pre-release without a version (neither the updater nor this pipeline takes it for a release of SFV Ember) and keeps
every pack, so older installs keep finding theirs: delete none of them. Nothing in it is for the owner to do; the
first promote that runs the `colours` phase makes it.

Promote records each phase in `promote\state.json`. After a failure, running it again resumes at the first
unfinished phase. A skipped phase stays open and runs on the next run without `-Skip`.

## Adopting a room-host pair built earlier

When the room hosts were already built, staged and installed for this room build (by hand, or by an earlier run),
a second sudo step for byte-different but equivalent binaries is avoided by adopting them:

```powershell
pwsh scripts\publish.ps1 -Stage rooms -Label <label> -AdoptKit <kit folder> -KitSource <sfv-ember commit> -KitLibrary <library commit>
```

Nothing is built or staged. The kit folder holds `room-build\sfve-room-host`, `room-build\sf4-net` and optionally
`room-build\BUILD_ID` and `SHA256SUMS`. The stage hashes the pair and checks it:

- `BUILD_ID` (when present) and the room build embedded in the room host are the plan's.
- On the build PC's Linux, `build-rooms-linux.sh --check` runs the checks a built pair gets: the host accepts its
  own build and refuses another, the embedded build, and the path scan. The desktop scans both files for forbidden
  strings.
- The kit's commits are ancestors of the plan's commits in both repositories.
- Nothing the two binaries are made from changed between them (`git diff`). The path lists are read from each
  commit pair:
  - sfv-ember: `data/pick-space.json`, `cmake/EmbedTextFile.cmake` (which embeds it), `scripts/build-room-host-linux.sh`, the catalog sources it names, and every
    header they include.
  - library: `server/roomhost/build-linux.sh`, `sources.txt`, the sources it lists for a data catalog, and every
    header included from `src/` or `server/roomhost/`. Also sf4-net's `Cargo.toml`, `Cargo.lock`, `build.rs`,
    `rust-toolchain.toml`, `resource.rc` and `src/`, with its path dependencies (not dev-dependencies).

  A change refuses the adoption and names the path.

The evidence records `adopted`, the kit's commits, both hashes and both path lists. Promote's server check then
compares the installed files with these hashes, as for a built pair.
## Before the first run

`D:\sfv-ember-dist\publish\config.json` holds this PC's paths, outside the repository:

```json
{
  "publicRepo": "Confetti3/SFV-Ember", "publicRemote": "public", "originRemote": "origin",
  "libraryRepo": "<the desktop's library repository>", "libraryRemote": "origin", "libraryBranch": "feat/ember-library",
  "libraryGithubRepo": "Confetti3/SF4-Ember-Netplay",
  "rules": ["<the release rules file>"], "notesForbidden": ["<words public text never uses>"],
  "laptop": { "host": "director", "work": "D:\\sf4\\publish", "interactive": "D:\\sf4\\interactive",
              "sourceCheckout": "D:\\sf4\\wt\\sfv-b9", "libraryCheckout": "D:\\sf4\\wt\\lib-b9",
              "buildDev": "D:\\sfv-b9-build", "buildPlayer": "D:\\sfv-b9-build-player", "vcpkgRoot": "D:\\sf4\\vcpkg",
              "renderStore": "D:\\sfv-ember-renders-b8", "oldPackageCheck": "D:\\keep\\beta6\\sfve_package_check.exe",
              "dist": "D:\\sfv-ember-dist\\publish", "pollSeconds": 60,
              "startTimeoutMinutes": 90, "buildTimeoutMinutes": 300, "roomsTimeoutMinutes": 90 },
  "rooms": { "backend": "wsl", "baseStage": "<the folder staged on the VPS for the last release>", "maxGlibc": "2.43" },
  "vps": { "host": "vps", "stage": "ember-sfv-bridge", "roomsLib": "/usr/local/lib/ember-rooms" }
}
```

A Linux build PC's `laptop` block, and `rooms.backend` `"linux"`:

```json
  "laptop": { "host": "laptop", "os": "linux", "pwsh": "<its pwsh>", "setup": "<a shell file that puts its tools on PATH>",
              "work": "/mnt/games/sfv-publish/work", "sourceCheckout": "/mnt/games/sfv-publish/sfv",
              "libraryCheckout": "/mnt/games/sfv-publish/lib", "buildDev": "/mnt/games/sfv-publish/build-dev",
              "buildPlayer": "/mnt/games/sfv-publish/build-player", "vcpkgRoot": "<its vcpkg>",
              "vcpkgCache": "/mnt/games/sfv-publish/vcpkg-cache", "renderStore": "<its copy of the render store>",
              "oldPackageCheck": "/mnt/games/sfv-publish/keep/beta6/sfve_package_check.exe",
              "discordSdk": "/mnt/games/sfv-publish/deps/DiscordSocialSdk-1.10.19337.zip", "dist": "/mnt/games/sfv-publish/packages",
              "testEmulator": "<ember-wine>", "testPrefix": "/mnt/games/sfv-publish/wine-tests",
              "lock": "/mnt/games/sfv-publish/heavy-job.lock", "jobs": 12, "machineNames": ["<its user>", "<its host name>"],
              "sccache": "<its sccache>", "sccacheDir": "/mnt/games/sfv-publish/cache/sccache", "sccachePort": 4228,
              "pollSeconds": 60, "startTimeoutMinutes": 90, "buildTimeoutMinutes": 300, "roomsTimeoutMinutes": 90 },
```

### The build PC

`laptop.host` names the Windows build PC by its SSH alias. `"local"` makes it this PC: the agent's phases run in a
local PowerShell and its copies are file copies (SfvPublishOperations.ps1), with every check unchanged. A local
build PC needs, in its `interactive` folder, `scripts\publish\local-queue.ps1` copied as `sfv-start.ps1` and a file
`local-queue` holding the Discord SDK archive's path. The queue starts each job detached at below-normal priority
and holds `D:\sf4-build\heavy-build.lock`, which the desktop's other builds take too, so it waits while another
heavy build runs. Its checkouts may be this PC's own worktrees at the plan's commits; they must be clean.

`laptop.os` `"linux"` makes the build PC a Linux PC (the Linux laptop, `ssh laptop`). The same stages and checks run;
what differs on that PC:

- The build is cross-compiled: `scripts\build.ps1` on Linux builds with clang-cl and lld-link against the Microsoft
  CRT and Windows SDK that xwin splats (`cmake/cross/clang-cl-x64.cmake`, from the setup file's `SF4E_XWIN` and
  `SF4E_LLVM_BIN`), its vcpkg packages with the same toolchain (Detours from `cmake/cross/vcpkg-ports`, built with
  CMake), and the helper with lld-link (`scripts\build-helper.ps1`). The tests run through Wine (`testEmulator`,
  `ember-wine`, in the prefix `testPrefix`, with `xinput1_3=n,b` so a test program loads the proxy beside it, as
  Windows does). Nothing in the checkouts is edited: what clang-cl needs is in the source (the library's probe gets
  its clang-only warnings turned off by SFV's CMakeLists.txt; guarded memory accesses fault inside a call of their
  own, which a clang-cl `__try` takes). Every receipt names its toolchain (`toolchain`: host, compiler, linker, CRT
  and SDK, read from the tools and the splat), the helper's record its `rustc`, linker and C compiler; the build
  stage refuses receipts whose toolchain is not the build PC's kind (MSVC on Windows, clang-cl on Linux), is
  missing, or differs between the two builds and the zip.
- Every compile goes through sccache (`laptop.sccache`), as the desktop's go through ccache: a compile is served from
  the cache only when its preprocessed source, compiler and flags are the ones it was made from. The cache has its
  own folder (`laptop.sccacheDir`, outside the home folder) and its own server (`laptop.sccachePort`), so the
  laptop's other builds, which restart the default sccache server, never stop it; `job.sh` stops it at the end.
  The two build trees are its base folders (`SCCACHE_BASEDIRS`): their paths are compared without that prefix, so
  the player build takes the development build's compiles where they are the same (every library unit: SFV's
  CMakeLists.txt sets `SFVE_DEV_BUILD` only on SFV Ember's own targets). vcpkg's port builds go through it too, the
  helper's rustc (`RUSTC_WRAPPER`) and its C compiles, and the room-host pair (below). The receipts name the cache,
  kind and version (`compilerCache`: `"sccache 0.19.0"`, `"ccache 4.14.1"` on Windows), the helper's record its own;
  the build stage refuses a receipt without the field, one whose cache is not the build PC's kind (a Linux build PC
  always uses sccache), or builds that name different caches. Each build step's log ends with the cache's statistics,
  `result.json` has their hits and misses (`cache`), and the stage prints them.
- `scripts\package.ps1` on Linux packages the same files: the checkouts are made with Windows line endings
  (`git config core.autocrlf true` in both; the agent refuses a checkout without it), so their fingerprints and the
  packaged text are the desktop's; the files it writes get CRLF lines and `MANIFEST.txt` its `\` paths; the Windows
  programs it runs (`sfve_package_check.exe`, the earlier beta's) run through Wine. The zip's entries are the same; the
  archive records Unix as their origin, in the order Linux lists the folder.
- Its checkouts, build trees, caches and package folder are outside the home folder (`/mnt/games/sfv-publish`):
  the compiled source paths would carry the user's name. `build.ps1` refuses a path in the home folder.
  `laptop.machineNames` (its user and host name) join the desktop's own names in every scan, and the agent's own scan
  of the package reports the names it looked for, which the desktop adds too.
- The job is `job.sh` in the attempt folder, which the agent's phase `start` starts detached (`setsid`), so the SSH
  call returns. It takes the heavy-job lock (`laptop.lock`, flock) before it builds: `start` answers WAIT while
  another job holds it, `status` reports the PC Running then, and a job that waited two hours for it fails. It
  sources `laptop.setup`, runs niced with `laptop.jobs` parallel jobs, and ends the test prefix's Wine at its exit.
- Rooms (`rooms.backend` `"linux"`) run `build-rooms-linux.sh` on the PC itself. It needs the room host's
  build headers there (`nlohmann-json3-dev`, `libspdlog-dev`, `libfmt-dev`, `libxxhash-dev`), and binaries that run
  on the server's glibc: `result.txt` reports the newest glibc symbol each binary needs, and `rooms.maxGlibc` (the
  server's release, 2.43 for Ubuntu 26.04) refuses a newer one. Its cargo target folder is kept between runs, and
  `sf4-net` and every `sfve-room-host` compile go through the build PC's sccache (`SFVE_SCCACHE`; `CACHE` in
  `result.txt`, which a pair built on a Linux build PC must name); the archives are still the plan's, checked by
  hash, and the pair gets every check.

Setting one up: `git clone` the SFV and library repositories into the two checkout folders, `git config core.autocrlf
true` in each, and check out a commit at or behind the next release's (the agent only fast-forwards); the bundles the
build stage sends bring the rest. Copy the earlier beta's `sfve_package_check.exe` and the Discord SDK archive into
the folders the config names.

`laptop.renderStore` is the render store the package takes its pictures from: a folder holding `pack\manifest.json`
and `pack\assets\selection\...` as `tools/boothpack.py pack` leaves them (`docs/design/beta8/pack.md`). It lives
outside git; the capture's masters stay on the machine that captured. From 0.1.1 it holds every colour (boothpack
1.1, `D:\sfv-ember-renders-011`); 0.1.0's first-colour store is `D:\sfv-ember-renders-b8`.

The rules file is the forbidden-string list kept outside the repository (git filter-repo's replace-text format).
It has to be the same file at every stage: the plan pins its SHA-256.

Fetch first, so the plan can see the remotes' commits: `git fetch public; git fetch origin`, and in the library
repository `git fetch origin`.

## A release from start to finish

```powershell
# 1. Plan (read-only; a dry run first is free)
pwsh scripts\publish.ps1 -Stage plan -Label 0.1.0-beta.9 -Source b9/integrate `
    -Notes <release notes .md> -DiscordDraft <Discord draft .md> -DryRun
pwsh scripts\publish.ps1 -Stage plan -Label 0.1.0-beta.9 -Source b9/integrate `
    -Notes <release notes .md> -DiscordDraft <Discord draft .md>

# 2. Windows build, tests and package on the build PC (waits; run it again to keep waiting)
pwsh scripts\publish.ps1 -Stage build -Label 0.1.0-beta.9

# 3. Linux room hosts, the kit staged on the VPS (prints the owner's command)
pwsh scripts\publish.ps1 -Stage rooms -Label 0.1.0-beta.9

# 4. The owner, on the VPS
ssh -t vps "sudo bash ~/ember-sfv-bridge/upgrade.sh rooms"
ssh -t vps "sudo bash ~/ember-sfv-bridge/upgrade.sh check"

# 5. Draft pre-release (private until published)
pwsh scripts\publish.ps1 -Stage draft -Label 0.1.0-beta.9

# 6. Promote: dry run, then the real one when the owner says "publish"
pwsh scripts\publish.ps1 -Stage promote -Label 0.1.0-beta.9 -DryRun
pwsh scripts\publish.ps1 -Stage promote -Label 0.1.0-beta.9
```

Promote refuses until the VPS offers the room build. Its read-only check (`vps.sh check`, as the operator account)
always compares the installed `sfve-room-host` and `sf4-net` in the build's folder under the room library with the
rooms evidence's SHA-256 (built or adopted), then runs the installed room host on its own build, then reads the
supervisor's `config.json`, which must list the build. If that file is readable only by root, the check says so.
The owner then runs `upgrade.sh check`, sees the build under "SFV builds", and promotes with `-RoomsConfirmed`. The
flag stands in for that last read only: the hash comparison and the room host's answer still have to pass.

### What only the owner does

- Supply the release notes and the Discord draft. Neither mentions the option `notesForbidden` names, and the pipeline never writes
  or posts them.
- Run `sudo bash ~/ember-sfv-bridge/upgrade.sh rooms` and `... check` on the VPS. The pipeline never runs sudo.
- Say "publish" before `-Stage promote` without `-DryRun`.
- Post the Discord announcement.

## Recovering

- A stage that failed: fix the cause and run it again. Finished stages are only checked again.
- A build PC job that failed: `-Stage build -NewAttempt` (a fresh folder on the build PC, a fresh package folder).
- Another commit or other notes: `-Stage plan -Replan`. First delete the draft on GitHub if one exists; the
  pipeline never deletes releases.
- A rebuilt stage after the draft: delete the draft on GitHub, then `-Stage build -Redo` (or `rooms`), then
  `-Stage draft`.
- The room build on the VPS: `sudo bash ~/ember-sfv-bridge/upgrade.sh rollback-rooms`. The previous stage folder
  is kept as `~/ember-sfv-bridge.before-<label>`.

## First real run: what to look at before each irreversible step

- `gh auth status`: the account that owns `Confetti3/SFV-Ember`, with `repo` scope.
- The plan's counts against what you expect: the commits since the last release (71 for beta.9 on 6668eb2), the
  library commits to push (1: 3515bca6f..8f56f3c25), and the room build marked changed.
- After `-Stage draft`: open the draft by the URL it prints and check the tag, title, notes and both assets. Promote
  reads it back by id again.
- Promote runs `git push --dry-run --porcelain` for every push and refuses any range other than the planned one.
  For a manual look first: `git push --dry-run --porcelain --no-follow-tags public <commit>:refs/heads/main`, and
  the same for the library's `feat/ember-library`.
- Run promote once with `-Skip publish`. Check the public `main`, `release/beta.N` on origin and the library tip.
  Then run promote again without it to publish.

## Known limits

- Review findings 12, 14 and 15 of the review of commit 75138f5 are not addressed in this version.
- The `server1` Linux back end is not written; rooms builds in the build PC's WSL or on the Linux build PC, or adopts
  a kit. With `laptop.host` `"local"` that is this PC's own WSL. Its Ubuntu should be the VPS's release (26.04,
  glibc 2.43): the pair links glibc dynamically, so a newer Linux (the Linux laptop's 26.10, glibc 2.44) can make
  binaries the VPS cannot run. `rooms.maxGlibc` refuses those; the Linux laptop also lacks the room host's build
  headers (no system packages there without the owner's sudo), so its rooms stage fails until they are there, and
  adopting a kit or the WSL build stays the way until then.
- The Linux build PC's cross build is not MSVC: the binaries differ from the desktop's (clang-cl's code, Detours built
  with CMake, the CRT and SDK of the xwin splat), and the receipts say so. Its tests ran under Wine, not Windows.
- When the supervisor's `config.json` is root-only, whether it lists the build rests on the owner's
  `-RoomsConfirmed`. The installed hashes and the room host's own answer are still checked by the pipeline.
## Tests

`pwsh -NoProfile -File tests\PublishTests.ps1` and `tests\PublishReviewTests.ps1` (one test per review finding; `-Only <name>`) run every stage on a fixture of local git repositories. GitHub,
the build PC and the VPS are faked (`scripts/publish/SfvPublishFakes.ps1`), and a push to anything outside the
fixture throws. They run on Windows and on Linux (the Linux laptop: `pwsh` from its setup file); on Linux the agent's
own test runs as a Linux build PC's (its job started under a real lock) and the desktop-as-build-PC test is skipped,
on Windows the other way round. The Linux build PC's stages are tested on the fakes on both.
