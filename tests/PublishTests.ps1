#Requires -Version 7
# The publish pipeline's tests (scripts/publish.ps1, docs/release/PUBLISHING.md): every stage on a fixture of git
# repositories (real git, local folders only), with GitHub, the build PC and the VPS faked
# (scripts/publish/SfvPublishFakes.ps1). Nothing leaves this PC.
#   pwsh -NoProfile -File tests\PublishTests.ps1
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$scripts = Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\publish'
. (Join-Path $scripts 'SfvPublish.ps1')
. (Join-Path $scripts 'SfvPublishOperations.ps1')
. (Join-Path $scripts 'SfvPublishFakes.ps1')

$script:passed = 0
function Check([bool]$Condition, [string]$Message) { if (!$Condition) { throw "CHECK FAILED: $Message" } }
function Pass([string]$Message) { $script:passed++; Write-Host "PASS $Message" }
function Reject([scriptblock]$Action, [string]$Expected) {
    try { & $Action 6>$null | Out-Null } catch {
        if ($_.Exception.Message -like $Expected) { return }
        throw "Expected a refusal like '$Expected', got: $($_.Exception.Message)"
    }
    throw "Expected a refusal like '$Expected', but it went through."
}
function G([string]$Repo, [string[]]$A) { $out = & git -C $Repo @A 2>&1; if ($LASTEXITCODE) { throw "git $($A -join ' '): $out" }; ($out -join "`n").Trim() }
function Write-File([string]$Path, [string]$Text) { New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null; [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false)) }
function Commit([string]$Repo, [string]$Message) { G $Repo @('add', '-A') | Out-Null; G $Repo @('-c', 'commit.gpgsign=false', 'commit', '-q', '-m', $Message) | Out-Null; G $Repo @('rev-parse', 'HEAD') }

$identity = @{GIT_AUTHOR_NAME = 'Release Bot'; GIT_AUTHOR_EMAIL = 'release@example.invalid'; GIT_COMMITTER_NAME = 'Release Bot'; GIT_COMMITTER_EMAIL = 'release@example.invalid'}
foreach ($k in $identity.Keys) { Set-Item "env:$k" $identity[$k] }
$oldBuild = 'sfv-ember/13173604/0.6/55c1d32e7a18ea97/layout-886b244cba8b2436'
$newBuild = 'sfv-ember/13173604/0.7/55c1d32e7a18ea97/layout-d728d987b541ac1b'

