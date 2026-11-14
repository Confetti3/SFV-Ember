# Publishing an SFV Ember release

`scripts\publish.ps1` publishes a release in five stages. It runs on the desktop (where `gh` is signed in and
`ssh vps` works). The Windows build runs on the build PC over `ssh director`. The Linux room hosts build in the
build PC's WSL. The VPS only receives files: every root step stays with the owner. Nothing becomes public before
the last stage, and the last stage publishes only after it has checked everything again.

## The stages and what each proves

| Stage | Where it runs | What it leaves (state folder) | What it proves |
|---|---|---|---|
| `plan` | desktop, read-only | `plan.json`, `notes.md` | The label is newer than every published release, and no release, draft or tag has it yet. The source commit descends from the last release's tag and fast-forwards the public `main` and the private `release/beta.N`. Every worktree on that commit or branch is clean, and so is the pipeline's own checkout. `CMakeLists.txt` makes this version. `data/pick-space.json`'s room build agrees with `kMatchBuild`, and the plan records whether it changed since the last release. The README's library pin names the library commit, which is on the library's public branch or fast-forwards it. The notes exist and use none of the words `notesForbidden` lists (the option public text never mentions). No forbidden string (the owner's rules file, plus this PC's user, profile and computer names) appears in the notes, the Discord draft, or any commit, tree, blob or path being released, raw or UTF-16. That covers both repositories. Every released commit has the last release's identity and no `...-by:` trailer. |
| `build` | build PC (Interactive task queue) | `build\files\*`, `build\evidence.json` | Both build PC checkouts are fast-forwarded to exactly the plan's commits and trees, are clean, and have their fingerprints recorded before the build. A development build and a player build ran, with their tests (ctest's own count must equal the receipt's list), then the helper and `package.ps1 -VersionLabel <label>`. Back on the desktop: both receipts name the plan's commits, the checkouts' fingerprints, the right kind and passing tests. The fingerprints did not move during the job. The zip, its `.sha256` and the receipt inside it agree with each other. Every packaged binary is the player build's, by hash. `sfv_ember.dll` carries the plan's room build and match line. No file or file name in the package carries a forbidden string or a name of the desktop or the build PC. |
| `rooms` | build PC WSL (`server1` slot not written yet) | `rooms\out\*`, `rooms\kit.tar`, `rooms\evidence.json` | `sf4-net` (`--profile server`) and `sfve-room-host` are built from `git archive`s of the plan's two commits, checked by hash. Both are Linux binaries named in `SHA256SUMS`. The room host answers `helper_unavailable` for its own build and `other_build` for another, and embeds exactly the plan's build. They hold no build-machine path or forbidden string. The kit is the last release's stage folder, checked against its own `SHA256SUMS`, with the library's `upgrade.sh` at the plan's commit and `room-build/` replaced. It is staged on the VPS without sudo, and the stage prints the owner's command. A pair built earlier can be adopted instead (below); a room build the last release already has must be adopted, never skipped, so promote always has hashes to compare. |
| `draft` | desktop → GitHub | `draft\draft-state.json`, `draft\evidence.json` | The stage creates a draft pre-release by API (it keeps the id) and uploads the zip and its `.sha256` to that id. It reads the draft back: tag, title, pre-release, notes and both assets by size and SHA-256 digest. It refuses when any other release has the tag or a release as new or newer is published. |
| `promote` | desktop → VPS (read-only), GitHub, git remotes | `promote\state.json`, `promote\downloaded\*`, `promote\evidence.json` | Every run first checks the whole chain again, then rechecks the build from its files, with the zip scanned again. It checks the draft by the recorded id: the only release with the tag, unchanged assets and notes, nothing newer published, no tag elsewhere. Then the server gate: the live VPS must offer the room build. Then it pushes the library (if needed), the private `release/beta.N` and the public `main`, each only as a fast-forward. It publishes the release by id at the source commit. Finally it reads the release back, checks that the tag is on the commit, downloads the zip and `.sha256`, and compares them. |

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
| `push-library` | promote | fast-forwards the library's `feat/ember-library` to the pinned commit (only when the plan says it is needed) |
| `push-origin` | promote | pushes the source commit to the private origin's `release/beta.N` |
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
  - sfv-ember: `data/pick-space.json`, `scripts/build-room-host-linux.sh`, the catalog sources it names, and every
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
  "rooms": { "backend": "wsl", "baseStage": "<the folder staged on the VPS for the last release>" },
  "vps": { "host": "vps", "stage": "ember-sfv-bridge", "roomsLib": "/usr/local/lib/ember-rooms" }
}
```

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
- The `server1` Linux back end is not written; rooms builds in the build PC's WSL, or adopts a kit.
- When the supervisor's `config.json` is root-only, whether it lists the build rests on the owner's
  `-RoomsConfirmed`. The installed hashes and the room host's own answer are still checked by the pipeline.
## Tests

`pwsh -NoProfile -File tests\PublishTests.ps1` and `tests\PublishReviewTests.ps1` (one test per review finding; `-Only <name>`) run every stage on a fixture of local git repositories. GitHub,
the build PC and the VPS are faked (`scripts/publish/SfvPublishFakes.ps1`), and a push to anything outside the
fixture throws.
