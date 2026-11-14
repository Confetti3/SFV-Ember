#Requires -Version 7
# The build PC's half of scripts/publish.ps1 (docs/release/PUBLISHING.md). The desktop copies this file into an
# attempt folder on the build PC (-Work) and runs one phase at a time over SSH; the build itself runs as a job of
# the Interactive task (sfv-start.ps1's queue: one heavy job at a time). Every phase prints one line
# "RESULT <json>" last.
#   status          the Interactive task, the job it runs, a running game, compilers and linkers
#   sync            the desktop's bundles fetched; both checkouts fast-forwarded to exactly the plan's commits and
#                   trees, clean; their source fingerprints; job.json, and the build job's .cmd in the task folder
#   job             (in the task) development and player builds with their tests, the helper, the package;
#                   out\ gets the files the desktop takes back; result.json last
#   rooms-install   the room-host job's .cmd, after its two inputs are checked against their SHA-256 (-Mode build:
#                   sfv.tar and library.tar; -Mode check: a kit's sfve-room-host and sf4-net, checked but not built)
#   rooms-job       (in the task) the WSL build of sf4-net and sfve-room-host from the archives
#   poll            new, starting, running, done or failed, with the log's last line
param([Parameter(Mandatory = $true)][ValidateSet('status', 'sync', 'job', 'rooms-install', 'rooms-job', 'poll')][string]$Phase,
      [Parameter(Mandatory = $true)][string]$Work,
      [string]$Label = '', [string]$Source = '', [string]$SourceTree = '', [string]$Library = '', [string]$LibraryTree = '',
      [string]$SourceCheckout = '', [string]$LibraryCheckout = '', [string]$BuildDev = '', [string]$BuildPlayer = '',
      [string]$VcpkgRoot = '', [string]$RenderStore = '', [string]$OldPackageCheck = '', [string]$Dist = '',
      [string]$Interactive = 'D:\sf4\interactive', [string]$JobName = '', [string]$BuildId = '', [string]$SfvTar = '', [string]$LibraryTar = '', [ValidateSet('build', 'check')][string]$Mode = 'build')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Out-Result([object]$Value) { Write-Output ('RESULT ' + ($Value | ConvertTo-Json -Depth 10 -Compress)) }
