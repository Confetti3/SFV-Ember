#Requires -Version 7
# The fixture and helpers of the publish tests (tests/PublishTests.ps1, tests/PublishReviewTests.ps1): dot-source it.
$script:passed = 0
# No git in these tests may reach a network: local folders only.
$env:GIT_ALLOW_PROTOCOL = 'file'
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
    G $lib @('remote', 'set-url', '--push', 'origin', 'https://github.com/Owner/Library.git') | Out-Null
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
    # Push URLs name GitHub repositories (for the URL checks); git here may use local folders only (GIT_ALLOW_PROTOCOL).
    G $sfv @('remote', 'set-url', '--push', 'public', 'https://github.com/Owner/SFV-Ember.git') | Out-Null
    G $sfv @('remote', 'set-url', '--push', 'origin', 'https://github.com/Owner/sfv-private.git') | Out-Null
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
    $config = [pscustomobject]@{publicRepo = 'Owner/SFV-Ember'; publicRemote = 'public'; originRemote = 'origin'; libraryRepo = $lib; libraryRemote = 'origin'; libraryGithubRepo = 'Owner/Library'
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