# A fresh world: the SFV and library repositories with their remotes, the previous release published, the rules, the
# notes, the base stage folder, and a context whose operations are the fakes.
function New-Fixture {
    $F = Join-Path ([IO.Path]::GetTempPath()) ("sfv-publish-test-" + [Guid]::NewGuid().ToString('N').Substring(0, 10))
    New-Item -ItemType Directory -Path $F | Out-Null
    $F = (Resolve-Path $F).Path
    foreach ($bare in 'public.git', 'origin.git', 'lib-origin.git') { G $F @('init', '-q', '--bare', $bare) | Out-Null }
    # The library: L1 public, L2 its child (the commit this release builds on).
    $lib = Join-Path $F 'lib'
    G $F @('init', '-q', '-b', 'feat/ember-library', 'lib') | Out-Null
    Write-File "$lib\src\core.txt" 'library one'
    Write-File "$lib\server\sfv-bridge\upgrade.sh" "#!/usr/bin/env bash`necho upgrade v1`n"
    Write-File "$lib\server\roomhost\sources.txt" "# [host]`nserver/roomhost/main.cxx`n`n# [host_windows]`nserver/roomhost/Win.cxx`n`n# [catalog]`nsrc/common/Sf4Catalog.cxx`n"
    Write-File "$lib\server\roomhost\build-linux.sh" "#!/usr/bin/env bash`n"
    Write-File "$lib\server\roomhost\main.cxx" "#include `"RoomHost.hxx`"`n#include <vector>`n"
    Write-File "$lib\server\roomhost\RoomHost.hxx" "#include `"common/Catalog.hxx`"`n"
    Write-File "$lib\server\roomhost\Win.cxx" 'windows only'
    Write-File "$lib\src\common\Catalog.hxx" 'catalog'
    Write-File "$lib\src\common\Sf4Catalog.cxx" 'sf4 only'
    Write-File "$lib\rust\sf4-net\Cargo.toml" "[package]`nname = `"sf4-net`"`n[dependencies]`nember-protocol = { path = `"../../server/ember/protocol`" }`n[dev-dependencies]`nember-bridge = { path = `"../../server/ember/bridge`" }`n"
    Write-File "$lib\rust\sf4-net\Cargo.lock" 'lock'
    Write-File "$lib\rust\sf4-net\src\main.rs" 'fn main() {}'
    Write-File "$lib\server\ember\protocol\Cargo.toml" "[package]`n"
    Write-File "$lib\server\ember\protocol\src\lib.rs" 'protocol'
    Write-File "$lib\server\ember\bridge\src\lib.rs" 'bridge'
    $L1 = Commit $lib 'library one'
    G $lib @('remote', 'add', 'origin', "$F\lib-origin.git") | Out-Null
    G $lib @('push', '-q', 'origin', 'feat/ember-library') | Out-Null
    Write-File "$lib\src\core.txt" 'library two'
    Write-File "$lib\server\sfv-bridge\upgrade.sh" "#!/usr/bin/env bash`necho upgrade v2`n"
    $L2 = Commit $lib 'library two'
    # SFV: C0 is the previous release (beta.8), C1 the release.
    $sfv = Join-Path $F 'sfv'
    G $F @('init', '-q', '-b', 'b9/integrate', 'sfv') | Out-Null
    Write-File "$sfv\CMakeLists.txt" "project(sfv_ember VERSION 0.1.0 LANGUAGES CXX)`nset(SFVE_VERSION `"`${PROJECT_VERSION}-beta.8`")`n"
    Write-File "$sfv\data\pick-space.json" "{`n`t`"build`": `"$oldBuild`"`n}`n"
    Write-File "$sfv\src\ember\SfvPickSpace.hxx" "constexpr const char* kMatchBuild = `"sfv-ember/13173604/0.6`";`n"
    Write-File "$sfv\README.md" "This release builds against library commit ``$($L1.Substring(0, 9))``.`n"
    Write-File "$sfv\scripts\build-room-host-linux.sh" "#!/usr/bin/env bash`nCATALOG_SOURCES=`"`$SFV/src/roomhost/SfvHostCatalog.cxx `$SFV/src/ember/SfvPickSpace.cxx `$GEN/SfvHostPickSpace.cxx`"`n"
    Write-File "$sfv\src\roomhost\SfvHostCatalog.cxx" "#include `"ember/SfvPickSpace.hxx`"`n#include `"RoomHost.hxx`"`n"
    Write-File "$sfv\src\ember\SfvPickSpace.cxx" "#include `"ember/SfvPickSpace.hxx`"`n#include `"common/Catalog.hxx`"`n"
    $C0 = Commit $sfv '0.1.0-beta.8'
    G $sfv @('remote', 'add', 'public', "$F\public.git") | Out-Null
    G $sfv @('remote', 'add', 'origin', "$F\origin.git") | Out-Null
    G $sfv @('push', '-q', 'public', "${C0}:refs/heads/main", "${C0}:refs/tags/v0.1.0-beta.8") | Out-Null
    G $sfv @('push', '-q', 'origin', "${C0}:refs/heads/release/beta.8") | Out-Null
    Write-File "$sfv\CMakeLists.txt" "project(sfv_ember VERSION 0.1.0 LANGUAGES CXX)`nset(SFVE_VERSION `"`${PROJECT_VERSION}-beta.9`")`n"
    Write-File "$sfv\data\pick-space.json" "{`n`t`"build`": `"$newBuild`"`n}`n"
    Write-File "$sfv\src\ember\SfvPickSpace.hxx" "constexpr const char* kMatchBuild = `"sfv-ember/13173604/0.7`";`n"
    Write-File "$sfv\src\feature.txt" 'new work'
    $K = Commit $sfv 'beta.9 work'
    Write-File "$sfv\README.md" "This release builds against library commit ``$($L2.Substring(0, 9))``.`n"
    $C1 = Commit $sfv '0.1.0-beta.9; README pins the library'
    # The owner's files.
    Write-File "$F\rules.txt" "regex:Forb[i]dden ?Name==>REMOVED`nliteral:SecretWord==>REMOVED`n"
    Write-File "$F\notes.md" "Ninth beta.`n`n### New`n- Faster rollbacks.`n"
    $base = Join-Path $F 'base\ember-sfv-bridge'
    Write-File "$base\upgrade.sh" "#!/usr/bin/env bash`necho upgrade v1`n"
    Write-File "$base\ember-short\site\assets\site.js" 'live site'
    Write-File "$base\room-build\BUILD_ID" "$oldBuild`n"
    Write-File "$base\room-build\sfve-room-host" 'old host'
    Write-File "$base\room-build\sf4-net" 'old helper'
    $paths = [string[]]@(Get-ChildItem -LiteralPath $base -Recurse -File | ForEach-Object { $_.FullName.Substring($base.Length + 1).Replace('\', '/') })
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    Write-File "$base\SHA256SUMS" ((@($paths | ForEach-Object { "$((Get-FileHash -LiteralPath (Join-Path $base $_) -Algorithm SHA256).Hash.ToLowerInvariant())  $_" }) -join "`n") + "`n")

    $world = New-FakeWorld $F
    $null = Add-FakeRelease $world 'v0.1.0-beta.7' $false 'SFV Ember 0.1.0 Beta 7'
    $null = Add-FakeRelease $world 'v0.1.0-beta.8' $false 'SFV Ember 0.1.0 Beta 8'
    $config = [pscustomobject]@{publicRepo = 'Owner/SFV-Ember'; publicRemote = 'public'; originRemote = 'origin'; libraryRepo = $lib; libraryRemote = 'origin'
        libraryBranch = 'feat/ember-library'; rules = @("$F\rules.txt"); notesForbidden = @('hidden')
        laptop = [pscustomobject]@{host = 'laptop'; work = 'D:\sf4\publish'; interactive = 'D:\sf4\interactive'; sourceCheckout = 'D:\sf4\wt\sfv'; libraryCheckout = 'D:\sf4\wt\lib'
            buildDev = 'D:\b\dev'; buildPlayer = 'D:\b\player'; vcpkgRoot = 'D:\vcpkg'; renderStore = 'D:\renders'; oldPackageCheck = ''; dist = 'D:\dist'
            pollSeconds = 30; startTimeoutMinutes = 10; buildTimeoutMinutes = 60; roomsTimeoutMinutes = 30}
        rooms = [pscustomobject]@{backend = 'wsl'; baseStage = (Split-Path -Parent $base) + '\ember-sfv-bridge'}
        vps = [pscustomobject]@{host = 'vps'; stage = 'ember-sfv-bridge'; roomsLib = '/usr/local/lib/ember-rooms'}}
    $ctx = [pscustomobject]@{Label = '0.1.0-beta.9'; Root = "$F\state\0.1.0-beta.9"; Config = $config; Ops = (New-FakeOperations $world "$F\public.git"); DryRun = $false; Skip = @()
        Repo = $sfv; PipelineRoot = $sfv; LibraryRepo = $lib; AgentPath = (Join-Path $scripts 'laptop-agent.ps1'); RoomScriptPath = (Join-Path $scripts 'build-rooms-linux.sh')
        VpsStagePath = (Join-Path $scripts 'vps.sh')}
    New-Item -ItemType Directory -Path $ctx.Root -Force | Out-Null
    [pscustomobject]@{F = $F; Ctx = $ctx; World = $world; Sfv = $sfv; Lib = $lib; C0 = $C0; C1 = $C1; K = $K; L1 = $L1; L2 = $L2
        P = @{Source = 'b9/integrate'; Notes = "$F\notes.md"; Rules = @("$F\rules.txt"); RoomsConfirmed = $false}}
}
# A hand-built kit: room-build/ with the pair and BUILD_ID, SHA256SUMS beside it.
function New-Kit($X, [string]$Build, [string]$Embedded = '') {
    $kit = Join-Path $X.F "kit-$([Guid]::NewGuid().ToString('N').Substring(0, 6))"
    if (!$Embedded) { $Embedded = $Build }
    New-Item -ItemType Directory -Path "$kit\room-build" -Force | Out-Null
    [IO.File]::WriteAllText("$kit\room-build\sfve-room-host", "`u{7f}ELF hand-built room host $Embedded", [Text.Encoding]::Latin1)
    [IO.File]::WriteAllText("$kit\room-build\sf4-net", "`u{7f}ELF hand-built helper", [Text.Encoding]::Latin1)
    Write-File "$kit\room-build\BUILD_ID" "$Build`n"
    Write-File "$kit\SHA256SUMS" ((@('sf4-net', 'sfve-room-host', 'BUILD_ID') | ForEach-Object { "$((Get-FileHash "$kit\room-build\$_" -Algorithm SHA256).Hash.ToLowerInvariant())  room-build/$_" }) -join "`n")
    $kit
}
function Adopt($X, [string]$Kit, [string]$Source, [string]$Library) { $p = $X.P.Clone(); $p.AdoptKit = $Kit; $p.KitSource = $Source; $p.KitLibrary = $Library; Invoke-PubRooms $X.Ctx $p 6>$null }
function Invoke-Quiet([scriptblock]$Action) { & $Action 6>$null | Out-Null }
function Plan($X) { $r = Invoke-PubPlan $X.Ctx $X.P 6>$null; $X.World.Plan = (Read-PubPlan $X.Ctx).plan; $r }
function Through($X, [string]$Last) {
    $null = Plan $X
    foreach ($stage in 'build', 'rooms', 'draft', 'promote') {
        switch ($stage) { 'build' { Invoke-Quiet { Invoke-PubBuild $X.Ctx $X.P } } 'rooms' { Invoke-Quiet { Invoke-PubRooms $X.Ctx $X.P } }
            'draft' { Invoke-Quiet { Invoke-PubDraft $X.Ctx $X.P } } 'promote' { Invoke-Quiet { Invoke-PubPromote $X.Ctx $X.P } } }
        if ($stage -ceq $Last) { return }
    }
}
$fixtures = [Collections.Generic.List[string]]::new()
function Fresh { $x = New-Fixture; $fixtures.Add($x.F); $x }

try {
    # --- labels, tags, rules -------------------------------------------------------------------------------------
    $l = ConvertFrom-PubLabel '0.1.0-beta.9'
    Check ($l.tag -ceq 'v0.1.0-beta.9' -and $l.releaseBranch -ceq 'release/beta.9' -and $l.title -ceq 'SFV Ember 0.1.0 Beta 9' -and $l.zip -ceq 'sfv-ember-0.1.0-beta.9.zip') 'label'
    Reject { ConvertFrom-PubLabel '0.1.0-rc1' } 'Not a beta label*'
    Check ((Compare-PubTag 'v0.1.0-beta.10' 'v0.1.0-beta.9') -eq 1 -and (Compare-PubTag 'v0.1.0' 'v0.1.0-beta.9') -eq 1 -and (Compare-PubTag 'v0.1.0-beta.8' 'v0.1.0-beta.8') -eq 0) 'tag order'
    Pass 'labels and tag order (beta.10 after beta.9, the final after its betas)'

    $x = Fresh
    $rules = Read-PubRules @("$($x.F)\rules.txt")
    Check (@(Find-PubHits $rules ([Text.Encoding]::UTF8.GetBytes('a ForbiddenName b')) 't').Count -ge 1) 'raw regex hit'
    Check (@(Find-PubHits $rules ([Text.Encoding]::Unicode.GetBytes('xx SecretWord')) 't').Count -ge 1) 'UTF-16 hit'
    Check (@(Find-PubHits $rules ([byte[]](@(0) + [Text.Encoding]::Unicode.GetBytes('SecretWord'))) 't').Count -ge 1) 'UTF-16 hit at an odd offset'
    Check (@(Find-PubHits $rules ([Text.Encoding]::UTF8.GetBytes('nothing here')) 't').Count -eq 0) 'no false hit'
    try { Assert-PubNoHits (Find-PubHits $rules ([Text.Encoding]::UTF8.GetBytes('SecretWord')) 'where') 'Text'; throw 'no refusal' }
    catch { Check ($_.Exception.Message -like '*rules.txt#2*' -and $_.Exception.Message -notlike '*SecretWord*') 'a hit names the rule, never its text' }
    Reject { Read-PubRules @() } 'Pass the rules file*'
    Pass 'rules: regex and literal lines, raw and UTF-16 at both alignments; a refusal never repeats the forbidden text'

    # --- plan ------------------------------------------------------------------------------------------------------
    $null = Plan $x
    $plan = (Read-PubPlan $x.Ctx).plan
    Check ($plan.source.commit -ceq $x.C1 -and $plan.previous.commit -ceq $x.C0 -and $plan.previous.tag -ceq 'v0.1.0-beta.8') 'plan source and previous'
    Check ($plan.library.commit -ceq $x.L2 -and $plan.library.needsPush -eq $true -and $plan.roomBuild.id -ceq $newBuild -and $plan.roomBuild.changed -eq $true) 'plan library and room build'
    Check ($plan.roomBuild.layoutId -ceq 'd728d987b541ac1b' -and $plan.roomBuild.matchLine -ceq 'sfv-ember/13173604/0.7') 'room build parts'
    Check ((Get-PubLedgerSha $x.Ctx 'plan') -ceq (Get-PubSha256 "$($x.Ctx.Root)\plan.json")) 'ledger has the plan'
    Check ((Get-Item "$($x.Ctx.Root)\plan.json").Attributes.HasFlag([IO.FileAttributes]::ReadOnly)) 'plan.json is read-only'
    Pass 'plan: commit, tree, previous release, library to push, room build and layout id; plan.json in the ledger, read-only'
    $before = Get-PubSha256 "$($x.Ctx.Root)\plan.json"
    $null = Plan $x
    Check ((Get-PubSha256 "$($x.Ctx.Root)\plan.json") -ceq $before) 'plan stands'
    Write-File "$($x.F)\notes2.md" 'Other notes.'
    Reject { $p = $x.P.Clone(); $p.Notes = "$($x.F)\notes2.md"; Invoke-PubPlan $x.Ctx $p 6>$null } '*exists with other inputs*'
    Pass 'plan: made once; the same inputs leave it as it is, other inputs need -Replan'

    # plan refusals, each on its own fixture
    $y = Fresh; Write-File "$($y.F)\notes.md" 'Ninth beta. Try the Hidden option.'
    Reject { Plan $y } '*mentions a word config.json''s notesForbidden lists*'
    $y = Fresh; Write-File "$($y.F)\notes.md" 'Thanks ForbiddenName.'
    Reject { Plan $y } '*release notes carries forbidden strings*'
    $y = Fresh; $p = $y.P.Clone(); $p.Notes = ''
    Reject { Invoke-PubPlan $y.Ctx $p 6>$null } '*Pass the release notes file*'
    Pass 'plan: refuses notes that are missing, mention a never-mentioned word, or carry a forbidden string'

    $y = Fresh; Write-File "$($y.Sfv)\docs\x.md" 'ok'; $null = Commit $y.Sfv 'note from ForbiddenName'
    Reject { Plan $y } '*source being released carries forbidden strings*'
    $y = Fresh; [IO.File]::WriteAllBytes("$($y.Sfv)\data\blob.bin", [byte[]](@(1, 2, 3) + [Text.Encoding]::Unicode.GetBytes('SecretWord'))); $null = Commit $y.Sfv 'data'
    Reject { Plan $y } '*source being released carries forbidden strings*'
    $y = Fresh; Write-File "$($y.Sfv)\docs\SecretWord.md" 'x'; $null = Commit $y.Sfv 'doc'
    Reject { Plan $y } '*source being released carries forbidden strings*'
    $y = Fresh; Write-File "$($y.Sfv)\docs\y.md" 'y'; $null = Commit $y.Sfv "change`n`nReviewed-by: Someone <s@example.invalid>"
    Reject { Plan $y } '*carries a trailer line*'
    $y = Fresh; Write-File "$($y.Sfv)\docs\z.md" 'z'
    G $y.Sfv @('add', '-A') | Out-Null; G $y.Sfv @('-c', 'commit.gpgsign=false', '-c', 'user.name=Other', 'commit', '-q', '--author', 'Other Person <other@example.invalid>', '-m', 'z') | Out-Null
    Reject { Plan $y } '*identity that is not the release identity*'
    Pass 'plan: refuses a forbidden string in a commit message, a blob (UTF-16 too) or a path, a trailer line, a foreign identity'

    $y = Fresh; Write-File "$($y.Sfv)\src\feature.txt" 'dirty'
    Reject { Plan $y } '*has uncommitted changes*'
    $y = Fresh; Write-File "$($y.Sfv)\CMakeLists.txt" "project(sfv_ember VERSION 0.1.0 LANGUAGES CXX)`nset(SFVE_VERSION `"`${PROJECT_VERSION}-beta.10`")`n"; $null = Commit $y.Sfv 'v'
    Reject { Plan $y } '*makes version*'
    $y = Fresh; Write-File "$($y.Sfv)\src\ember\SfvPickSpace.hxx" "constexpr const char* kMatchBuild = `"sfv-ember/13173604/0.8`";`n"; $null = Commit $y.Sfv 'm'
    Reject { Plan $y } '*kMatchBuild is not*'
    $y = Fresh; $p = $y.P.Clone(); $p.Library = $y.L1
    Reject { Invoke-PubPlan $y.Ctx $p 6>$null } '*README.md pins library*'
    $y = Fresh; G $y.Sfv @('checkout', '-q', '-b', 'side', $y.C0) | Out-Null; G $y.Sfv @('reset', '-q', '--hard', "$($y.C0)~0") | Out-Null
    G $y.Sfv @('-c', 'commit.gpgsign=false', 'commit', '-q', '--amend', '-m', 'rewritten beta.8') | Out-Null
    $p = $y.P.Clone(); $p.Source = 'side'
    Reject { Invoke-PubPlan $y.Ctx $p 6>$null } '*does not descend from the last release*'
    $y = Fresh; $null = Add-FakeRelease $y.World 'v0.1.0-beta.9' $true 'SFV Ember 0.1.0 Beta 9'
    Reject { Plan $y } '*already has a release or draft named*'
    $y = Fresh; $null = Add-FakeRelease $y.World 'v0.1.0-beta.10' $false
    Reject { Plan $y } '*is not newer than the newest published release*'
    Pass 'plan: refuses a dirty worktree, a wrong version, a pick space that disagrees with kMatchBuild, a library other than the README pin, a source off the last release, an existing draft, a newer release'

    # --- build -----------------------------------------------------------------------------------------------------
    Invoke-Quiet { Invoke-PubBuild $x.Ctx $x.P }
    $build = Read-PubEvidence $x.Ctx 'build' (Read-PubPlan $x.Ctx)
    Check ($build.facts.zipSha256 -cmatch '^[0-9a-f]{64}$' -and $build.facts.tests -eq 3) 'build facts'
    Check (@($x.World.Commands | Where-Object { $_ -like 'scp*source.bundle*' }).Count -eq 1) 'source bundle sent'
    Check ((G $x.Sfv @('rev-parse', 'refs/sfv-publish/0.1.0-beta.9/source')) -ceq $x.C1) 'bundle ref is the plan commit'
    $commandsBefore = $x.World.Commands.Count
    Invoke-Quiet { Invoke-PubBuild $x.Ctx $x.P }
    Check ($x.World.Commands.Count -eq $commandsBefore) 'a finished build is not run again'
    Pass 'build: syncs the plan''s commits, runs the job through the queue, checks what comes back; a re-run only checks'

    $cases = @(
        @({ param($f) $f.sourceRevision = 'f' * 40 }, '*receipt is for source*'),
        @({ param($f) $f.sourceFingerprint = 'D' * 64 }, '*fingerprints are not those*'),
        @({ param($f) $f.resultFingerprint = 'E' * 64 }, '*changed between the sync and the end*'),
        @({ param($f) $f.ctestFailed = 1 }, '*tests did not all run and pass*'),
        @({ param($f) $f.sidecarSha = 'f' * 64 }, '*.sha256 does not name the zip*'),
        @({ param($f) $f.zipShaOverride = 'e' * 64 }, '*not the one the laptop packaged*'),
        @({ param($f) $f.dll = 'MZ a mod without the build' }, '*does not carry the plan''s room build*'),
        @({ param($f) $f.extra = 'thanks to ForbiddenName' }, '*package carries forbidden strings*'),
        @({ param($f) $f.machineScan = @(@{file = 'Launcher.exe'; hits = 2}) }, '*laptop''s own names appear*'),
        @({ param($f) $f.packagedOverride = { param($r) $r.package = @{label = '0.1.0-beta.8'} } }, '*zip''s receipt is not a player build*'),
        @({ param($f) $f.version = '0.1.0-beta.8' }, '*is not SFV Ember 0.1.0-beta.9*')
    )
    foreach ($case in $cases) {
        $y = Fresh; $null = Plan $y; $y.World.BuildTweak = $case[0]
        Reject { Invoke-PubBuild $y.Ctx $y.P } $case[1]
        Check (!(Test-Path "$($y.Ctx.Root)\build\evidence.json")) 'no evidence after a refusal'
    }
    Pass "build: refuses $($cases.Count) kinds of wrong result (other commit or fingerprint, failed tests, sidecar, zip, room build, forbidden or machine names in the package, receipt, version)"
    $y = Fresh; $null = Plan $y; $y.World.LaptopBusy = 'sfv-b9-roomhost'
    Reject { Invoke-PubBuild $y.Ctx $y.P } '*one heavy build at a time*'
    $y = Fresh; $null = Plan $y; $y.World.JobFails = $true
    Reject { Invoke-PubBuild $y.Ctx $y.P } '*laptop build failed*'
    $y = Fresh; $null = Plan $y; $p = $y.P.Clone(); $p.Rules = @("$($y.F)\notes.md")
    Reject { Invoke-PubBuild $y.Ctx $p } '*not the ones the plan was made with*'
    Pass 'build: refuses a busy build PC, a failed job, and rules files other than the plan''s'

    # --- rooms -----------------------------------------------------------------------------------------------------
    Invoke-Quiet { Invoke-PubRooms $x.Ctx $x.P }
    $rooms = Read-PubEvidence $x.Ctx 'rooms' (Read-PubPlan $x.Ctx)
    $sums = Get-Content "$($x.Ctx.Root)\rooms\kit\ember-sfv-bridge\SHA256SUMS"
    Check ($sums -contains "$($rooms.facts.'sfve-room-host')  room-build/sfve-room-host") 'kit sums name the new room host'
    Check ((Get-Content "$($x.Ctx.Root)\rooms\kit\ember-sfv-bridge\room-build\BUILD_ID").Trim() -ceq $newBuild) 'kit BUILD_ID'
    Check ((Get-Content "$($x.Ctx.Root)\rooms\kit\ember-sfv-bridge\upgrade.sh" -Raw) -match 'upgrade v2') 'kit upgrade.sh from the planned library commit'
    Check ((Get-Content "$($x.Ctx.Root)\rooms\kit\ember-sfv-bridge\ember-short\site\assets\site.js" -Raw) -ceq 'live site') 'kit keeps the live site files'
    $paths = @($sums | ForEach-Object { $_.Substring(66) }); $sorted = [string[]]$paths.Clone(); [Array]::Sort($sorted, [StringComparer]::Ordinal)
    Check (($paths -join '|') -ceq ($sorted -join '|')) 'SHA256SUMS in byte order'
    Check ($x.World.VpsStaged -ceq $rooms.facts.kit.sums) 'kit staged on the VPS'
    Check (!@($x.World.Commands | Where-Object { $_ -match 'sudo' -and $_ -like 'ssh*' }).Count) 'the pipeline never runs sudo'
    Pass 'rooms: the pair built from the plan''s archives, checked, the kit made from the base stage with the new room build and staged; no sudo'
    foreach ($case in @(@({ param($r) $r.own = 'other_build' }, '*for its own build*'), @({ param($r) $r.other = 'ok' }, '*for another build*'),
                        @({ param($r) $r.embedded = $oldBuild }, '*embeds*'), @({ param($r) $r.paths = '3' }, '*build-machine paths*'),
                        @({ param($r) $r.hostBytes = "`u{7f}ELF host SecretWord" }, '*Linux sfve-room-host carries forbidden*'), @({ param($r) $r.hostBytes = 'MZ windows' }, '*not a Linux binary*'))) {
        $y = Fresh; $null = Plan $y; $y.World.RoomsTweak = $case[0]
        Reject { Invoke-PubRooms $y.Ctx $y.P } $case[1]
    }
    Pass 'rooms: refuses a room host that refuses its own build, takes another, embeds another, carries paths or forbidden strings, or is not Linux'
    $y = Fresh; $p = $y.P.Clone(); $null = Plan $y; $y.Ctx.Skip = @('vps-stage')
    Invoke-Quiet { Invoke-PubRooms $y.Ctx $p }
    Check (!$y.World.VpsStaged -and (Get-Content "$($y.Ctx.Root)\outward.log" -Raw) -match 'vps-stage SKIPPED') 'skip logged'
    $y.Ctx.Skip = @(); Invoke-Quiet { Invoke-PubRooms $y.Ctx $p }
    Check ([bool]$y.World.VpsStaged) 'a re-run stages what was skipped'
    Pass 'rooms: -Skip vps-stage is logged; a later run stages the kit without building again'

    # --- draft -----------------------------------------------------------------------------------------------------
    Invoke-Quiet { Invoke-PubDraft $x.Ctx $x.P }
    $draft = Read-PubEvidence $x.Ctx 'draft' (Read-PubPlan $x.Ctx)
    $release = Get-FakeRelease $x.World $draft.facts.releaseId
    Check ($release.draft -and $release.prerelease -and $release.tag_name -ceq 'v0.1.0-beta.9' -and $release.name -ceq 'SFV Ember 0.1.0 Beta 9' -and @($release.assets).Count -eq 2) 'draft shape'
    Check ($draft.inputs.build -ceq $build.sha256 -and $draft.inputs.rooms -ceq $rooms.sha256) 'draft names its inputs'
    Pass 'draft: a draft pre-release by id from the checked zip and sidecar, its evidence naming the build and rooms evidence'

    # tampering with evidence or files, on copies of the finished state
    $y = Fresh; Through $y 'draft'
    (Get-Item "$($y.Ctx.Root)\build\files\result.json").Attributes = 'Normal'; Add-Content "$($y.Ctx.Root)\build\files\result.json" ' '
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*changed since its evidence was written*'
    $y = Fresh; Through $y 'draft'
    $e = "$($y.Ctx.Root)\build\evidence.json"; (Get-Item $e).Attributes = 'Normal'; Write-File $e ((Get-Content $e -Raw).Replace('"tests": 3', '"tests": 4'))
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*not the file the build stage wrote*'
    $y = Fresh; Through $y 'draft'
    $e = "$($y.Ctx.Root)\plan.json"; (Get-Item $e).Attributes = 'Normal'; Write-File $e ((Get-Content $e -Raw).Replace('"needsPush": true', '"needsPush": false'))
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*plan.json is not the file the plan stage wrote*'
    $y = Fresh; Through $y 'draft'
    Write-File "$($y.Ctx.Root)\notes.md" 'changed notes'
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*notes.md is not the notes file the plan pinned*'
    Pass 'evidence: a file changed after its stage, an evidence rewritten, a plan rewritten or other notes are all refused'

    # a build redone after the draft: the old draft's evidence no longer chains
    $y = Fresh; Through $y 'draft'
    Reject { Invoke-PubBuild $y.Ctx (@{} + $y.P + @{Redo = $true}) 6>$null } '*still exists*'
    Pass 'redo: a stage is not redone while the draft made from it is still on GitHub'
    $y = Fresh; Through $y 'rooms'
    Invoke-Quiet { Invoke-PubBuild $y.Ctx (@{} + $y.P + @{Redo = $true; NewAttempt = $true}) }
    Check ((Get-PubLedger $y.Ctx | Where-Object { $_.superseded -and $_.stage -ceq 'build' }).Count -eq 1) 'superseded in the ledger'
    Check (Test-Path "$($y.Ctx.Root)\rooms\evidence.json") 'rooms evidence kept: it does not depend on the build'
    Check (@(Get-ChildItem "$($y.Ctx.Root)\superseded" -Recurse -Filter evidence.json).Count -eq 1) 'old build evidence moved aside'
    Pass 'redo: -Redo build moves the build (and what relies on it) aside, records it in the ledger, keeps the rooms stage'

    # draft refusals
    $y = Fresh; Through $y 'rooms'; $null = Add-FakeRelease $y.World 'v0.1.0-beta.9' $true 'old draft'
    Reject { Invoke-PubDraft $y.Ctx $y.P } '*that this stage did not make*'
    $y = Fresh; Through $y 'rooms'
    $y.Ctx.DryRun = $true; Invoke-Quiet { Invoke-PubDraft $y.Ctx $y.P }; $y.Ctx.DryRun = $false
    Check (!@($y.World.Releases | Where-Object { $_.tag_name -ceq 'v0.1.0-beta.9' }).Count -and !(Test-Path "$($y.Ctx.Root)\draft\evidence.json")) 'dry run makes no draft'
    Pass 'draft: refuses when another release has the tag; a dry run creates nothing'
    # a draft interrupted after it was created resumes its uploads
    $y = Fresh; Through $y 'rooms'
    $script:realGh = $y.Ctx.Ops.Gh; $script:failUpload = $true
    $y.Ctx.Ops.Gh = { param($A) if ($script:failUpload -and ($A -join ' ') -like '*uploads.github.com*') { return ConvertTo-FakeResult '' 1 'network' }; & $script:realGh $A }
    Reject { Invoke-PubDraft $y.Ctx $y.P 6>$null } '*failed*'
    $script:failUpload = $false; $y.Ctx.Ops.Gh = $script:realGh
    Invoke-Quiet { Invoke-PubDraft $y.Ctx $y.P }
    Check (@($y.World.Releases | Where-Object { $_.tag_name -ceq 'v0.1.0-beta.9' }).Count -eq 1) 'one draft only'
    Pass 'draft: interrupted after the draft exists, a re-run uploads to the same draft by id'

    # --- promote ---------------------------------------------------------------------------------------------------
    $y = Fresh; Through $y 'draft'; $y.World.VpsCheck = 'not-installed'
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*does not offer*'
    Check (!$y.World.Pushes.Count) 'nothing pushed before the server offers the build'
    $y.World.VpsCheck = 'installed-config-unreadable'
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*needs root to read*'
    Pass 'promote: refuses until the live server offers the room build; nothing is pushed before'

    $y = Fresh; Through $y 'draft'; $y.Ctx.DryRun = $true
    Invoke-Quiet { Invoke-PubPromote $y.Ctx $y.P }; $y.Ctx.DryRun = $false
    Check (!$y.World.Pushes.Count -and (Get-FakeRelease $y.World (Read-PubJson "$($y.Ctx.Root)\draft\evidence.json").facts.releaseId).draft) 'dry run'
    Pass 'promote: a dry run checks everything and pushes and publishes nothing'

    Invoke-Quiet { Invoke-PubPromote $x.Ctx $x.P }
    $promote = Read-PubEvidence $x.Ctx 'promote' (Read-PubPlan $x.Ctx)
    $release = Get-FakeRelease $x.World $draft.facts.releaseId
    Check (!$release.draft -and $release.target_commitish -ceq $x.C1) 'published at the commit'
    Check ((G "$($x.F)\public.git" @('rev-parse', 'refs/tags/v0.1.0-beta.9')) -ceq $x.C1 -and (G "$($x.F)\public.git" @('rev-parse', 'main')) -ceq $x.C1) 'public tag and main'
    Check ((G "$($x.F)\origin.git" @('rev-parse', 'release/beta.9')) -ceq $x.C1) 'origin release branch'
    Check ((G "$($x.F)\lib-origin.git" @('rev-parse', 'feat/ember-library')) -ceq $x.L2) 'library pushed'
    Check (($x.World.Pushes -join ';') -match '^origin refs/heads/feat/ember-library.*;origin refs/heads/release/beta.9.*;public refs/heads/main') 'push order: library, origin, public'
    Check ($promote.facts.zipSha256 -ceq $build.facts.zipSha256) 'downloaded zip compared'
    $pushes = $x.World.Pushes.Count
    Invoke-Quiet { Invoke-PubPromote $x.Ctx $x.P }
    Check ($x.World.Pushes.Count -eq $pushes) 'a finished promote does nothing again'
    Pass 'promote: library, private release branch and public main pushed as fast-forwards, the draft published by id at the commit, the zip downloaded and compared'

    # interrupted at publish: the re-run resumes without pushing again
    $y = Fresh; Through $y 'draft'; $y.World.FailPatch = $true
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*failed*'
    $state = Read-PubJson "$($y.Ctx.Root)\promote\state.json"
    Check ($state.phases.'push-public'.status -ceq 'done' -and !$state.phases.publish) 'state after the failure'
    $pushes = $y.World.Pushes.Count; $y.World.FailPatch = $false
    Invoke-Quiet { Invoke-PubPromote $y.Ctx $y.P }
    Check ($y.World.Pushes.Count -eq $pushes -and (Test-Path "$($y.Ctx.Root)\promote\evidence.json")) 'resumed'
    Pass 'promote: a failure at publish leaves the pushes recorded; the re-run publishes without pushing again'

    # skipped outward steps
    $y = Fresh; Through $y 'draft'; $y.Ctx.Skip = @('push-public')
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*does not contain*'
    Check ((Get-Content "$($y.Ctx.Root)\outward.log" -Raw) -match 'push-public SKIPPED') 'skip logged'
    $y.Ctx.Skip = @('publish'); Invoke-Quiet { Invoke-PubPromote $y.Ctx $y.P }
    Check ((Get-FakeRelease $y.World (Read-PubJson "$($y.Ctx.Root)\draft\evidence.json").facts.releaseId).draft) 'publish skipped keeps the draft'
    $y.Ctx.Skip = @(); Invoke-Quiet { Invoke-PubPromote $y.Ctx $y.P }
    Check (Test-Path "$($y.Ctx.Root)\promote\evidence.json") 'finished after the skips'
    Pass 'promote: each outward step can be skipped and is logged; publishing needs the commit on the public main; a later run finishes'

    # the wrong draft: an older or second draft, changed assets, a newer release
    $y = Fresh; Through $y 'draft'; $null = Add-FakeRelease $y.World 'v0.1.0-beta.9' $true 'older draft'
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*releases are named v0.1.0-beta.9*'
    $y = Fresh; Through $y 'draft'; $r = Get-FakeRelease $y.World (Read-PubJson "$($y.Ctx.Root)\draft\evidence.json").facts.releaseId; $r.assets[0].digest = 'sha256:' + ('0' * 64)
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*is not the checked file*'
    $y = Fresh; Through $y 'draft'; $r = Get-FakeRelease $y.World (Read-PubJson "$($y.Ctx.Root)\draft\evidence.json").facts.releaseId; $r.body = 'edited by hand'
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*notes are not the planned notes*'
    $y = Fresh; Through $y 'draft'; $null = Add-FakeRelease $y.World 'v0.1.0-beta.10' $false
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*as new or newer is published*'
    $y = Fresh; Through $y 'draft'; $y.World.Releases.Remove((Get-FakeRelease $y.World (Read-PubJson "$($y.Ctx.Root)\draft\evidence.json").facts.releaseId)) | Out-Null
    $null = Add-FakeRelease $y.World 'v0.1.0-beta.9' $true 'SFV Ember 0.1.0 Beta 9'
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*is gone from*'
    $y = Fresh; Through $y 'draft'; G "$($y.F)\public.git" @('update-ref', 'refs/tags/v0.1.0-beta.9', $y.C0) | Out-Null
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*points at*'
    Pass 'promote: refuses a second draft with the tag, a changed asset, edited notes, a newer release, a replaced draft, a tag already elsewhere'

    # a public main that moved on its own: no forced push
    $y = Fresh; Through $y 'draft'
    $other = Join-Path $y.F 'other'; G $y.F @('clone', '-q', '-b', 'main', "$($y.F)\public.git", 'other') | Out-Null
    Write-File "$other\x.txt" 'x'; $null = Commit $other 'someone else'; G $other @('push', '-q', 'origin', 'HEAD:main') | Out-Null
    Reject { Invoke-PubPromote $y.Ctx $y.P 6>$null } '*would not fast-forward*'
    Pass 'promote: never forces a push; a public main that moved elsewhere stops it'

    # --- rooms by adoption ------------------------------------------------------------------------------------------
    $y = Fresh; $null = Plan $y; Invoke-Quiet { Invoke-PubBuild $y.Ctx $y.P }
    $kit = New-Kit $y $newBuild
    Adopt $y $kit $y.K $y.L2 | Out-Null
    $rooms = Read-PubEvidence $y.Ctx 'rooms' (Read-PubPlan $y.Ctx)
    Check ($rooms.facts.adopted -and $rooms.facts.kit.sfvCommit -ceq $y.K -and $rooms.facts.kit.libraryCommit -ceq $y.L2) 'adopted facts'
    Check ($rooms.facts.'sfve-room-host' -ceq (Get-PubSha256 "$kit\room-build\sfve-room-host") -and $rooms.facts.'sf4-net' -ceq (Get-PubSha256 "$kit\room-build\sf4-net")) 'kit hashes recorded'
    $sp = @($rooms.facts.kit.sfvPaths); $lp = @($rooms.facts.kit.libraryPaths)
    foreach ($path in 'data/pick-space.json', 'scripts/build-room-host-linux.sh', 'src/roomhost/SfvHostCatalog.cxx', 'src/ember/SfvPickSpace.cxx', 'src/ember/SfvPickSpace.hxx') { Check ($path -cin $sp) "sfv path $path" }
    foreach ($path in 'server/roomhost/main.cxx', 'server/roomhost/RoomHost.hxx', 'src/common/Catalog.hxx', 'server/roomhost/sources.txt', 'rust/sf4-net/Cargo.lock', 'rust/sf4-net/src/main.rs', 'server/ember/protocol/src/lib.rs') { Check ($path -cin $lp) "library path $path" }
    foreach ($path in 'server/roomhost/Win.cxx', 'src/common/Sf4Catalog.cxx', 'server/ember/bridge/src/lib.rs', 'src/core.txt') { Check ($path -cnotin $lp) "not a room-host input: $path" }
    Check (!@($y.World.Commands | Where-Object { $_ -like '*sfv.tar*' -or $_ -like '*vps*install*' -or $_ -like 'scp*vps:*' }).Count) 'nothing built or staged'
    Check (@($y.World.Commands | Where-Object { $_ -like '*rooms-install*-Mode ''check''*' }).Count -eq 1) 'the pair is checked on the build PC''s Linux'
    Invoke-Quiet { Invoke-PubDraft $y.Ctx $y.P }
    Invoke-Quiet { Invoke-PubPromote $y.Ctx $y.P }
    $check = @($y.World.Commands | Where-Object { $_ -like 'ssh vps bash -s -- check*' })[0]
    Check ($check -like "*$($rooms.facts.'sfve-room-host')*$($rooms.facts.'sf4-net')*") 'the server check compares the kit''s hashes'
    Check (Test-Path "$($y.Ctx.Root)\promote\evidence.json") 'promoted'
    Pass 'rooms -AdoptKit: no build, no staging; the pair checked on Linux, the kit''s commits proven equivalent, the input paths recorded; promote compares the installed hashes with the kit''s'

    $y = Fresh; $null = Plan $y
    $kit = New-Kit $y $oldBuild
    Reject { Adopt $y $kit $y.K $y.L2 } '*is for room build*'
    $kit = New-Kit $y $newBuild $oldBuild; Remove-Item "$kit\room-build\BUILD_ID"
    Reject { Adopt $y $kit $y.K $y.L2 } '*does not carry the plan''s room build*'
    Pass 'rooms -AdoptKit: refuses a kit for another room build (by its BUILD_ID or the build its room host embeds)'

    foreach ($case in @(@('data\pick-space.json', "{`n`t`"build`": `"$newBuild`",`n`t`"unshipped`": []`n}`n", '*changed data/pick-space.json*'),
                        @('src\ember\SfvPickSpace.hxx', "constexpr const char* kMatchBuild = `"sfv-ember/13173604/0.7`"; // edited`n", '*changed src/ember/SfvPickSpace.hxx*'))) {
        $y = Fresh
        Write-File "$($y.Sfv)\$($case[0])" $case[1]; $null = Commit $y.Sfv 'after the kit'
        $null = Plan $y
        Reject { Adopt $y (New-Kit $y $newBuild) $y.K $y.L2 } $case[2]
    }
    $y = Fresh; Write-File "$($y.Lib)\rust\sf4-net\src\main.rs" 'fn main() { changed() }'; $l3 = Commit $y.Lib 'helper'
    Write-File "$($y.Sfv)\README.md" "This release builds against library commit ``$($l3.Substring(0, 9))``.`n"; $null = Commit $y.Sfv 'pin'
    $null = Plan $y
    Reject { Adopt $y (New-Kit $y $newBuild) $y.K $y.L2 } '*library changed rust/sf4-net/src/main.rs*'
    $y = Fresh; Write-File "$($y.Lib)\server\ember\bridge\src\lib.rs" 'dev only'; Write-File "$($y.Lib)\src\core.txt" 'unrelated'; $l3 = Commit $y.Lib 'not in the build'
    Write-File "$($y.Sfv)\README.md" "This release builds against library commit ``$($l3.Substring(0, 9))``.`n"; Write-File "$($y.Sfv)\src\feature.txt" 'more'; $null = Commit $y.Sfv 'pin'
    $null = Plan $y
    Adopt $y (New-Kit $y $newBuild) $y.K $y.L2 | Out-Null
    Check (Test-Path "$($y.Ctx.Root)\rooms\evidence.json") 'changes outside the build do not stop adoption'
    Pass 'rooms -AdoptKit: refuses a change between the kit''s and the plan''s commits in the pick space, an included header or the helper''s crate, naming the path; changes outside the build pass'

    $y = Fresh; $null = Plan $y
    G $y.Sfv @('checkout', '-q', '-b', 'elsewhere', $y.C0) | Out-Null; Write-File "$($y.Sfv)\src\x.txt" 'x'; $side = Commit $y.Sfv 'elsewhere'; G $y.Sfv @('checkout', '-q', 'b9/integrate') | Out-Null
    Reject { Adopt $y (New-Kit $y $newBuild) $side $y.L2 } '*is not an ancestor of the plan''s*'
    G $y.Lib @('checkout', '-q', '-b', 'elsewhere', $y.L1) | Out-Null; Write-File "$($y.Lib)\src\y.txt" 'y'; $libSide = Commit $y.Lib 'elsewhere'; G $y.Lib @('checkout', '-q', 'feat/ember-library') | Out-Null
    Reject { Adopt $y (New-Kit $y $newBuild) $y.K $libSide } '*library commit*is not an ancestor*'
    Pass 'rooms -AdoptKit: refuses a kit commit that is not an ancestor of the plan''s, in either repository'

    $y = Fresh; $null = Plan $y; Invoke-Quiet { Invoke-PubBuild $y.Ctx $y.P }
    Adopt $y (New-Kit $y $newBuild) $y.K $y.L2 | Out-Null
    $bin = "$($y.Ctx.Root)\rooms\kit-in\sfve-room-host"; (Get-Item $bin).Attributes = 'Normal'; [IO.File]::AppendAllText($bin, 'x')
    Reject { Invoke-PubDraft $y.Ctx $y.P 6>$null } '*kit-in/sfve-room-host changed since its evidence was written*'
    Pass 'rooms -AdoptKit: a kit binary changed after adoption stops every later stage'

    # unchanged room build: it must be adopted (promote needs hashes to compare), never skipped
    $y = Fresh
    Write-File "$($y.Sfv)\data\pick-space.json" "{`n`t`"build`": `"$oldBuild`"`n}`n"; Write-File "$($y.Sfv)\src\ember\SfvPickSpace.hxx" "constexpr const char* kMatchBuild = `"sfv-ember/13173604/0.6`";`n"
    $null = Commit $y.Sfv 'same room build'
    $null = Plan $y
    Reject { Invoke-PubRooms $y.Ctx $y.P } '*adopt that kit*'
    Pass 'rooms: an unchanged room build is refused unless its installed kit is adopted'
    # the build PC agent itself (scripts/publish/laptop-agent.ps1), on fixture checkouts here
    $y = Fresh
    Write-File "$($y.Lib)\scripts\BuildProvenance.ps1" "function Get-SourceFingerprint([string]`$Root) { ('F' * 64) }`n"
    $l3 = Commit $y.Lib 'provenance'
    $work = Join-Path $y.F 'agent'; New-Item -ItemType Directory "$work", "$($y.F)\interactive" | Out-Null
    foreach ($side in @(@($y.Sfv, $y.C1, 'source'), @($y.Lib, $l3, 'library'))) {
        G $side[0] @('update-ref', "refs/sfv-publish/t/$($side[2])", $side[1]) | Out-Null
        G $side[0] @('bundle', 'create', '-q', "$work\$($side[2]).bundle", "refs/sfv-publish/t/$($side[2])") | Out-Null
    }
    G $y.F @('clone', '-q', '-b', 'b9/integrate', $y.Sfv, 'sfv-laptop') | Out-Null; G "$($y.F)\sfv-laptop" @('reset', '-q', '--hard', $y.C0) | Out-Null
    G $y.F @('clone', '-q', '-b', 'feat/ember-library', $y.Lib, 'lib-laptop') | Out-Null; G "$($y.F)\lib-laptop" @('reset', '-q', '--hard', $y.L1) | Out-Null
    $agent = Join-Path $scripts 'laptop-agent.ps1'
    $sync = { pwsh -NoProfile -File $agent -Phase sync -Work $work -Label t -Source $y.C1 -SourceTree (G $y.Sfv @('rev-parse', "$($y.C1)^{tree}")) -Library $l3 `
        -LibraryTree (G $y.Lib @('rev-parse', "$l3^{tree}")) -SourceCheckout "$($y.F)\sfv-laptop" -LibraryCheckout "$($y.F)\lib-laptop" -BuildDev D:\x -BuildPlayer D:\y `
        -VcpkgRoot D:\v -RenderStore D:\r -Dist D:\d -Interactive "$($y.F)\interactive" -JobName t-job | Select-Object -Last 1 }
    $result = ((& $sync) -replace '^RESULT ', '') | ConvertFrom-Json
    Check ($result.ok -and $result.sourceFingerprint -ceq ('F' * 64)) "agent sync: $($result.error)"
    Check ((G "$($y.F)\sfv-laptop" @('rev-parse', 'HEAD')) -ceq $y.C1 -and (G "$($y.F)\lib-laptop" @('rev-parse', 'HEAD')) -ceq $l3) 'checkouts fast-forwarded'
    Check ((Get-Content "$($y.F)\interactive\t-job.cmd" -Raw) -match '-Phase job') 'job file written'
    $poll = ((pwsh -NoProfile -File $agent -Phase poll -Work $work -Interactive "$($y.F)\interactive" | Select-Object -Last 1) -replace '^RESULT ', '') | ConvertFrom-Json
    Check ($poll.state -ceq 'new') 'poll before the job'
    G "$($y.F)\sfv-laptop" @('reset', '-q', '--hard', $y.C0) | Out-Null; Write-File "$($y.F)\sfv-laptop\README.md" 'local change'
    $result = ((& $sync) -replace '^RESULT ', '') | ConvertFrom-Json
    Check (!$result.ok -and $result.error -like '*local changes*') 'a dirty checkout is refused'
    G "$($y.F)\sfv-laptop" @('checkout', '-q', '--', '.') | Out-Null; Write-File "$($y.F)\sfv-laptop\x.txt" 'x'; $null = Commit "$($y.F)\sfv-laptop" 'laptop only'
    $result = ((& $sync) -replace '^RESULT ', '') | ConvertFrom-Json
    Check (!$result.ok -and $result.error -like '*not behind*') 'a diverged checkout is refused'
    Pass 'build PC agent: fetches the bundles, fast-forwards both checkouts to the plan''s commits, writes the job; refuses a dirty or diverged checkout'

    Write-Host ''
    Write-Host "All $script:passed publish tests passed."
} finally {
    foreach ($f in $fixtures) { if (Test-Path $f) { Get-ChildItem $f -Recurse -Force -File | ForEach-Object { $_.Attributes = 'Normal' }; Remove-Item $f -Recurse -Force -ErrorAction SilentlyContinue } }
}
