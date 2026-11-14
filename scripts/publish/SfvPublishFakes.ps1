#Requires -Version 7
# The fakes of tests/PublishTests.ps1: GitHub, the build PC and the VPS live in memory and in a fixture folder.
# git runs for real, but only on the fixture's own repositories (local folders, remotes included); a push to
# anything outside the fixture throws. Dot-source after SfvPublish.ps1 and SfvPublishOperations.ps1.

function New-FakeWorld([string]$Fixture) {
    [pscustomobject]@{
        Fixture = $Fixture; Clock = [DateTime]::new(2026, 11, 14, 20, 0, 0, [DateTimeKind]::Utc); Sleeps = 0
        Releases = [Collections.Generic.List[object]]::new(); NextId = 1000; Assets = @{}; FailPatch = $false
        Commands = [Collections.Generic.List[string]]::new(); Pushes = [Collections.Generic.List[string]]::new()
        LaptopBusy = ''; JobStarted = @{}; BuildTweak = $null; RoomsTweak = $null; JobFails = $false
        VpsCheck = 'offered'; VpsStaged = ''; VpsUploads = @{}; Patches = 0; PatchThenFail = $false; OnPush = $null
        Fingerprints = @{source = ('A' * 64); library = ('B' * 64)}
        Plan = $null
    }
}

function Get-FakeLocalPath([object]$World, [string]$Remote) {
    # "laptop:D:/sf4/publish/x" -> <fixture>\laptop\D\sf4\publish\x ; "vps:name" -> <fixture>\vps\name
    if ($Remote -cmatch '^laptop:([A-Za-z]):[\\/](.*)$') { return Join-Path $World.Fixture "laptop\$($Matches[1])\$($Matches[2].Replace('/', '\'))" }
    if ($Remote -cmatch '^vps:(.*)$') { return Join-Path $World.Fixture "vps\$($Matches[1])" }
    $null
}
function Get-FakeRelease([object]$World, [string]$Id) { $World.Releases | Where-Object { "$($_.id)" -ceq $Id } | Select-Object -First 1 }
function ConvertTo-FakeResult([string]$Text, [int]$Exit = 0, [string]$Err = '') { [pscustomobject]@{exitCode = $Exit; bytes = [Text.UTF8Encoding]::new($false).GetBytes($Text); stderr = $Err} }
function Add-FakeRelease([object]$World, [string]$Tag, [bool]$Draft, [string]$Name = '') {
    $World.NextId++
    $release = [pscustomobject]@{id = $World.NextId; tag_name = $Tag; name = $(if ($Name) { $Name } else { $Tag }); draft = $Draft; prerelease = $true
        target_commitish = 'main'; body = ''; created_at = $World.Clock.ToString('o'); published_at = $(if ($Draft) { $null } else { $World.Clock.ToString('o') })
        html_url = "https://example.invalid/releases/$($World.NextId)"; assets = [Collections.Generic.List[object]]::new()}
    $World.Releases.Add($release)
    $release
}