function Save-Json([string]$Path, [object]$Value) { [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false)) }
function Sha([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Invoke-AgentGit([string]$Repo, [string[]]$Arguments) {
    $out = & git.exe -c safe.directory=* -C $Repo @Arguments 2>&1
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
function Write-JobCmd([string]$Name, [string]$AgentPhase) {
    $cmd = @(
        "@rem SFV Ember publish: $AgentPhase in $Work",
        '@set CMAKE_BUILD_PARALLEL_LEVEL=8',
        '@set GIT_CONFIG_COUNT=1', '@set GIT_CONFIG_KEY_0=safe.directory', '@set GIT_CONFIG_VALUE_0=*',
        "@`"%ProgramFiles%\PowerShell\7\pwsh.exe`" -NoProfile -ExecutionPolicy Bypass -File `"$Work\laptop-agent.ps1`" -Phase $AgentPhase -Work `"$Work`" > `"$Work\job.log`" 2>&1",
        '@echo sfv job done %DATE% %TIME%')
    [IO.File]::WriteAllText((Join-Path $Interactive "$Name.cmd"), ($cmd -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
}
# Counts of this PC's own names (user, profile folder, computer) in a file, raw and UTF-16.
function Get-MachineHits([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $raw = [Text.Encoding]::Latin1.GetString($bytes)
    $wide = [Text.Encoding]::Unicode.GetString($bytes)
    $count = 0
    foreach ($name in @($env:USERNAME, $env:COMPUTERNAME, (Split-Path -Leaf "$env:USERPROFILE")) | Where-Object { $_ -and $_.Length -ge 3 } | Sort-Object -Unique) {
        $count += ([regex]::Matches($raw, [regex]::Escape($name))).Count + ([regex]::Matches($wide, [regex]::Escape($name))).Count
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
    & "$env:ProgramFiles\PowerShell\7\pwsh.exe" -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments *> $log
    $code = $LASTEXITCODE
    $script:steps += @{name = $Name; exit = $code; seconds = [int]((Get-Date) - $started).TotalSeconds}
    if ($code) { throw "$Name failed ($code); see $log" }
}

New-Item -ItemType Directory -Path $Work -Force | Out-Null
switch ($Phase) {
    'status' {
        $task = ((schtasks /query /tn 'Ember Interactive' /fo LIST | Select-String 'Status:').ToString() -replace '^Status:\s*', '').Trim()
        $jobFile = Join-Path $Interactive 'job.cmd'
        $job = if (Test-Path -LiteralPath $jobFile) { (Get-Content -LiteralPath $jobFile -TotalCount 1) } else { '' }
        $game = @(Get-Process | Where-Object { $_.ProcessName -like 'StreetFighterV*' } | ForEach-Object ProcessName | Sort-Object -Unique)
        $heavy = @(Get-Process | Where-Object { $_.ProcessName -in @('cl', 'link', 'ninja', 'cmake', 'ctest', 'cargo', 'rustc', 'msbuild') } | ForEach-Object ProcessName | Sort-Object -Unique)
        Out-Result @{task = $task; job = $job; game = $game; heavy = $heavy; cFreeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1); dFreeGb = [math]::Round((Get-PSDrive D).Free / 1GB, 1)}
    }
    'sync' {
        try {
            $sides = [ordered]@{source = @($SourceCheckout, $Source, $SourceTree); library = @($LibraryCheckout, $Library, $LibraryTree)}
            foreach ($side in $sides.Keys) {
                $path, $commit, $tree = $sides[$side]
                $ref = "refs/sfv-publish/$Label/$side"
                Invoke-AgentGit $path @('fetch', '--quiet', '--force', (Join-Path $Work "$side.bundle"), "+${ref}:$ref") | Out-Null
                if ((Invoke-AgentGit $path @('rev-parse', $ref)) -cne $commit) { throw "The $side bundle holds another commit than $commit" }
                $status = Invoke-AgentGit $path @('status', '--porcelain', '--untracked-files=normal')
                if ($status) { throw "$path has local changes; nothing moved." }
                $head = Invoke-AgentGit $path @('rev-parse', 'HEAD')
                if ($head -cne $commit) {
                    & git.exe -c safe.directory=* -C $path merge-base --is-ancestor $head $commit
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
            Save-Json (Join-Path $Work 'job.json') ([ordered]@{label = $Label; source = $SourceCheckout; library = $LibraryCheckout; buildDev = $BuildDev
                buildPlayer = $BuildPlayer; vcpkgRoot = $VcpkgRoot; renderStore = $RenderStore; oldPackageCheck = $OldPackageCheck; dist = $Dist})
            Write-JobCmd $JobName 'job'
            Out-Result @{ok = $true; sourceFingerprint = $checkout.source.fingerprint; libraryFingerprint = $checkout.library.fingerprint}
        } catch { Out-Result @{ok = $false; error = $_.Exception.Message} }
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
            Invoke-Step 'build-development' (Join-Path $job.source 'scripts\build.ps1') (@('-BuildDir', $job.buildDev) + $common)
            Invoke-Step 'build-player' (Join-Path $job.source 'scripts\build.ps1') (@('-Player', '-BuildDir', $job.buildPlayer) + $common)
            Invoke-Step 'helper' (Join-Path $job.source 'scripts\build-helper.ps1') @('-BuildDir', $job.buildPlayer, '-EmberRepo', $job.library)
            if (Test-Path -LiteralPath $job.dist) { throw "The package folder $($job.dist) exists already; use a new attempt." }
            New-Item -ItemType Directory -Path $job.dist | Out-Null
            $package = @('-BuildDir', $job.buildPlayer, '-OutDir', $job.dist, '-VersionLabel', $job.label, '-RenderStore', $job.renderStore)
            if ($job.oldPackageCheck) { $package += @('-OldPackageCheck', $job.oldPackageCheck) }
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
            Copy-Item -LiteralPath (Join-Path $Work 'checkout.json') (Join-Path $out 'checkout.json')
            $result.ctest = @{development = (Get-CTest (Join-Path $Work 'build-development.log')); player = (Get-CTest (Join-Path $Work 'build-player.log'))}
            $result.zipSha256 = Sha (Join-Path $out "sfv-ember-$($job.label).zip")
            $result.machineScan = @(Get-ChildItem -LiteralPath $folder -Recurse -File | ForEach-Object { @{file = $_.FullName.Substring($folder.Length + 1); hits = (Get-MachineHits $_.FullName)} } | Where-Object { $_.hits })
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
            $names = if ($Mode -ceq 'check') { @('sfve-room-host', 'sf4-net') } else { @('sfv.tar', 'library.tar') }
            if ((Sha (Join-Path $Work $names[0])) -cne $SfvTar -or (Sha (Join-Path $Work $names[1])) -cne $LibraryTar) { throw 'The inputs on this PC are not the desktop''s.' }
            if ($BuildId -cnotmatch '^sfv-ember/[0-9]+/[0-9]+\.[0-9]+/[0-9a-f]{16}/layout-[0-9a-f]{16}$') { throw "Not a room build: $BuildId" }
            Save-Json (Join-Path $Work 'job.json') @{buildId = $BuildId; mode = $Mode}
            Write-JobCmd $JobName 'rooms-job'
            Out-Result @{ok = $true}
        } catch { Out-Result @{ok = $false; error = $_.Exception.Message} }
    }
    'rooms-job' {
        Save-Json (Join-Path $Work 'started.json') @{at = (Get-Date).ToUniversalTime().ToString('o')}
        $result = [ordered]@{ok = $false}
        try {
            $job = Get-Content -LiteralPath (Join-Path $Work 'job.json') -Raw | ConvertFrom-Json
            $drive = $Work.Substring(0, 1).ToLowerInvariant()
            $mnt = "/mnt/$drive/$($Work.Substring(3).Replace('\', '/'))"
            $log = Join-Path $Work 'rooms.log'
            $flag = if ($job.mode -ceq 'check') { '--check ' } else { '' }
            $in = if ($job.mode -ceq 'check') { "$mnt/kit-in" } else { $mnt }
            if ($job.mode -ceq 'check') { New-Item -ItemType Directory -Path (Join-Path $Work 'kit-in') -Force | Out-Null; Copy-Item -LiteralPath (Join-Path $Work 'sfve-room-host'), (Join-Path $Work 'sf4-net') -Destination (Join-Path $Work 'kit-in') -Force }
            & wsl -e bash -lc "bash '$mnt/build-rooms-linux.sh' $flag'$in' '$mnt/out' '$($job.buildId)'" *> $log
            $result.exit = $LASTEXITCODE
            $text = if (Test-Path -LiteralPath (Join-Path $Work 'out\result.txt')) { Get-Content -LiteralPath (Join-Path $Work 'out\result.txt') -Raw } else { '' }
            $result.ok = ($result.exit -eq 0 -and $text -cmatch '(?m)^STATUS=ok')
            if (!$result.ok) { $result.error = "the WSL build ended with $($result.exit); see $log" }
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
        } elseif ((Test-Path -LiteralPath (Join-Path $Interactive 'job.cmd')) -and (Get-Content -LiteralPath (Join-Path $Interactive 'job.cmd') -Raw).Contains($Work)) {
            Out-Result @{state = 'starting'; tail = 'the task has the job'}
        } else {
            Out-Result @{state = 'new'; tail = ''}
        }
    }
}
