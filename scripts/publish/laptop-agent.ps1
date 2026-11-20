#Requires -Version 7
# The build PC's half of scripts/publish.ps1 (docs/release/PUBLISHING.md). The desktop copies this file into an
# attempt folder on the build PC (-Work) and runs one phase at a time over SSH. Every phase prints one line
# "RESULT <json>" last. A Windows build PC runs the build as a job of the Interactive task (sfv-start.ps1's queue:
# one heavy job at a time); a Linux build PC runs it as a detached job.sh that holds the build PC's heavy-job lock
# (-Lock), started by the phase start.
#   status          a job running (the Interactive task, or the lock held), a running game, compilers and linkers
#   sync            the desktop's bundles fetched; both checkouts fast-forwarded to exactly the plan's commits and
#                   trees, clean; their source fingerprints; job.json, and the build job's .cmd (Windows: in the task
#                   folder) or job.sh (Linux: in -Work)
#   start           (Linux) job.sh started detached, or WAIT while another heavy job holds the lock
#   job             (in the job) development and player builds with their tests, the helper, the package;
#                   out/ gets the files the desktop takes back; result.json last
#   rooms-install   the room-host job, after its two inputs are checked against their SHA-256 (-Mode build:
#                   sfv.tar and library.tar; -Mode check: a kit's sfve-room-host and sf4-net, checked but not built)
#   rooms-job       (in the job) build-rooms-linux.sh on sf4-net and sfve-room-host from the archives: in WSL on
#                   Windows, directly on Linux
#   poll            new, starting, running, done or failed, with the log's last line
# Linux settings (sync and rooms-install; kept in job.json): -Setup, a shell file job.sh sources first (the tools on
# PATH, SF4E_XWIN and SF4E_LLVM_BIN for scripts/build.ps1's cross build); -VcpkgCache; -DiscordSdk; -TestEmulator,
# the program that runs a Windows program (Wine) and -TestPrefix, its prefix; -Lock; -Jobs.
param([Parameter(Mandatory = $true)][ValidateSet('status', 'sync', 'start', 'job', 'rooms-install', 'rooms-job', 'poll')][string]$Phase,
      [Parameter(Mandatory = $true)][string]$Work,
      [string]$Label = '', [string]$Source = '', [string]$SourceTree = '', [string]$Library = '', [string]$LibraryTree = '',
      [string]$SourceCheckout = '', [string]$LibraryCheckout = '', [string]$BuildDev = '', [string]$BuildPlayer = '',
      [string]$VcpkgRoot = '', [string]$RenderStore = '', [string]$MinColourCoverage = '', [string]$OldPackageCheck = '', [string]$Dist = '', [string]$ColourPackRepo = '',
      [string]$Interactive = 'D:\sf4\interactive', [string]$JobName = '', [string]$BuildId = '', [string]$SfvTar = '', [string]$LibraryTar = '', [ValidateSet('build', 'check')][string]$Mode = 'build',
      [string]$Setup = '', [string]$VcpkgCache = '', [string]$DiscordSdk = '', [string]$TestEmulator = '', [string]$TestPrefix = '', [string]$Lock = '', [string]$Jobs = '')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Out-Result([object]$Value) { Write-Output ('RESULT ' + ($Value | ConvertTo-Json -Depth 10 -Compress)) }