# Each fixture's operations read their own world from $script:FakeWorlds by the fixture's key (the scriptblocks are
# made from text with the key in them, so several fixtures live side by side in one test run).
$script:FakeWorlds = @{}
function New-FakeOperations([object]$World, [string]$PublicBare) {
    $key = [Guid]::NewGuid().ToString('N')
    $World | Add-Member -NotePropertyName PublicBare -NotePropertyValue $PublicBare -Force
    $script:FakeWorlds[$key] = $World
    $w = "`$W = `$script:FakeWorlds['$key']"
    $real = New-PubOperations
    @{
        Git = $real.Git
        Tar = $real.Tar
        Now = [scriptblock]::Create("$w; `$W.Clock = `$W.Clock.AddSeconds(1); `$W.Clock")
        Sleep = [scriptblock]::Create("param(`$Seconds) $w; `$W.Sleeps++; `$W.Clock = `$W.Clock.AddSeconds(`$Seconds)")
        Push = [scriptblock]::Create("param(`$Repo, `$Remote, `$Commit, `$Ref, `$DryRun) $w; Invoke-FakePush `$W `$Repo `$Remote `$Commit `$Ref `$DryRun")
        Scp = [scriptblock]::Create("param(`$From, `$To, `$Timeout) $w; Invoke-FakeScp `$W `$From `$To")
        Ssh = [scriptblock]::Create("param(`$HostAlias, `$Command, `$Stdin, `$Timeout) $w; `$W.Commands.Add(`"ssh `$HostAlias `$Command`"); if (`$HostAlias -ceq 'laptop') { return Invoke-FakeLaptop `$W `$Command }; if (`$HostAlias -ceq 'vps') { return Invoke-FakeVps `$W `$Command }; throw `"A test reached an unknown host `$HostAlias`"")
        Gh = [scriptblock]::Create("param(`$Arguments) $w; `$W.Commands.Add(`"gh `$(`$Arguments -join ' ')`"); Invoke-FakeGh `$W `$W.PublicBare `$Arguments")
    }
}
# Pushes go to the remote's fetch URL (a fixture folder), never by the remote's name: a fixture's push URLs name
# GitHub repositories for the URL checks.
function Invoke-FakePush([object]$World, [string]$Repo, [string]$Remote, [string]$Commit, [string]$Ref, $DryRun = $false) {
    $url = (& git -C $Repo remote get-url $Remote).Trim()
    if (![IO.Path]::GetFullPath($url).StartsWith($World.Fixture, [StringComparison]::OrdinalIgnoreCase)) { throw "A test tried to push outside the fixture: $url" }
    if ($DryRun) { return Invoke-PubProcess 'git' @('-C', $Repo, 'push', '--porcelain', '--no-follow-tags', '--dry-run', $url, "${Commit}:$Ref") $null 60 }
    $World.Pushes.Add("$Remote $Ref $Commit")
    if ($World.OnPush) { & $World.OnPush $Remote $Ref }
    Invoke-PubProcess 'git' @('-C', $Repo, 'push', '--quiet', '--no-follow-tags', $url, "${Commit}:$Ref") $null 60
}
function Invoke-FakeScp([object]$World, [string]$From, [string]$To) {
    $World.Commands.Add("scp $From $To")
    $source = if (Get-FakeLocalPath $World $From) { Get-FakeLocalPath $World $From } else { $From }
    $target = if (Get-FakeLocalPath $World $To) { Get-FakeLocalPath $World $To } else { $To }
    if (!(Test-Path -LiteralPath $source)) { return ConvertTo-FakeResult '' 1 "scp: $From not found" }
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
    ConvertTo-FakeResult ''
}
function Invoke-FakeGh([object]$World, [string]$PublicBare, [string[]]$A) {
    $joined = $A -join ' '
    $inputFile = if ($A -contains '--input') { $A[[Array]::IndexOf($A, '--input') + 1] } else { '' }
    if ($joined -cmatch '^api repos/[^/]+/[^/]+/releases --paginate') {
        return ConvertTo-FakeResult ((@($World.Releases | ForEach-Object { $_ | Select-Object id, tag_name, name, draft, prerelease, target_commitish, created_at, published_at | ConvertTo-Json -Compress }) -join "`n") + "`n")
    }
    if ($joined -cmatch '^api repos/[^/]+/[^/]+/releases/([0-9]+)$') {
        $release = Get-FakeRelease $World $Matches[1]
        if (!$release) { return ConvertTo-FakeResult '{"message":"Not Found"}' 1 'gh: Not Found (HTTP 404)' }
        return ConvertTo-FakeResult ($release | ConvertTo-Json -Depth 5)
    }
    if ($joined -cmatch '^api -X POST repos/[^/]+/[^/]+/releases --input') {
        $request = Get-Content -LiteralPath $inputFile -Raw | ConvertFrom-Json
        $release = Add-FakeRelease $World $request.tag_name $request.draft $request.name
        $release.body = $request.body; $release.prerelease = $request.prerelease; $release.target_commitish = $request.target_commitish
        return ConvertTo-FakeResult ($release | ConvertTo-Json -Depth 5)
    }
    if ($joined -cmatch '^api -X POST https://uploads\.github\.com/repos/[^/]+/[^/]+/releases/([0-9]+)/assets\?name=([^ ]+)') {
        $release = Get-FakeRelease $World $Matches[1]
        $name = [uri]::UnescapeDataString($Matches[2])
        $store = Join-Path $World.Fixture "github\$($release.id)"
        New-Item -ItemType Directory -Path $store -Force | Out-Null
        Copy-Item -LiteralPath $inputFile -Destination (Join-Path $store $name) -Force
        $release.assets.Add([pscustomobject]@{name = $name; size = (Get-Item -LiteralPath $inputFile).Length; state = 'uploaded'
            digest = "sha256:$((Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash.ToLowerInvariant())"})
        return ConvertTo-FakeResult '{}'
    }
    if ($joined -cmatch '^api -X PATCH repos/[^/]+/[^/]+/releases/([0-9]+) --input') {
        if ($World.FailPatch) { return ConvertTo-FakeResult '' 1 'gh: Server Error (HTTP 502)' }
        $World.Patches++
        $release = Get-FakeRelease $World $Matches[1]
        $request = Get-Content -LiteralPath $inputFile -Raw | ConvertFrom-Json
        & git -C $PublicBare cat-file -e "$($request.target_commitish)^{commit}" 2>$null
        if ($LASTEXITCODE) { return ConvertTo-FakeResult '' 1 'gh: Validation Failed (HTTP 422) target_commitish' }
        & git -C $PublicBare update-ref "refs/tags/$($release.tag_name)" $request.target_commitish
        $release.draft = $false; $release.target_commitish = $request.target_commitish; $release.published_at = $World.Clock.ToString('o')
        if ($World.PatchThenFail) { return ConvertTo-FakeResult '' 1 'gh: connection reset after the request was applied' }
        return ConvertTo-FakeResult ($release | ConvertTo-Json -Depth 5)
    }
    if ($joined -cmatch '^release download (\S+) --repo \S+ --dir (\S+)') {
        $tag = $Matches[1]; $dir = $Matches[2]
        $release = $World.Releases | Where-Object { $_.tag_name -ceq $tag -and !$_.draft } | Select-Object -First 1
        if (!$release) { return ConvertTo-FakeResult '' 1 'release not found' }
        foreach ($asset in $release.assets) { Copy-Item -LiteralPath (Join-Path $World.Fixture "github\$($release.id)\$($asset.name)") -Destination (Join-Path $dir $asset.name) }
        return ConvertTo-FakeResult ''
    }
    throw "A test reached an unexpected gh call: $joined"
}

function Invoke-FakeVps([object]$World, [string]$Command) {
    if ($Command -cmatch "^bash -s -- check ") { return ConvertTo-FakeResult "RESULT $($World.VpsCheck)`n" }
    if ($Command -cmatch "^bash -s -- state '([^']+)' '([^']+)' '([0-9a-f]+)'") {
        return ConvertTo-FakeResult $(if ($World.VpsStaged -ceq $Matches[3]) { "RESULT staged`n" } else { "RESULT not-staged`n" })
    }
    if ($Command -cmatch "^bash -s -- install '([^']+)' '([^']+)' '([0-9a-f]+)'") {
        $tar = Join-Path $World.Fixture "vps\$($Matches[1]).incoming-$($Matches[2]).tar"
        if (!(Test-Path -LiteralPath $tar)) { return ConvertTo-FakeResult "RESULT no-upload`n" 3 }
        $World.VpsStaged = $Matches[3]
        return ConvertTo-FakeResult "RESULT staged`n"
    }
    throw "A test reached an unexpected VPS command: $Command"
}

# The build PC: the agent's phases and the queue, answered from the fixture.
function Invoke-FakeLaptop([object]$World, [string]$Command) {
    if ($Command -cmatch "New-Item -ItemType Directory -Force -Path '([^']+)'") {
        New-Item -ItemType Directory -Path (Get-FakeLocalPath $World "laptop:$($Matches[1])") -Force | Out-Null
        return ConvertTo-FakeResult ''
    }
    if ($Command -cmatch 'sfv-start\.ps1'' -Job (\S+)') {
        $job = $Matches[1]
        $World.JobStarted[$job] = $true
        return ConvertTo-FakeResult "STARTED`n"
    }
    if ($Command -cnotmatch "-Phase (\S+) -Work '([^']+)'") { throw "A test reached an unexpected laptop command: $Command" }
    $phase = $Matches[1]; $work = $Matches[2]
    $local = Get-FakeLocalPath $World "laptop:$work"
    $arg = { param($Name) if ($Command -cmatch "-$Name '([^']*)'") { $Matches[1] } else { '' } }
    switch ($phase) {
        'status' {
            $task = if ($World.LaptopBusy) { 'Running' } else { 'Ready' }
            return ConvertTo-FakeResult ("RESULT " + (@{task = $task; job = $World.LaptopBusy; game = @(); heavy = @()} | ConvertTo-Json -Compress) + "`n")
        }
        'sync' {
            if (Test-Path -LiteralPath (Join-Path $local 'out')) { return ConvertTo-FakeResult "RESULT {`"ok`":false,`"error`":`"used folder`"}`n" }
            foreach ($b in 'source.bundle', 'library.bundle') { if (!(Test-Path -LiteralPath (Join-Path $local $b))) { return ConvertTo-FakeResult "RESULT {`"ok`":false,`"error`":`"no $b`"}`n" } }
            $job = & $arg 'JobName'
            Set-Content -LiteralPath (Join-Path $local 'job-name.txt') -Value $job
            return ConvertTo-FakeResult ("RESULT " + (@{ok = $true; sourceFingerprint = $World.Fingerprints.source; libraryFingerprint = $World.Fingerprints.library} | ConvertTo-Json -Compress) + "`n")
        }
        'rooms-install' {
            if (Test-Path -LiteralPath (Join-Path $local 'out')) { return ConvertTo-FakeResult "RESULT {`"ok`":false,`"error`":`"used folder`"}`n" }
            Set-Content -LiteralPath (Join-Path $local 'job-name.txt') -Value (& $arg 'JobName')
            Set-Content -LiteralPath (Join-Path $local 'build-id.txt') -Value (& $arg 'BuildId')
            Set-Content -LiteralPath (Join-Path $local 'mode.txt') -Value (& $arg 'Mode')
            return ConvertTo-FakeResult "RESULT {`"ok`":true}`n"
        }
        'poll' {
            $job = if (Test-Path -LiteralPath (Join-Path $local 'job-name.txt')) { (Get-Content -LiteralPath (Join-Path $local 'job-name.txt')).Trim() } else { '' }
            if (!$job -or !$World.JobStarted[$job]) { return ConvertTo-FakeResult "RESULT {`"state`":`"new`",`"tail`":`"`"}`n" }
            if ($World.JobFails) { return ConvertTo-FakeResult "RESULT {`"state`":`"failed`",`"tail`":`"tests failed`"}`n" }
            if (!(Test-Path -LiteralPath (Join-Path $local 'out'))) {
                if ($job -cmatch '-rooms-') { New-FakeRoomOutput $World $local } else { New-FakeBuildOutput $World $local }
            }
            return ConvertTo-FakeResult "RESULT {`"state`":`"done`",`"tail`":`"ok`"}`n"
        }
    }
    throw "A test reached an unexpected agent phase: $phase"
}

function Save-FakeJson([string]$Path, [object]$Value) { [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false)) }

# What the build PC's job leaves in out\: receipts, the helper record, a package zip and its sidecar, result.json.
# $World.BuildTweak (a scriptblock taking the files hashtable) may change any of it first.
function New-FakeBuildOutput([object]$World, [string]$Work) {
    $plan = $World.Plan
    $out = Join-Path $Work 'out'
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    $label = $plan.label
    $staging = Join-Path $Work "pkg\sfv-ember-$label"
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    $f = @{
        dll = "MZ fake mod; room build $($plan.roomBuild.id); match $($plan.roomBuild.matchLine);"
        launcher = 'MZ launcher'; extra = 'START HERE'; extraName = 'START_HERE.md'
        sourceRevision = $plan.source.commit; emberRevision = $plan.library.commit
        sourceFingerprint = $World.Fingerprints.source; emberFingerprint = $World.Fingerprints.library
        tests = @('RoomBuild', 'Picks', 'Render'); ctestTotal = 3; ctestFailed = 0; version = $plan.version; label = $label
        resultFingerprint = $World.Fingerprints.source; machineScan = @(); sidecarSha = $null; zipShaOverride = $null; packagedOverride = $null
    }
    if ($World.BuildTweak) { & $World.BuildTweak $f }
    [IO.File]::WriteAllText((Join-Path $staging 'sfv_ember.dll'), $f.dll, [Text.Encoding]::Latin1)
    [IO.File]::WriteAllText((Join-Path $staging 'Launcher.exe'), $f.launcher)
    [IO.File]::WriteAllBytes((Join-Path $staging $f.extraName), [Text.Encoding]::Unicode.GetBytes($f.extra))
    $binaries = @('sfv_ember.dll', 'Launcher.exe') | ForEach-Object { [ordered]@{path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $staging $_) -Algorithm SHA256).Hash} }
    $receipt = { param($Kind) [ordered]@{schema = 1; product = 'SFV Ember'; version = $f.version; sourceRevision = $f.sourceRevision; sourceFingerprint = $f.sourceFingerprint
        emberRevision = $f.emberRevision; emberFingerprint = $f.emberFingerprint; buildKind = $Kind; testsPassed = $true; tests = $f.tests; binaries = $binaries} }
    Save-FakeJson (Join-Path $out 'receipt-development.json') (& $receipt 'development')
    Save-FakeJson (Join-Path $out 'receipt-player.json') (& $receipt 'player')
    Save-FakeJson (Join-Path $out 'helper.provenance.json') @{emberRevision = $f.emberRevision; emberFingerprint = $f.emberFingerprint; sha256 = ('C' * 64)}
    $packaged = & $receipt 'player'
    $packaged.package = @{label = $f.label}
    if ($f.packagedOverride) { & $f.packagedOverride $packaged }
    Save-FakeJson (Join-Path $staging 'build-provenance.json') $packaged
    Copy-Item -LiteralPath (Join-Path $staging 'build-provenance.json') (Join-Path $out 'build-provenance.json')
    $zip = Join-Path $out "sfv-ember-$label.zip"
    Compress-Archive -LiteralPath $staging -DestinationPath $zip
    $sha = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath "$zip.sha256" -Value "$(if ($f.sidecarSha) { $f.sidecarSha } else { $sha })  sfv-ember-$label.zip" -NoNewline
    Save-FakeJson (Join-Path $out 'checkout.json') @{source = @{commit = $plan.source.commit; tree = $plan.source.tree; clean = $true; fingerprint = $World.Fingerprints.source}
        library = @{commit = $plan.library.commit; tree = $plan.library.tree; clean = $true; fingerprint = $World.Fingerprints.library}}
    Save-FakeJson (Join-Path $out 'result.json') @{ok = $true; steps = @(@{name = 'build-development'; exit = 0}, @{name = 'package'; exit = 0})
        ctest = @{development = @{passed = $f.ctestTotal - $f.ctestFailed; failed = $f.ctestFailed; total = $f.ctestTotal}; player = @{passed = $f.ctestTotal - $f.ctestFailed; failed = $f.ctestFailed; total = $f.ctestTotal}}
        zipSha256 = $(if ($f.zipShaOverride) { $f.zipShaOverride } else { $sha }); machineScan = $f.machineScan
        source = @{fingerprint = $f.resultFingerprint}; library = @{fingerprint = $World.Fingerprints.library}}
    Set-Content -LiteralPath (Join-Path $out 'job.log') -Value 'fake job'
}

function New-FakeRoomOutput([object]$World, [string]$Work) {
    $out = Join-Path $Work 'out'
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    $build = (Get-Content -LiteralPath (Join-Path $Work 'build-id.txt')).Trim()
    $r = @{own = 'helper_unavailable'; other = 'other_build'; embedded = $build; paths = '0'; hostBytes = "`u{7f}ELF room host $build"; status = 'ok'}
    if ($World.RoomsTweak) { & $World.RoomsTweak $r }
    $mode = if (Test-Path -LiteralPath (Join-Path $Work 'mode.txt')) { (Get-Content -LiteralPath (Join-Path $Work 'mode.txt')).Trim() } else { 'build' }
    if ($mode -ceq 'check') {
        # A kit is checked, not built: its own bytes, its own answers.
        Copy-Item -LiteralPath (Join-Path $Work 'sfve-room-host'), (Join-Path $Work 'sf4-net') -Destination $out
        $text = [Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes((Join-Path $out 'sfve-room-host')))
        $embedded = [regex]::Match($text, 'sfv-ember/[0-9]+/[0-9.]+/[0-9a-f]+/layout-[0-9a-f]+').Value
        if ($embedded) { $r.embedded = $embedded }
        if ($embedded -cne $build) { $r.own = 'other_build' }
    } else {
        [IO.File]::WriteAllText((Join-Path $out 'sfve-room-host'), $r.hostBytes, [Text.Encoding]::Latin1)
        [IO.File]::WriteAllText((Join-Path $out 'sf4-net'), "`u{7f}ELF helper", [Text.Encoding]::Latin1)
    }
    $sums = @('sfve-room-host', 'sf4-net') | ForEach-Object { "$((Get-FileHash -LiteralPath (Join-Path $out $_) -Algorithm SHA256).Hash.ToLowerInvariant())  $_" }
    Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS') -Value $sums
    $sha = { param($N) (Get-FileHash -LiteralPath (Join-Path $Work $N) -Algorithm SHA256).Hash.ToLowerInvariant() }
    $inputsLines = if ($mode -ceq 'check') { @('MODE=check') } else { @('MODE=build', "SFV_TAR=$(& $sha 'sfv.tar')", "LIBRARY_TAR=$(& $sha 'library.tar')") }
    Set-Content -LiteralPath (Join-Path $out 'result.txt') -Value (@("BUILD_ID=$build") + $inputsLines + @(
        "ANSWER_OWN=$($r.own)", "ANSWER_OTHER=$($r.other)", "EMBEDDED=$($r.embedded)", "PATHS_SFVE_ROOM_HOST=$($r.paths)", 'PATHS_SF4_NET=0', "STATUS=$($r.status)"))
    Set-Content -LiteralPath (Join-Path $out 'job.log') -Value 'fake rooms'
}