function Save-Json([string]$Path, [object]$Value) { [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false)) }
function Sha([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
$git = if ($IsWindows) { 'git.exe' } else { 'git' }
function Invoke-AgentGit([string]$Repo, [string[]]$Arguments) {
    $out = & $git -c safe.directory=* -C $Repo @Arguments 2>&1
    if ($LASTEXITCODE) { throw "git $($Arguments -join ' ') in ${Repo}: $($out -join ' ')" }
    ($out -join "`n").Trim()
}
# A checkout exactly at $Commit and $Tree, with nothing modified or untracked.
function Assert-Checkout([string]$Path, [string]$Commit, [string]$Tree) {
    if ((Invoke-AgentGit $Path @('rev-parse', 'HEAD')) -cne $Commit) { throw "$Path is not at $Commit" }
    if ((Invoke-AgentGit $Path @('rev-parse', 'HEAD^{tree}')) -cne $Tree) { throw "$Path is not at tree $Tree" }
    $status = Invoke-AgentGit $Path @('status', '--porcelain', '--untracked-files=normal')
    if ($status) { throw "$Path is not clean: $($status -split "`n" | Select-Object -First 5)" }
}
function Get-Fingerprint([string]$LibraryRoot, [string]$Root) {
    . (Join-Path $LibraryRoot 'scripts\BuildProvenance.ps1')
    $env:GIT_CONFIG_COUNT = '1'; $env:GIT_CONFIG_KEY_0 = 'safe.directory'; $env:GIT_CONFIG_VALUE_0 = '*'
    Get-SourceFingerprint $Root
}
# A path or value in single quotes for bash.
function ConvertTo-Bash([string]$Value) { "'" + $Value.Replace("'", "'\''") + "'" }
function Write-JobCmd([string]$Name, [string]$AgentPhase) {
    if (!$IsWindows) { Write-JobScript $AgentPhase; return }
    $cmd = @(
        "@rem SFV Ember publish: $AgentPhase in $Work",
        '@set CMAKE_BUILD_PARALLEL_LEVEL=8',
        '@set GIT_CONFIG_COUNT=1', '@set GIT_CONFIG_KEY_0=safe.directory', '@set GIT_CONFIG_VALUE_0=*',
        "@`"%ProgramFiles%\PowerShell\7\pwsh.exe`" -NoProfile -ExecutionPolicy Bypass -File `"$Work\laptop-agent.ps1`" -Phase $AgentPhase -Work `"$Work`" > `"$Work\job.log`" 2>&1",
        '@echo sfv job done %DATE% %TIME%')
    [IO.File]::WriteAllText((Join-Path $Interactive "$Name.cmd"), ($cmd -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
}
# Linux: job.sh in the attempt folder. It waits for the build PC's heavy-job lock (one heavy job at a time; the
# phase start does not start it while another holds the lock), sources the setup file, and runs the phase at a
# lower priority with this build PC's settings; Wine's processes of the test prefix are ended at its exit.
function Write-JobScript([string]$AgentPhase) {
    $job = Get-Content -LiteralPath (Join-Path $Work 'job.json') -Raw | ConvertFrom-Json
    $q = { param($Value) ConvertTo-Bash $Value }
    $lines = @(
        '#!/usr/bin/env bash',
        "# SFV Ember publish: $AgentPhase in $Work",
        'set -u',
        "exec 9>$(& $q $job.lock)",
        "flock -w 7200 9 || { echo '{`"ok`": false, `"error`": `"the heavy-job lock stayed taken for 2 h`"}' > $(& $q (Join-Path $Work 'result.json')); exit 3; }",
        "echo `$`$ > $(& $q (Join-Path $Work 'job.pid'))",
        ". $(& $q $job.setup)",
        "mkdir -p $(& $q (Join-Path $Work 'tmp'))",
        "export TMPDIR=$(& $q (Join-Path $Work 'tmp'))",
        "export CMAKE_BUILD_PARALLEL_LEVEL=$([int]$job.jobs) VCPKG_MAX_CONCURRENCY=$([int]$job.jobs) CARGO_BUILD_JOBS=$([int]$job.jobs)",
        "export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*'",
        "export SF4E_DISCORD_SDK_ARCHIVE=$(& $q "$($job.discordSdk)")",
        # Windows' DLL search finds the xinput proxy beside a test program before the system's; Wine prefers its
        # builtin xinput1_3 unless told otherwise (guard_probe and every test that loads the proxy).
        "export SFVE_WINE_EMULATOR=$(& $q $job.testEmulator) EMBER_WINE_DATA=$(& $q $job.testPrefix) WINEDLLOVERRIDES='xinput1_3=n,b'",
        "trap `"$(& $q $job.testEmulator) --kill >/dev/null 2>&1 || true`" EXIT",
        "nice -n 5 $(& $q $job.pwsh) -NoProfile -File $(& $q (Join-Path $Work 'laptop-agent.ps1')) -Phase $AgentPhase -Work $(& $q $Work) > $(& $q (Join-Path $Work 'job.log')) 2>&1")
    [IO.File]::WriteAllText((Join-Path $Work 'job.sh'), ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
}
# The Linux settings a job needs, checked when it is set up.
function Get-LinuxSettings {
    foreach ($pair in @(@('-Setup', $Setup), @('-TestEmulator', $TestEmulator), @('-Lock', $Lock))) { if (!$pair[1]) { throw "A Linux build PC needs $($pair[0])." } }
    if (!(Test-Path -LiteralPath $Setup -PathType Leaf)) { throw "No setup file at $Setup" }
    if (!(Test-Path -LiteralPath $TestEmulator -PathType Leaf)) { throw "No test emulator at $TestEmulator" }
    [ordered]@{setup = $Setup; testEmulator = $TestEmulator; testPrefix = $TestPrefix; lock = $Lock; jobs = $(if ($Jobs) { [int]$Jobs } else { 12 })
        pwsh = (Get-Process -Id $PID).Path; discordSdk = $DiscordSdk; vcpkgCache = $VcpkgCache}
}
# This PC's own names: user, profile (home) folder, computer.
function Get-MachineNames {
    $names = if ($IsWindows) { @($env:USERNAME, $env:COMPUTERNAME, (Split-Path -Leaf "$env:USERPROFILE")) }
        else { @($env:USER, ([Environment]::MachineName), (Split-Path -Leaf "$HOME"), "$(& hostname)".Trim()) }
    @($names | Where-Object { $_ -and $_.Length -ge 3 } | Sort-Object -Unique)
}
# Counts of this PC's own names in a file, raw and UTF-16 (on Linux in any case, as a word of its own, as the
# desktop's scan reads them: a user's name is in its home folder's path whatever the case).
function Get-MachineHits([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $raw = [Text.Encoding]::Latin1.GetString($bytes)
    $wide = [Text.Encoding]::Unicode.GetString($bytes)
    $count = 0
    foreach ($name in Get-MachineNames) {
        $pattern = if ($IsWindows) { [regex]::new([regex]::Escape($name)) } else { [regex]::new("(?<![A-Za-z0-9])$([regex]::Escape($name))(?![A-Za-z0-9])", 'IgnoreCase') }
        $count += $pattern.Matches($raw).Count + $pattern.Matches($wide).Count
    }
    $count
}
function Get-CTest([string]$Log) {
    $last = @(Select-String -LiteralPath $Log -Pattern '([0-9]+)% tests passed, ([0-9]+) tests? failed out of ([0-9]+)') | Select-Object -Last 1
    if (!$last) { return @{passed = 0; failed = 0; total = 0} }
    $failed = [int]$last.Matches[0].Groups[2].Value; $total = [int]$last.Matches[0].Groups[3].Value
    @{passed = $total - $failed; failed = $failed; total = $total}
}
function Invoke-Step([string]$Name, [string]$Script, [string[]]$Arguments) {
    $log = Join-Path $Work "$Name.log"
    $started = Get-Date
    $pwsh = if ($IsWindows) { "$env:ProgramFiles\PowerShell\7\pwsh.exe" } else { (Get-Process -Id $PID).Path }
    & $pwsh -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments *> $log
    $code = $LASTEXITCODE
    $script:steps += @{name = $Name; exit = $code; seconds = [int]((Get-Date) - $started).TotalSeconds}
    if ($code) { throw "$Name failed ($code); see $log" }
}
# Linux: whether the heavy-job lock is free now (flock takes it and lets it go at once).
function Test-LockFree([string]$Path) {
    & flock -n $Path true 2>$null
    $LASTEXITCODE -eq 0
}

New-Item -ItemType Directory -Path $Work -Force | Out-Null
# A job is set up only in a folder no job has used: an earlier attempt's out\ or result.json would answer for it.
function Assert-FreshWork { foreach ($name in 'out', 'result.json', 'started.json', 'job.json', 'checkout.json') { if (Test-Path -LiteralPath (Join-Path $Work $name)) { throw "$Work was used by an earlier job ($name); each attempt needs a new folder." } } }
switch ($Phase) {
    'status' {
        $heavyNames = @('cl', 'link', 'ninja', 'cmake', 'ctest', 'cargo', 'rustc', 'msbuild', 'clang-cl', 'lld-link', 'cl.exe', 'link.exe', 'cc1plus')
        if (!$IsWindows) {
            # A Linux build PC: a job runs while the heavy-job lock is held; the game runs under Proton.
            $running = if ($Lock -and !(Test-LockFree $Lock)) { 'Running' } else { 'Ready' }
            $game = @(Get-Process | Where-Object { $_.ProcessName -like 'StreetFighterV*' } | ForEach-Object ProcessName | Sort-Object -Unique)
            $heavy = @(Get-Process | Where-Object { $_.ProcessName -in $heavyNames } | ForEach-Object ProcessName | Sort-Object -Unique)
            $free = [math]::Round(([IO.DriveInfo]::new($Work)).AvailableFreeSpace / 1GB, 1)
            Out-Result @{task = $running; job = $(if ($running -ceq 'Running') { "the lock $Lock is held" } else { '' }); game = $game; heavy = $heavy; workFreeGb = $free
                os = 'linux'; names = @(Get-MachineNames)}
            break
        }
        # A build PC that is the desktop itself runs jobs through local-queue.ps1 (its sfv-start.ps1), not a task.
        $local = Join-Path $Interactive 'local-queue.pid'
        $task = if (Test-Path -LiteralPath (Join-Path $Interactive 'local-queue')) {
            $queued = if (Test-Path -LiteralPath $local) { Get-Process -Id ([int](Get-Content -LiteralPath $local -Raw)) -ErrorAction SilentlyContinue }
            if ($queued) { 'Running' } else { 'Ready' }
        } else { ((schtasks /query /tn 'Ember Interactive' /fo LIST | Select-String 'Status:').ToString() -replace '^Status:\s*', '').Trim() }
        $jobFile = Join-Path $Interactive 'job.cmd'
        $job = if (Test-Path -LiteralPath $jobFile) { (Get-Content -LiteralPath $jobFile -TotalCount 1) } else { '' }
        $game = @(Get-Process | Where-Object { $_.ProcessName -like 'StreetFighterV*' } | ForEach-Object ProcessName | Sort-Object -Unique)
        $heavy = @(Get-Process | Where-Object { $_.ProcessName -in @('cl', 'link', 'ninja', 'cmake', 'ctest', 'cargo', 'rustc', 'msbuild') } | ForEach-Object ProcessName | Sort-Object -Unique)
        Out-Result @{task = $task; job = $job; game = $game; heavy = $heavy; cFreeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1); dFreeGb = [math]::Round((Get-PSDrive D).Free / 1GB, 1)}
    }
    'sync' {
        try {
            Assert-FreshWork
            if ($MinColourCoverage -cnotmatch '^(0(\.[0-9]+)?|1(\.0+)?)$') { throw "No colour coverage for the package (-MinColourCoverage, 0 to 1): '$MinColourCoverage'" }
            $linux = if (!$IsWindows) { Get-LinuxSettings } else { $null }
            $sides = [ordered]@{source = @($SourceCheckout, $Source, $SourceTree); library = @($LibraryCheckout, $Library, $LibraryTree)}
            foreach ($side in $sides.Keys) {
                $path, $commit, $tree = $sides[$side]
                # On Linux the checkouts are made as on Windows (core.autocrlf): the same bytes, so the same
                # fingerprints and the same text in the package.
                if (!$IsWindows -and (Invoke-AgentGit $path @('config', '--get', 'core.autocrlf')) -cne 'true') { throw "$path does not check out with Windows line endings (git config core.autocrlf true)." }
                $ref = "refs/sfv-publish/$Label/$side"
                Invoke-AgentGit $path @('fetch', '--quiet', '--force', (Join-Path $Work "$side.bundle"), "+${ref}:$ref") | Out-Null
                if ((Invoke-AgentGit $path @('rev-parse', $ref)) -cne $commit) { throw "The $side bundle holds another commit than $commit" }
                $status = Invoke-AgentGit $path @('status', '--porcelain', '--untracked-files=normal')
                if ($status) { throw "$path has local changes; nothing moved." }
                $head = Invoke-AgentGit $path @('rev-parse', 'HEAD')
                if ($head -cne $commit) {
                    & $git -c safe.directory=* -C $path merge-base --is-ancestor $head $commit
                    if ($LASTEXITCODE) { throw "$path is at $head, which is not behind $commit (it has work the desktop lacks, or is another line); nothing moved." }
                    Invoke-AgentGit $path @('merge', '--quiet', '--ff-only', $commit) | Out-Null
                }
                Assert-Checkout $path $commit $tree
            }
            $checkout = [ordered]@{}
            foreach ($side in $sides.Keys) {
                $checkout[$side] = [ordered]@{checkout = $sides[$side][0]; commit = $sides[$side][1]; tree = $sides[$side][2]; clean = $true
                    fingerprint = (Get-Fingerprint $LibraryCheckout $sides[$side][0])}
            }
            Save-Json (Join-Path $Work 'checkout.json') $checkout
            $job = [ordered]@{label = $Label; source = $SourceCheckout; library = $LibraryCheckout; buildDev = $BuildDev
                buildPlayer = $BuildPlayer; vcpkgRoot = $VcpkgRoot; renderStore = $RenderStore; minColourCoverage = $MinColourCoverage; oldPackageCheck = $OldPackageCheck; dist = $Dist
                colourPackRepo = $ColourPackRepo}
            if ($linux) { $job.os = 'linux'; foreach ($key in $linux.Keys) { $job[$key] = $linux[$key] } }
            Save-Json (Join-Path $Work 'job.json') $job
            Write-JobCmd $JobName 'job'
            Out-Result @{ok = $true; sourceFingerprint = $checkout.source.fingerprint; libraryFingerprint = $checkout.library.fingerprint}
        } catch { Out-Result @{ok = $false; error = $_.Exception.Message} }
    }
    'start' {
        # Linux: the job set up by sync or rooms-install, detached (it outlives this SSH call), unless another
        # heavy job holds the lock.
        try {
            if ($IsWindows) { throw 'A Windows build PC starts its jobs through sfv-start.ps1.' }
            $job = Get-Content -LiteralPath (Join-Path $Work 'job.json') -Raw | ConvertFrom-Json
            if (!(Test-Path -LiteralPath (Join-Path $Work 'job.sh'))) { throw "No job.sh in $Work" }
            if (Test-Path -LiteralPath (Join-Path $Work 'launched.json')) { Out-Result @{state = 'started'; note = 'started already'}; break }
            if (!(Test-LockFree $job.lock)) { Out-Result @{state = 'wait'; note = "another heavy job holds $($job.lock)"}; break }
            Save-Json (Join-Path $Work 'launched.json') @{at = (Get-Date).ToUniversalTime().ToString('o')}
            # bash does the redirection, so the job holds none of this call's (SSH's) output open.
            & bash -c 'setsid -f nohup bash "$1" > /dev/null 2>&1 < /dev/null' 'start' (Join-Path $Work 'job.sh')
            if ($LASTEXITCODE) { Remove-Item -LiteralPath (Join-Path $Work 'launched.json'); throw "setsid failed ($LASTEXITCODE)" }
            Out-Result @{state = 'started'; note = ''}
        } catch { Out-Result @{state = 'error'; note = $_.Exception.Message} }
    }
    'job' {
        $script:steps = @()
        Save-Json (Join-Path $Work 'started.json') @{at = (Get-Date).ToUniversalTime().ToString('o')}
        $result = [ordered]@{ok = $false}
        try {
            $job = Get-Content -LiteralPath (Join-Path $Work 'job.json') -Raw | ConvertFrom-Json
            $checkout = Get-Content -LiteralPath (Join-Path $Work 'checkout.json') -Raw | ConvertFrom-Json
            foreach ($side in @('source', 'library')) {
                Assert-Checkout $checkout.$side.checkout $checkout.$side.commit $checkout.$side.tree
                if ((Get-Fingerprint $job.library $checkout.$side.checkout) -cne $checkout.$side.fingerprint) { throw "The $side checkout changed since the sync." }
            }
            $common = @('-EmberRoot', $job.library, '-VcpkgRoot', $job.vcpkgRoot)
            if (!$IsWindows) { $common += @('-VcpkgCache', $job.vcpkgCache) }
            # Linux: the DLLs a test program loads from the build's packages, for Wine (as the build tree's own
            # folder is for Windows).
            $packageBin = { param($Build) if (!$IsWindows) { $env:WINEPATH = "Z:$(Join-Path $Build 'vcpkg_installed/x64-windows-static-md-wchar-filenames/bin')" } }
            & $packageBin $job.buildDev
            Invoke-Step 'build-development' (Join-Path $job.source 'scripts\build.ps1') (@('-BuildDir', $job.buildDev) + $common)
            & $packageBin $job.buildPlayer
            Invoke-Step 'build-player' (Join-Path $job.source 'scripts\build.ps1') (@('-Player', '-BuildDir', $job.buildPlayer) + $common)
            Invoke-Step 'helper' (Join-Path $job.source 'scripts\build-helper.ps1') @('-BuildDir', $job.buildPlayer, '-EmberRepo', $job.library)
            if (Test-Path -LiteralPath $job.dist) { throw "The package folder $($job.dist) exists already; use a new attempt." }
            New-Item -ItemType Directory -Path $job.dist | Out-Null
            $package = @('-BuildDir', $job.buildPlayer, '-OutDir', $job.dist, '-VersionLabel', $job.label, '-RenderStore', $job.renderStore, '-MinColourCoverage', $job.minColourCoverage)
            if ($job.oldPackageCheck) { $package += @('-OldPackageCheck', $job.oldPackageCheck) }
            # A repository here is the pipeline's colourPack setting: split the colours into a pack hosted on it.
            if ($job.colourPackRepo) { $package += @('-ColourPack', '-ColourPackRepo', $job.colourPackRepo) }
            Invoke-Step 'package' (Join-Path $job.source 'scripts\package.ps1') $package
            $out = Join-Path $Work 'out'
            New-Item -ItemType Directory -Path $out -Force | Out-Null
            $folder = Join-Path $job.dist "sfv-ember-$($job.label)"
            Copy-Item -LiteralPath (Join-Path $job.buildDev 'build-provenance.json') (Join-Path $out 'receipt-development.json')
            Copy-Item -LiteralPath (Join-Path $job.buildPlayer 'build-provenance.json') (Join-Path $out 'receipt-player.json')
            Copy-Item -LiteralPath (Join-Path $job.buildPlayer 'sf4-net.provenance.json') (Join-Path $out 'helper.provenance.json')
            Copy-Item -LiteralPath (Join-Path $folder 'build-provenance.json') (Join-Path $out 'build-provenance.json')
            Copy-Item -LiteralPath "$folder.zip" (Join-Path $out "sfv-ember-$($job.label).zip")
            Copy-Item -LiteralPath "$folder.zip.sha256" (Join-Path $out "sfv-ember-$($job.label).zip.sha256")
            # The colour pack package.ps1 wrote beside the zip, under its own name, when it split the colours out
            # (docs/design/COLOUR_PACK.md); by default the zip carries every colour and there is none.
            $packs = @(Get-ChildItem -LiteralPath $job.dist -File -Filter 'sfve-colours-*.zip')
            if ($packs.Count -gt 1) { throw "The package step left $($packs.Count) colour packs in $($job.dist), not one or none." }
            $pack = if ($packs.Count) { Join-Path $out $packs[0].Name }
            if ($pack) { Copy-Item -LiteralPath $packs[0].FullName $pack }
            Copy-Item -LiteralPath (Join-Path $Work 'checkout.json') (Join-Path $out 'checkout.json')
            $result.ctest = @{development = (Get-CTest (Join-Path $Work 'build-development.log')); player = (Get-CTest (Join-Path $Work 'build-player.log'))}
            $result.zipSha256 = Sha (Join-Path $out "sfv-ember-$($job.label).zip")
            $result.colourPack = if ($pack) { @{name = $packs[0].Name; sha256 = (Sha $pack)} } else { $null }
            # The pack's entries are stored, so its names and manifest are scanned as they are in the file.
            $result.machineScan = @(@(Get-ChildItem -LiteralPath $folder -Recurse -File | ForEach-Object { @{file = $_.FullName.Substring($folder.Length + 1); hits = (Get-MachineHits $_.FullName)} }) +
                @($packs | ForEach-Object { @{file = $_.Name; hits = (Get-MachineHits (Join-Path $out $_.Name))} }) | Where-Object { $_.hits })
            # The names that scan looked for, which the desktop's own scan adds to its rules.
            $result.machineNames = @(Get-MachineNames)
            $result.source = @{fingerprint = (Get-Fingerprint $job.library $checkout.source.checkout)}
            $result.library = @{fingerprint = (Get-Fingerprint $job.library $checkout.library.checkout)}
            $result.ok = $true
        } catch { $result.error = $_.Exception.Message }
        $result.steps = $script:steps
        $out = Join-Path $Work 'out'
        New-Item -ItemType Directory -Path $out -Force | Out-Null
        Save-Json (Join-Path $out 'result.json') $result
        Copy-Item -LiteralPath (Join-Path $Work 'job.log') (Join-Path $out 'job.log') -ErrorAction SilentlyContinue
        if (!(Test-Path -LiteralPath (Join-Path $out 'job.log'))) { Set-Content -LiteralPath (Join-Path $out 'job.log') -Value ($script:steps | ConvertTo-Json -Compress) }
        Save-Json (Join-Path $Work 'result.json') $result
    }
    'rooms-install' {
        try {
            Assert-FreshWork
            $names = if ($Mode -ceq 'check') { @('sfve-room-host', 'sf4-net') } else { @('sfv.tar', 'library.tar') }
            if ((Sha (Join-Path $Work $names[0])) -cne $SfvTar -or (Sha (Join-Path $Work $names[1])) -cne $LibraryTar) { throw 'The inputs on this PC are not the desktop''s.' }
            if ($BuildId -cnotmatch '^sfv-ember/[0-9]+/[0-9]+\.[0-9]+/[0-9a-f]{16}/layout-[0-9a-f]{16}$') { throw "Not a room build: $BuildId" }
            $job = [ordered]@{buildId = $BuildId; mode = $Mode}
            if (!$IsWindows) { $linux = Get-LinuxSettings; $job.os = 'linux'; foreach ($key in $linux.Keys) { $job[$key] = $linux[$key] } }
            Save-Json (Join-Path $Work 'job.json') $job
            Write-JobCmd $JobName 'rooms-job'
            Out-Result @{ok = $true}
        } catch { Out-Result @{ok = $false; error = $_.Exception.Message} }
    }
    'rooms-job' {
        Save-Json (Join-Path $Work 'started.json') @{at = (Get-Date).ToUniversalTime().ToString('o')}
        $result = [ordered]@{ok = $false}
        try {
            $job = Get-Content -LiteralPath (Join-Path $Work 'job.json') -Raw | ConvertFrom-Json
            $log = Join-Path $Work 'rooms.log'
            if ($job.mode -ceq 'check') { New-Item -ItemType Directory -Path (Join-Path $Work 'kit-in') -Force | Out-Null; Copy-Item -LiteralPath (Join-Path $Work 'sfve-room-host'), (Join-Path $Work 'sf4-net') -Destination (Join-Path $Work 'kit-in') -Force }
            if ($IsWindows) {
                $drive = $Work.Substring(0, 1).ToLowerInvariant()
                $mnt = "/mnt/$drive/$($Work.Substring(3).Replace('\', '/'))"
                $flag = if ($job.mode -ceq 'check') { '--check ' } else { '' }
                $in = if ($job.mode -ceq 'check') { "$mnt/kit-in" } else { $mnt }
                & wsl -e bash -lc "bash '$mnt/build-rooms-linux.sh' $flag'$in' '$mnt/out' '$($job.buildId)'" *> $log
            } else {
                # This PC is the Linux the room hosts are built on.
                $in = if ($job.mode -ceq 'check') { Join-Path $Work 'kit-in' } else { $Work }
                $arguments = @(if ($job.mode -ceq 'check') { '--check' }) + @($in, (Join-Path $Work 'out'), $job.buildId)
                & bash (Join-Path $Work 'build-rooms-linux.sh') @arguments *> $log
            }
            $result.exit = $LASTEXITCODE
            $text = if (Test-Path -LiteralPath (Join-Path $Work 'out\result.txt')) { Get-Content -LiteralPath (Join-Path $Work 'out\result.txt') -Raw } else { '' }
            $result.ok = ($result.exit -eq 0 -and $text -cmatch '(?m)^STATUS=ok')
            if (!$result.ok) { $result.error = "the $(if ($IsWindows) { 'WSL' } else { 'Linux' }) build ended with $($result.exit); see $log" }
            Copy-Item -LiteralPath $log (Join-Path $Work 'out\job.log') -ErrorAction SilentlyContinue
        } catch { $result.error = $_.Exception.Message }
        Save-Json (Join-Path $Work 'result.json') $result
    }
    'poll' {
        $resultPath = Join-Path $Work 'result.json'
        $logs = @(Get-ChildItem -LiteralPath $Work -Filter *.log -File | Sort-Object LastWriteTime -Descending)
        $tail = if ($logs.Count) { "$($logs[0].Name): $((Get-Content -LiteralPath $logs[0].FullName -Tail 1) -join ' ')" } else { '' }
        if (Test-Path -LiteralPath $resultPath) {
            $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
            Out-Result @{state = $(if ($result.ok) { 'done' } else { 'failed' }); tail = $(if ($result.ok) { $tail } else { "$($result.error) ($tail)" })}
        } elseif (Test-Path -LiteralPath (Join-Path $Work 'started.json')) {
            Out-Result @{state = 'running'; tail = $tail}
        } elseif (!$IsWindows -and (Test-Path -LiteralPath (Join-Path $Work 'launched.json'))) {
            # Started, waiting for the lock or its setup; a job.sh that gave up says so in job.log.
            Out-Result @{state = 'starting'; tail = $(if ($tail) { $tail } else { 'the job waits for the heavy-job lock' })}
        } elseif ($IsWindows -and (Test-Path -LiteralPath (Join-Path $Interactive 'job.cmd')) -and (Get-Content -LiteralPath (Join-Path $Interactive 'job.cmd') -Raw).Contains($Work)) {
            Out-Result @{state = 'starting'; tail = 'the task has the job'}
        } else {
            Out-Result @{state = 'new'; tail = ''}
        }
    }
}
