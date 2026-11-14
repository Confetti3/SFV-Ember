#Requires -Version 7
# The build, rooms, draft and promote stages (dot-sourced by SfvPublish.ps1). Every command that leaves this PC goes
# through $Ctx.Ops (SfvPublishOperations.ps1 in the field, fakes in tests/PublishTests.ps1).

# ---------------------------------------------------------------------------------------------------------------
# The laptop: its agent (scripts/publish/laptop-agent.ps1, copied for each attempt) and the Interactive task queue.

function ConvertTo-PubWslPath([string]$WindowsPath) {
    if ($WindowsPath -cnotmatch '^([A-Za-z]):[\\/](.*)$') { throw "Not a drive path: $WindowsPath" }
    "/mnt/$($Matches[1].ToLowerInvariant())/$($Matches[2].Replace('\', '/'))"
}
function Invoke-PubLaptop([object]$Ctx, [string]$Command, [int]$TimeoutSeconds = 300) {
    $result = & $Ctx.Ops.Ssh $Ctx.Config.laptop.host $Command $null $TimeoutSeconds
    $text = ConvertTo-PubText $result
    if ($result.exitCode) { throw "The laptop command failed ($($result.exitCode)): $Command`n$text`n$($result.stderr)" }
    $text
}
# Runs a phase of the laptop agent in $Work and returns its RESULT line as an object.
function Invoke-PubAgent([object]$Ctx, [string]$Work, [string]$Phase, [string[]]$Arguments = @(), [int]$TimeoutSeconds = 600) {
    $agent = "$Work\laptop-agent.ps1"
    # Name/value pairs; a pair with an empty value is left out (Windows PowerShell, the build PC's SSH shell, drops
    # empty arguments, which would shift every later one).
    $pairs = for ($i = 0; $i -lt $Arguments.Count; $i += 2) { if ("$($Arguments[$i + 1])") { "$($Arguments[$i]) '$($Arguments[$i + 1].Replace("'", "''"))'" } }
    $quoted = @($pairs) -join ' '
    $text = Invoke-PubLaptop $Ctx "pwsh -NoProfile -ExecutionPolicy Bypass -File '$agent' -Phase $Phase -Work '$Work' $quoted" $TimeoutSeconds
    $line = @($text -split "`r?`n" | Where-Object { $_ -cmatch '^RESULT ' }) | Select-Object -Last 1
    if (!$line) { throw "The laptop agent ($Phase) gave no result:`n$text" }
    $line.Substring(7) | ConvertFrom-Json
}
function Copy-PubToLaptop([object]$Ctx, [string]$Local, [string]$Remote, [int]$TimeoutSeconds = 900) {
    $result = & $Ctx.Ops.Scp $Local "$($Ctx.Config.laptop.host):$($Remote.Replace('\', '/'))" $TimeoutSeconds
    if ($result.exitCode) { throw "Could not copy $Local to the laptop: $($result.stderr)" }
}
function Copy-PubFromLaptop([object]$Ctx, [string]$Remote, [string]$Local, [int]$TimeoutSeconds = 900) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Local) -Force | Out-Null
    $result = & $Ctx.Ops.Scp "$($Ctx.Config.laptop.host):$($Remote.Replace('\', '/'))" $Local $TimeoutSeconds
    if ($result.exitCode) { throw "Could not copy $Remote from the laptop: $($result.stderr)" }
}
# The laptop must be free: no game, no build, no job of another caller running in the Interactive task.
function Assert-PubLaptopFree([object]$Ctx, [string]$Work) {
    $status = Invoke-PubAgent $Ctx $Work 'status' @('-Interactive', $Ctx.Config.laptop.interactive)
    if ($status.game) { throw "The laptop runs the game ($($status.game -join ', ')); try again when it is closed." }
    if ($status.task -cne 'Ready') { throw "The laptop's Interactive task is $($status.task) (a job runs: $($status.job)); one heavy build at a time." }
    if (@($status.heavy).Count) { throw "The laptop is building ($(@($status.heavy) -join ', ')); one heavy build at a time." }
    $status
}
# Starts a job through the laptop's queue (sfv-start.ps1) and waits, polling, until its result exists.
function Wait-PubLaptopJob([object]$Ctx, [string]$Work, [string]$JobName, [int]$StartMinutes, [int]$RunMinutes) {
    $poll = [int]$Ctx.Config.laptop.pollSeconds
    $deadline = (& $Ctx.Ops.Now).AddMinutes($StartMinutes)
    $state = Invoke-PubAgent $Ctx $Work 'poll'
    if ($state.state -ceq 'new') {
        while ($true) {
            $start = Invoke-PubLaptop $Ctx "powershell -NoProfile -ExecutionPolicy Bypass -File '$($Ctx.Config.laptop.interactive)\sfv-start.ps1' -Job $JobName"
            if ($start -cmatch 'STARTED') { Write-PubLog $Ctx "Laptop job $JobName started."; break }
            if ($start -cmatch 'ERROR') { throw "The laptop queue refused $JobName`: $start" }
            if ((& $Ctx.Ops.Now) -gt $deadline) { throw "$JobName did not start within $StartMinutes minutes ($($start.Trim())); run the stage again to keep waiting." }
            Write-PubLog $Ctx "Laptop job $JobName waits: $($start.Trim())"
            & $Ctx.Ops.Sleep $poll
        }
    }
    $deadline = (& $Ctx.Ops.Now).AddMinutes($RunMinutes)
    while ($true) {
        $state = Invoke-PubAgent $Ctx $Work 'poll'
        if ($state.state -cin @('done', 'failed')) { return $state }
        if ((& $Ctx.Ops.Now) -gt $deadline) { throw "$JobName has not finished after $RunMinutes minutes (last: $($state.tail)); run the stage again to keep waiting, nothing is started twice." }
        Write-PubLog $Ctx "Laptop job $JobName $($state.state): $($state.tail)"
        & $Ctx.Ops.Sleep $poll
    }
}
# A stage's laptop attempt: a folder of its own under the laptop's work folder, recorded in <stage>\attempt.json.
function Get-PubAttempt([object]$Ctx, [string]$Stage, [switch]$New) {
    $path = Join-Path (Get-PubStageDir $Ctx $Stage) 'attempt.json'
    if ((Test-Path -LiteralPath $path) -and !$New) { return Read-PubJson $path }
    $number = 1
    if (Test-Path -LiteralPath $path) { $number = [int](Read-PubJson $path).number + 1; Move-Item -LiteralPath $path -Destination "$path.$($number - 1)" }
    $attempt = [pscustomobject]@{
        number = $number; job = "sfv-publish-$($Ctx.Label -replace '[^A-Za-z0-9]', '')-$Stage-$number"
        work = "$($Ctx.Config.laptop.work)\$($Ctx.Label)\$Stage-$number"; startedAt = Get-PubNow $Ctx
    }
    Write-PubText $path (ConvertTo-PubJsonText $attempt)
    $attempt
}

# ---------------------------------------------------------------------------------------------------------------
# Build: development and player builds with their tests, the helper and the package, on the laptop.

function Invoke-PubBuild([object]$Ctx, [hashtable]$P) {
    $planRead = Read-PubPlan $Ctx
    $plan = $planRead.plan
    if (!(Test-PubStageToDo $Ctx 'build' $planRead -Redo:$P.Redo)) { return }
    $rules = Read-PubRulesForPlan $Ctx $plan $P.Rules
    $dir = Get-PubStageDir $Ctx 'build'
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $attempt = if ((Test-Path -LiteralPath (Join-Path $dir 'attempt.json')) -and !$P.NewAttempt) { Get-PubAttempt $Ctx 'build' } else { $null }
    $lap = $Ctx.Config.laptop
    if ($Ctx.DryRun) {
        Write-PubLog $Ctx "Dry run: would sync $($plan.source.commit) and library $($plan.library.commit) to the laptop, build development ($($lap.buildDev)) and player ($($lap.buildPlayer)) with tests, the helper, and package $($plan.label)."
        return
    }
    if (!$attempt) {
        $attempt = Get-PubAttempt $Ctx 'build' -New
        Write-PubLog $Ctx "Build attempt $($attempt.number) in $($attempt.work) on the laptop."
        Invoke-PubLaptop $Ctx "pwsh -NoProfile -Command `"New-Item -ItemType Directory -Force -Path '$($attempt.work)' | Out-Null`"" | Out-Null
        Copy-PubToLaptop $Ctx $Ctx.AgentPath "$($attempt.work)\laptop-agent.ps1"
        $null = Assert-PubLaptopFree $Ctx $attempt.work
        # The two commits travel as bundles of a ref made for them.
        $bundles = Join-Path $dir 'bundles'
        New-Item -ItemType Directory -Path $bundles -Force | Out-Null
        foreach ($side in @(@('source', $Ctx.Repo, $plan.source.commit), @('library', $Ctx.LibraryRepo, $plan.library.commit))) {
            $ref = "refs/sfv-publish/$($Ctx.Label)/$($side[0])"
            Invoke-PubGit $Ctx $side[1] @('update-ref', $ref, $side[2]) | Out-Null
            $bundle = Join-Path $bundles "$($side[0]).bundle"
            if (Test-Path -LiteralPath $bundle) { Remove-Item -LiteralPath $bundle }
            Invoke-PubGit $Ctx $side[1] @('bundle', 'create', $bundle, $ref) | Out-Null
            Copy-PubToLaptop $Ctx $bundle "$($attempt.work)\$($side[0]).bundle"
        }
        $sync = Invoke-PubAgent $Ctx $attempt.work 'sync' @(
            '-Label', $plan.label, '-Source', $plan.source.commit, '-SourceTree', $plan.source.tree, '-Library', $plan.library.commit, '-LibraryTree', $plan.library.tree,
            '-SourceCheckout', $lap.sourceCheckout, '-LibraryCheckout', $lap.libraryCheckout, '-BuildDev', $lap.buildDev, '-BuildPlayer', $lap.buildPlayer,
            '-VcpkgRoot', $lap.vcpkgRoot, '-RenderStore', $lap.renderStore, '-OldPackageCheck', $lap.oldPackageCheck, '-Dist', "$($lap.dist)\$($plan.label)-$($attempt.number)",
            '-Interactive', $lap.interactive, '-JobName', $attempt.job) 1800
        if (!$sync.ok) { throw "The laptop could not check out the plan's commits: $($sync.error)" }
        Write-PubLog $Ctx "Laptop checkouts at the plan's commits; source fingerprint $($sync.sourceFingerprint)."
    }
    $state = Wait-PubLaptopJob $Ctx $attempt.work $attempt.job ([int]$lap.startTimeoutMinutes) ([int]$lap.buildTimeoutMinutes)
    if ($state.state -ceq 'failed') { throw "The laptop build failed: $($state.tail)`nIts log: $($attempt.work)\job.log. Fix it and run -Stage build -NewAttempt." }
    # Bring back what the build made; the desktop checks everything itself.
    $fetch = Join-Path $dir 'files'
    if (Test-Path -LiteralPath $fetch) { Remove-Item -LiteralPath $fetch -Recurse -Force }
    foreach ($name in @('result.json', 'checkout.json', 'job.log', 'receipt-development.json', 'receipt-player.json', 'helper.provenance.json', 'build-provenance.json', $plan.zip, "$($plan.zip).sha256")) {
        Copy-PubFromLaptop $Ctx "$($attempt.work)\out\$name" (Join-Path $fetch $name)
    }
    $facts = Assert-PubBuildFiles $Ctx $planRead $fetch $rules
    $files = @(Get-ChildItem -LiteralPath $fetch -File | Sort-Object Name | ForEach-Object { Get-PubFileRecord $dir "files\$($_.Name)" })
    $facts.attempt = $attempt.number
    $facts.agentSha256 = Get-PubSha256 $Ctx.AgentPath
    $null = Write-PubEvidence $Ctx 'build' (New-PubEvidence $Ctx 'build' $planRead $files $facts)
    Write-PubLog $Ctx "Build done: $($plan.zip) $($facts.zipSha256)."
}

# The rules files the plan pinned, by SHA-256: a later stage scans with exactly those.
function Read-PubRulesForPlan([object]$Ctx, [object]$Plan, [string[]]$Paths) {
    $given = @(Get-PubRulesFingerprint $Paths)
    if ((@($given) -join ',') -cne (@($Plan.rules.sha256) -join ',')) { throw 'These rules files are not the ones the plan was made with (-Rules, same files in the same order).' }
    Read-PubRules $Paths (Get-PubMachineNames)
}

# Everything about the build's files, from the files alone (the build stage and promote both run it).
function Assert-PubBuildFiles([object]$Ctx, [object]$PlanRead, [string]$Folder, [object[]]$Rules) {
    $plan = $PlanRead.plan
    $result = Read-PubJson (Join-Path $Folder 'result.json')
    $checkout = Read-PubJson (Join-Path $Folder 'checkout.json')
    if (!$result.ok -or @($result.steps | Where-Object { $_.exit -ne 0 }).Count) { throw 'The laptop job did not finish every step.' }
    if ($checkout.source.commit -cne $plan.source.commit -or $checkout.source.tree -cne $plan.source.tree -or
        $checkout.library.commit -cne $plan.library.commit -or $checkout.library.tree -cne $plan.library.tree) { throw 'The laptop checked out other commits than the plan''s.' }
    if (!$checkout.source.clean -or !$checkout.library.clean) { throw 'The laptop checkouts were not clean.' }
    foreach ($fp in @($checkout.source.fingerprint, $checkout.library.fingerprint)) { if ("$fp" -cnotmatch '^[0-9A-F]{64}$') { throw 'The laptop recorded no source fingerprint.' } }
    if ($result.source.fingerprint -cne $checkout.source.fingerprint -or $result.library.fingerprint -cne $checkout.library.fingerprint) { throw 'The laptop checkouts changed between the sync and the end of the build.' }
    $tests = $null
    foreach ($kind in @('development', 'player')) {
        $receipt = Read-PubJson (Join-Path $Folder "receipt-$kind.json")
        if ($receipt.product -cne 'SFV Ember' -or $receipt.version -cne $plan.version) { throw "The $kind receipt is not SFV Ember $($plan.version) ($($receipt.version))." }
        if ($receipt.sourceRevision -cne $plan.source.commit -or $receipt.emberRevision -cne $plan.library.commit) { throw "The $kind receipt is for source $($receipt.sourceRevision) and library $($receipt.emberRevision), not the plan's commits." }
        if ($receipt.sourceFingerprint -cne $checkout.source.fingerprint -or $receipt.emberFingerprint -cne $checkout.library.fingerprint) { throw "The $kind receipt's fingerprints are not those of the plan's checkouts." }
        if ($receipt.buildKind -cne $kind -or $receipt.testsPassed -ne $true -or !@($receipt.tests).Count) { throw "The $kind receipt is not a $kind build whose tests passed." }
        $ran = $result.ctest.$kind
        if (!$ran -or [int]$ran.total -ne @($receipt.tests).Count -or [int]$ran.failed -ne 0 -or [int]$ran.passed -ne [int]$ran.total) { throw "The $kind tests did not all run and pass ($($ran.passed) of $($ran.total))." }
        if ($null -eq $tests) { $tests = @($receipt.tests) } elseif ((@($receipt.tests) -join ',') -cne ($tests -join ',')) { throw 'The development and player builds ran different tests.' }
    }
    $helper = Read-PubJson (Join-Path $Folder 'helper.provenance.json')
    if ($helper.emberRevision -cne $plan.library.commit -or $helper.emberFingerprint -cne $checkout.library.fingerprint) { throw 'The helper was not built from the plan''s library checkout.' }
    # The zip: its hash, its sidecar, and the receipt inside it.
    $zip = Join-Path $Folder $plan.zip
    $zipSha = Get-PubSha256 $zip
    $sidecar = [IO.File]::ReadAllText("$zip.sha256").Trim()
    if ($sidecar -cnotmatch "^([0-9a-fA-F]{64})(\s+\*?$([regex]::Escape($plan.zip)))?$" -or $Matches[1].ToLowerInvariant() -cne $zipSha) { throw 'The zip''s .sha256 does not name the zip.' }
    if ([string]$result.zipSha256 -cne $zipSha) { throw 'The zip that came back is not the one the laptop packaged.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    $temp = Join-Path $Ctx.Root "tmp\scan-$([Guid]::NewGuid().ToString('N'))"
    try {
        $prefix = "$($plan.zip -replace '\.zip$', '')/"
        $entries = @($archive.Entries | Where-Object { $_.Length -gt 0 -or !$_.FullName.EndsWith('/') })
        if (@($entries | Where-Object { !$_.FullName.Replace('\', '/').StartsWith($prefix) }).Count) { throw "The zip holds files outside $prefix" }
        $read = { param($Name) $entry = $entries | Where-Object { $_.FullName.Replace('\', '/') -ceq "$prefix$Name" } | Select-Object -First 1
            if (!$entry) { throw "The zip has no $Name" }
            $stream = $entry.Open(); $memory = [IO.MemoryStream]::new(); try { $stream.CopyTo($memory) } finally { $stream.Dispose() }; ,$memory.ToArray() }
        $inner = [byte[]](& $read 'build-provenance.json')
        if ((Get-PubBytesSha256 $inner) -cne (Get-PubSha256 (Join-Path $Folder 'build-provenance.json'))) { throw 'The zip''s build-provenance.json is not the packaged receipt that came back.' }
        $packaged = [Text.UTF8Encoding]::new($false).GetString($inner).TrimStart([char]0xFEFF) | ConvertFrom-Json
        if ($packaged.sourceRevision -cne $plan.source.commit -or $packaged.emberRevision -cne $plan.library.commit -or $packaged.buildKind -cne 'player' -or
            $packaged.sourceFingerprint -cne $checkout.source.fingerprint -or $packaged.package.label -cne $plan.label -or $packaged.testsPassed -ne $true) { throw 'The zip''s receipt is not a player build of the plan''s commits labelled for this release.' }
        $player = Read-PubJson (Join-Path $Folder 'receipt-player.json')
        foreach ($binary in @($player.binaries)) {
            $bytes = [byte[]](& $read $binary.path)
            if ((Get-PubBytesSha256 $bytes).ToUpperInvariant() -cne ([string]$binary.sha256).ToUpperInvariant()) { throw "The zip's $($binary.path) is not the player build's." }
        }
        $dll = [Text.Encoding]::Latin1.GetString([byte[]](& $read 'sfv_ember.dll'))
        if (!$dll.Contains($plan.roomBuild.id)) { throw "sfv_ember.dll does not carry the plan's room build $($plan.roomBuild.id)." }
        if (!$dll.Contains($plan.roomBuild.matchLine)) { throw "sfv_ember.dll does not carry the match line $($plan.roomBuild.matchLine)." }
        # Every file and file name of the package, raw and UTF-16.
        $hits = [Collections.Generic.List[object]]::new()
        $count = 0
        foreach ($entry in $entries) {
            $stream = $entry.Open(); $memory = [IO.MemoryStream]::new(); try { $stream.CopyTo($memory) } finally { $stream.Dispose() }
            foreach ($hit in (Find-PubHits $Rules $memory.ToArray() "package $($entry.FullName)")) { $hits.Add($hit) }
            foreach ($hit in (Find-PubHits $Rules ([Text.UTF8Encoding]::new($false).GetBytes($entry.FullName)) "package name $($entry.FullName)")) { $hits.Add($hit) }
            $count++
        }
        Assert-PubNoHits @($hits) 'The package'
        if (@($result.machineScan | Where-Object { [int]$_.hits -ne 0 }).Count) { throw "The laptop's own names appear in the package: $(@($result.machineScan | Where-Object { [int]$_.hits -ne 0 } | ForEach-Object { $_.file }) -join ', ')" }
    } finally {
        $archive.Dispose()
        if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
    }
    @{zip = $plan.zip; zipSha256 = $zipSha; zipSize = (Get-Item -LiteralPath $zip).Length; packageFiles = $count; tests = $tests.Count
      sourceFingerprint = $checkout.source.fingerprint; libraryFingerprint = $checkout.library.fingerprint; roomBuild = $plan.roomBuild.id}
}

# ---------------------------------------------------------------------------------------------------------------
# Rooms: the Linux room host pair from the plan's commits, its kit staged on the VPS for the owner's sudo.

function Invoke-PubRooms([object]$Ctx, [hashtable]$P) {
    $planRead = Read-PubPlan $Ctx
    $plan = $planRead.plan
    $dir = Get-PubStageDir $Ctx 'rooms'
    $evidencePath = Get-PubEvidencePath $Ctx 'rooms'
    if ((Test-Path -LiteralPath $evidencePath) -and !$P.Redo) {
        $evidence = Read-PubEvidence $Ctx 'rooms' $planRead
        Write-PubLog $Ctx "rooms is built already (evidence $($evidence.sha256), checked again)."
        Publish-PubRoomKit $Ctx $plan $evidence
        return
    }
    $null = Test-PubStageToDo $Ctx 'rooms' $planRead -Redo:$P.Redo
    $rules = Read-PubRulesForPlan $Ctx $plan $P.Rules
    if ($P.AdoptKit) { Invoke-PubRoomsAdopt $Ctx $planRead $rules $P; return }
    # Promote compares the installed binaries with this stage's hashes, so every rooms evidence names a pair: a room
    # build the last release already installed is adopted from its kit, not skipped.
    if (!$plan.roomBuild.changed -and !$P.Force) {
        throw "The room build $($plan.roomBuild.id) is $($plan.previous.tag)'s, which the VPS has installed: adopt that kit (-AdoptKit <folder> -KitSource <sfv commit> -KitLibrary <library commit>), or build it again with -Force."
    }
    $backend = if ($P.LinuxBackend) { $P.LinuxBackend } else { $Ctx.Config.rooms.backend }
    if ($backend -cnotin @('wsl', 'server1')) { throw "No Linux back end $backend (wsl or server1)." }
    if ($Ctx.DryRun) { Write-PubLog $Ctx "Dry run: would archive $($plan.source.commit) and $($plan.library.commit), build sf4-net and sfve-room-host for $($plan.roomBuild.id) on $backend, and stage the kit."; return }
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    # The inputs: git archives of exactly the plan's commits, no line-ending conversion.
    $inputs = Join-Path $dir 'inputs'
    if (!(Test-Path -LiteralPath (Join-Path $inputs 'sfv.tar')) -or !(Test-Path -LiteralPath (Join-Path $inputs 'library.tar'))) {
        New-Item -ItemType Directory -Path $inputs -Force | Out-Null
        Invoke-PubGit $Ctx $Ctx.Repo @('-c', 'core.autocrlf=false', 'archive', '--format=tar', '-o', (Join-Path $inputs 'sfv.tar'), $plan.source.commit, 'src', 'scripts', 'data') | Out-Null
        Invoke-PubGit $Ctx $Ctx.LibraryRepo @('-c', 'core.autocrlf=false', 'archive', '--format=tar', '-o', (Join-Path $inputs 'library.tar'), $plan.library.commit, 'server', 'rust', 'src') | Out-Null
    }
    $out = Join-Path $dir 'out'
    if ($backend -ceq 'wsl') { Invoke-PubRoomsWsl $Ctx $plan $inputs $out $P }
    else { Invoke-PubRoomsServer1 $Ctx $plan $inputs $out $P }
    $facts = Assert-PubRoomFiles $Ctx $plan $out $rules
    $facts.backend = $backend
    $facts.inputs = @{sfvTar = (Get-PubSha256 (Join-Path $inputs 'sfv.tar')); libraryTar = (Get-PubSha256 (Join-Path $inputs 'library.tar'))}
    $facts.buildScript = Get-PubSha256 $Ctx.RoomScriptPath
    $kit = New-PubRoomKit $Ctx $plan $dir $out
    $facts.kit = $kit
    $files = @(@('out\sfve-room-host', 'out\sf4-net', 'out\SHA256SUMS', 'out\result.txt', 'kit.tar', 'kit-SHA256SUMS') | ForEach-Object { Get-PubFileRecord $dir $_ })
    $null = Write-PubEvidence $Ctx 'rooms' (New-PubEvidence $Ctx 'rooms' $planRead $files $facts)
    $evidence = Read-PubEvidence $Ctx 'rooms' $planRead
    Publish-PubRoomKit $Ctx $plan $evidence
}

# -Check: the kit's two binaries (in $Inputs) are checked by the same script in its --check mode; nothing is built.
function Invoke-PubRoomsWsl([object]$Ctx, [object]$Plan, [string]$Inputs, [string]$Out, [hashtable]$P, [switch]$Check) {
    $lap = $Ctx.Config.laptop
    $attempt = if ((Test-Path -LiteralPath (Join-Path (Get-PubStageDir $Ctx 'rooms') 'attempt.json')) -and !$P.NewAttempt) { Get-PubAttempt $Ctx 'rooms' } else { $null }
    if (!$attempt) {
        $attempt = Get-PubAttempt $Ctx 'rooms' -New
        Invoke-PubLaptop $Ctx "pwsh -NoProfile -Command `"New-Item -ItemType Directory -Force -Path '$($attempt.work)' | Out-Null`"" | Out-Null
        Copy-PubToLaptop $Ctx $Ctx.AgentPath "$($attempt.work)\laptop-agent.ps1"
        if (!$Check) { $null = Assert-PubLaptopFree $Ctx $attempt.work }
        Copy-PubToLaptop $Ctx $Ctx.RoomScriptPath "$($attempt.work)\build-rooms-linux.sh"
        $names = if ($Check) { @('sfve-room-host', 'sf4-net') } else { @('sfv.tar', 'library.tar') }
        foreach ($name in $names) { Copy-PubToLaptop $Ctx (Join-Path $Inputs $name) "$($attempt.work)\$name" }
        $job = Invoke-PubAgent $Ctx $attempt.work 'rooms-install' @('-BuildId', $Plan.roomBuild.id, '-Interactive', $lap.interactive, '-JobName', $attempt.job,
            '-Mode', $(if ($Check) { 'check' } else { 'build' }),
            '-SfvTar', (Get-PubSha256 (Join-Path $Inputs $names[0])), '-LibraryTar', (Get-PubSha256 (Join-Path $Inputs $names[1])))
        if (!$job.ok) { throw "The laptop could not prepare the room-host job: $($job.error)" }
    }
    $state = Wait-PubLaptopJob $Ctx $attempt.work $attempt.job ([int]$lap.startTimeoutMinutes) ([int]$lap.roomsTimeoutMinutes)
    if ($state.state -ceq 'failed') { throw "The room-host build failed: $($state.tail)`nIts log: $($attempt.work)\job.log. Run -Stage rooms -NewAttempt after fixing it." }
    if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
    foreach ($name in @('sfve-room-host', 'sf4-net', 'SHA256SUMS', 'result.txt', 'job.log')) { Copy-PubFromLaptop $Ctx "$($attempt.work)\out\$name" (Join-Path $Out $name) }
}
function Invoke-PubRoomsServer1([object]$Ctx, [object]$Plan, [string]$Inputs, [string]$Out, [hashtable]$P) {
    # The second back end: a workflow in the private builds repository on the self-hosted server1 runner
    # (gh workflow run <file> -f ref=... -f runner=server1), its artifact downloaded into $Out with the same four
    # files the WSL job leaves. Not written yet: the builds repository has no SFV room-host workflow.
    throw 'The server1 back end is not implemented yet (no SFV room-host workflow in the builds repository); use -LinuxBackend wsl.'
}

# The pair, checked from the files: Linux binaries, the hashes SHA256SUMS names, the room host's own answers for
# its build and another, the build it embeds, no build-machine path, no forbidden string.
# -Adopted: the files are a kit's, checked by the script's --check mode (no archives; the hashes must be the kit's).
function Assert-PubRoomFiles([object]$Ctx, [object]$Plan, [string]$Out, [object[]]$Rules, [switch]$Adopted, [hashtable]$KitHashes = @{}) {
    $result = @{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $Out 'result.txt'))) { if ($line -cmatch '^([A-Z_]+)=(.*)$') { $result[$Matches[1]] = $Matches[2] } }
    if ($result.STATUS -cne 'ok') { throw "The room-host build did not finish (STATUS $($result.STATUS))." }
    if ($result.BUILD_ID -cne $Plan.roomBuild.id) { throw "The room host was built for $($result.BUILD_ID), not $($Plan.roomBuild.id)." }
    if ($Adopted) {
        if ($result.MODE -cne 'check') { throw 'The kit was not checked in check mode.' }
    } elseif ($result.SFV_TAR -cne (Get-PubSha256 (Join-Path (Get-PubStageDir $Ctx 'rooms') 'inputs\sfv.tar')) -or
        $result.LIBRARY_TAR -cne (Get-PubSha256 (Join-Path (Get-PubStageDir $Ctx 'rooms') 'inputs\library.tar'))) { throw 'The room host was built from other archives than the plan''s.' }
    if ($result.ANSWER_OWN -cne 'helper_unavailable') { throw "The room host answers $($result.ANSWER_OWN) for its own build (helper_unavailable expected)." }
    if ($result.ANSWER_OTHER -cne 'other_build') { throw "The room host answers $($result.ANSWER_OTHER) for another build (other_build expected)." }
    if ($result.EMBEDDED -cne $Plan.roomBuild.id) { throw "The room host embeds $($result.EMBEDDED), not $($Plan.roomBuild.id)." }
    foreach ($key in @($result.Keys | Where-Object { $_ -clike 'PATHS_*' })) { if ($result[$key] -cne '0') { throw "The Linux build left build-machine paths in its binaries ($key=$($result[$key]))." } }
    $sums = @{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $Out 'SHA256SUMS'))) { if ($line -cmatch '^([0-9a-f]{64}) [ *]?(.+)$') { $sums[$Matches[2]] = $Matches[1] } }
    $facts = @{buildId = $Plan.roomBuild.id; adopted = [bool]$Adopted}
    foreach ($name in @('sfve-room-host', 'sf4-net')) {
        $path = Join-Path $Out $name
        $bytes = [IO.File]::ReadAllBytes($path)
        if ($bytes.Length -lt 4 -or $bytes[0] -ne 0x7F -or $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x4C -or $bytes[3] -ne 0x46) { throw "$name is not a Linux binary." }
        $sha = Get-PubBytesSha256 $bytes
        if ($sums[$name] -cne $sha) { throw "$name is not the file SHA256SUMS names." }
        if ($Adopted -and $KitHashes[$name] -cne $sha) { throw "The checked $name is not the kit's ($($KitHashes[$name]))" }
        Assert-PubNoHits (Find-PubHits $Rules $bytes "room host $name") "The Linux $name"
        $facts[$name] = $sha
    }
    $facts
}

# The kit the VPS runs `upgrade.sh rooms` from: the folder staged for the last release (config rooms.baseStage,
# checked against its own SHA256SUMS), with the library's upgrade.sh at the plan's commit and room-build/ replaced by
# this pair and its BUILD_ID; SHA256SUMS made as stage.sh makes it. Written as kit.tar.
function New-PubRoomKit([object]$Ctx, [object]$Plan, [string]$Dir, [string]$Out) {
    $base = $Ctx.Config.rooms.baseStage
    if (!$base -or !(Test-Path -LiteralPath (Join-Path $base 'SHA256SUMS'))) { throw "No base stage folder with SHA256SUMS at '$base' (config rooms.baseStage: the folder staged for the last release)." }
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $base 'SHA256SUMS'))) {
        if ($line -cnotmatch '^([0-9a-f]{64}) [ *]?(.+)$') { continue }
        if ((Get-PubSha256 (Join-Path $base $Matches[2])) -cne $Matches[1]) { throw "The base stage's $($Matches[2]) is not the file its SHA256SUMS names." }
    }
    $kit = Join-Path $Dir "kit\$($Ctx.Config.vps.stage)"
    if (Test-Path -LiteralPath (Join-Path $Dir 'kit')) { Remove-Item -LiteralPath (Join-Path $Dir 'kit') -Recurse -Force }
    New-Item -ItemType Directory -Path $kit -Force | Out-Null
    Copy-Item -Path (Join-Path $base '*') -Destination $kit -Recurse
    Remove-Item -LiteralPath (Join-Path $kit 'SHA256SUMS')
    $upgrade = Invoke-PubGit $Ctx $Ctx.LibraryRepo @('show', "$($Plan.library.commit):server/sfv-bridge/upgrade.sh")
    [IO.File]::WriteAllBytes((Join-Path $kit 'upgrade.sh'), [byte[]]$upgrade.bytes)
    $roomDir = Join-Path $kit 'room-build'
    New-Item -ItemType Directory -Path $roomDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Out 'sfve-room-host') -Destination (Join-Path $roomDir 'sfve-room-host') -Force
    Copy-Item -LiteralPath (Join-Path $Out 'sf4-net') -Destination (Join-Path $roomDir 'sf4-net') -Force
    [IO.File]::WriteAllText((Join-Path $roomDir 'BUILD_ID'), "$($Plan.roomBuild.id)`n", [Text.UTF8Encoding]::new($false))
    # Paths in byte order (LC_ALL=C sort), "<sha256>  <path>", as stage.sh writes it.
    $paths = [string[]]@(Get-ChildItem -LiteralPath $kit -Recurse -File | ForEach-Object { $_.FullName.Substring($kit.Length + 1).Replace('\', '/') })
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    $ordered = [string[]]@($paths | ForEach-Object { "$(Get-PubSha256 (Join-Path $kit $_))  $_" })
    $sumsText = ($ordered -join "`n") + "`n"
    [IO.File]::WriteAllText((Join-Path $kit 'SHA256SUMS'), $sumsText, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $kit 'SHA256SUMS') -Destination (Join-Path $Dir 'kit-SHA256SUMS') -Force
    $tar = Join-Path $Dir 'kit.tar'
    if (Test-Path -LiteralPath $tar) { Remove-Item -LiteralPath $tar }
    $made = & $Ctx.Ops.Tar (Join-Path $Dir 'kit') $Ctx.Config.vps.stage $tar
    if ($made.exitCode) { throw "Could not pack the kit: $($made.stderr)" }
    $baseUpgrade = Join-Path $base 'upgrade.sh'
    $upgradeSha = Get-PubBytesSha256 ([byte[]]$upgrade.bytes)
    @{sums = (Get-PubSha256 (Join-Path $Dir 'kit-SHA256SUMS')); files = $ordered.Count; upgradeSha256 = $upgradeSha
      upgradeChanged = ((Test-Path -LiteralPath $baseUpgrade) -and (Get-PubSha256 $baseUpgrade) -cne $upgradeSha); base = $base}
}

# Puts the kit on the VPS (no sudo): kit.tar to ~/<stage>.incoming-<label>.tar, unpacked and checked there, the old
# folder kept as ~/<stage>.before-<label>. Then the owner's command.
function Publish-PubRoomKit([object]$Ctx, [object]$Plan, [object]$Evidence) {
    if ($Evidence.facts.adopted) {
        Write-PubLog $Ctx "Adopted kit: nothing to stage. Promote checks that the VPS has room host $($Evidence.facts.'sfve-room-host'.Substring(0, 12)) and helper $($Evidence.facts.'sf4-net'.Substring(0, 12)) installed for $($Plan.roomBuild.id)."
        return
    }
    $vps = $Ctx.Config.vps
    $script = [IO.File]::ReadAllBytes($Ctx.VpsStagePath)
    $tar = Join-Path (Get-PubStageDir $Ctx 'rooms') 'kit.tar'
    $remoteTar = "$($vps.stage).incoming-$($Plan.label).tar"
    $outcome = Invoke-PubOutward $Ctx 'vps-stage' "kit.tar ($($Evidence.facts.kit.sums.Substring(0, 12)) sums) to $($vps.host):~/$($vps.stage)" {
        $state = & $Ctx.Ops.Ssh $vps.host "bash -s -- state '$($vps.stage)' '$($Plan.label)' '$($Evidence.facts.kit.sums)'" $script 120
        $now = (ConvertTo-PubText $state)
        if ($now -cmatch 'RESULT staged') { return 'staged already' }
        $copy = & $Ctx.Ops.Scp $tar "$($vps.host):$remoteTar" 900
        if ($copy.exitCode) { throw "Could not copy the kit to the VPS: $($copy.stderr)" }
        $install = & $Ctx.Ops.Ssh $vps.host "bash -s -- install '$($vps.stage)' '$($Plan.label)' '$($Evidence.facts.kit.sums)'" $script 300
        $text = ConvertTo-PubText $install
        if ($install.exitCode -or $text -cnotmatch 'RESULT staged') { throw "The VPS did not take the kit: $text $($install.stderr)" }
        'staged'
    }
    if ($outcome.done) { Write-PubLog $Ctx "VPS kit: $($outcome.result)." }
    Write-Host ''
    Write-Host 'The owner''s step on the VPS (root), then the read-only check:'
    Write-Host "  ssh -t $($vps.host) `"sudo bash ~/$($vps.stage)/upgrade.sh rooms`""
    Write-Host "  ssh -t $($vps.host) `"sudo bash ~/$($vps.stage)/upgrade.sh check`""
    Write-Host "Room build: $($Plan.roomBuild.id)"
}

# ---------------------------------------------------------------------------------------------------------------
# Draft: a draft pre-release on the public repository, from the checked files only, by id.

function Get-PubReleaseAssets([object]$Ctx, [object]$Plan) {
    $dir = Join-Path (Get-PubStageDir $Ctx 'build') 'files'
    @(@($Plan.zip, "$($Plan.zip).sha256") | ForEach-Object { $path = Join-Path $dir $_; [pscustomobject]@{name = $_; path = $path; sha256 = (Get-PubSha256 $path); size = (Get-Item -LiteralPath $path).Length} })
}
function Get-PubNotesText([object]$Ctx) { [IO.File]::ReadAllText((Join-Path $Ctx.Root 'notes.md')).Replace("`r`n", "`n").TrimEnd() }

# A release read back by id is exactly this plan's draft: tag, title, pre-release, notes and assets by digest.
function Assert-PubDraftRelease([object]$Ctx, [object]$Plan, [object]$Release, [object[]]$Assets, [switch]$Published) {
    if ($Release.tag_name -cne $Plan.tag) { throw "Release $($Release.id) is $($Release.tag_name), not $($Plan.tag)." }
    if ($Release.name -cne $Plan.title) { throw "Release $($Release.id) is titled '$($Release.name)', not '$($Plan.title)'." }
    if ($Release.prerelease -ne $true) { throw "Release $($Release.id) is not a pre-release." }
    if ($Published) { if ($Release.draft -ne $false) { throw "Release $($Release.id) is still a draft." } }
    elseif ($Release.draft -ne $true) { throw "Release $($Release.id) is not a draft any more." }
    if (([string]$Release.body).Replace("`r`n", "`n").TrimEnd() -cne (Get-PubNotesText $Ctx)) { throw "Release $($Release.id)'s notes are not the planned notes." }
    $names = @($Release.assets | ForEach-Object { $_.name } | Sort-Object)
    if (($names -join ',') -cne (@($Assets.name | Sort-Object) -join ',')) { throw "Release $($Release.id) has assets $($names -join ', '), not $(@($Assets.name) -join ', ')." }
    foreach ($asset in $Release.assets) {
        $file = $Assets | Where-Object name -CEQ $asset.name
        if ($asset.state -and $asset.state -cne 'uploaded') { throw "$($asset.name) of release $($Release.id) is not fully uploaded ($($asset.state))." }
        if ([long]$asset.size -ne [long]$file.size -or [string]$asset.digest -cne "sha256:$($file.sha256)") { throw "$($asset.name) of release $($Release.id) is not the checked file (digest $($asset.digest))." }
    }
}

function Invoke-PubDraft([object]$Ctx, [hashtable]$P) {
    $planRead = Read-PubPlan $Ctx
    $plan = $planRead.plan
    if (!(Test-PubStageToDo $Ctx 'draft' $planRead -Redo:$P.Redo)) { return }
    $build = Read-PubEvidence $Ctx 'build' $planRead
    $null = Read-PubEvidence $Ctx 'rooms' $planRead
    $dir = Get-PubStageDir $Ctx 'draft'
    $assets = Get-PubReleaseAssets $Ctx $plan
    if (($assets | Where-Object name -CEQ $plan.zip).sha256 -cne $build.facts.zipSha256) { throw 'The zip is not the one the build evidence names.' }
    $repo = $Ctx.Config.publicRepo
    $statePath = Join-Path $dir 'draft-state.json'
    $state = if (Test-Path -LiteralPath $statePath) { Read-PubJson $statePath } else { $null }
    # No tag, no release with this tag except the draft this stage created, and nothing newer published.
    $releases = @(Get-PubReleases $Ctx)
    $named = @($releases | Where-Object { $_.tag_name -ceq $plan.tag })
    $ours = if ($state) { [string]$state.releaseId } else { '' }
    $stale = @($named | Where-Object { "$($_.id)" -cne $ours })
    if ($stale.Count) { throw "$repo already has release(s) named $($plan.tag) that this stage did not make ($(@($stale.id) -join ', ')); delete stale drafts there first." }
    $newer = @($releases | Where-Object { !$_.draft -and $_.tag_name -cmatch '^v[0-9]' -and (Compare-PubTag $_.tag_name $plan.tag) -ge 0 })
    if ($newer.Count) { throw "A release as new or newer is published already: $(@($newer.tag_name) -join ', ')." }
    if (Get-PubRemoteRef $Ctx $Ctx.Repo $Ctx.Config.publicRemote "refs/tags/$($plan.tag)") { throw "The public repository has the tag $($plan.tag) already." }
    $notesPath = Join-Path $Ctx.Root 'notes.md'
    $outcome = Invoke-PubOutward $Ctx 'draft' "draft pre-release $($plan.tag) on $repo with $(@($assets.name) -join ', ')" {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        if (!$ours) {
            # Target: the default branch for now; promote names the commit when it publishes (the commit reaches the
            # public repository only when main is fast-forwarded, just before).
            $request = Join-Path $dir 'create-request.json'
            Write-PubText $request (ConvertTo-PubJsonText ([ordered]@{tag_name = $plan.tag; target_commitish = 'main'; name = $plan.title
                body = (Get-PubNotesText $Ctx); draft = $true; prerelease = $true}))
            $created = ConvertTo-PubText (Invoke-PubGh $Ctx @('api', '-X', 'POST', "repos/$repo/releases", '--input', $request)) | ConvertFrom-Json
            if ([string]$created.id -cnotmatch '^[1-9][0-9]*$' -or $created.draft -ne $true) { throw 'GitHub did not return a draft with an id.' }
            Write-PubText $statePath (ConvertTo-PubJsonText ([ordered]@{releaseId = [string]$created.id; createdAt = Get-PubNow $Ctx; planSha256 = $planRead.sha256}))
            Write-PubLog $Ctx "Draft created: release $($created.id)."
        }
        $id = [string](Read-PubJson $statePath).releaseId
        $release = Get-PubReleaseById $Ctx $id
        foreach ($asset in $assets) {
            $there = @($release.assets | Where-Object name -CEQ $asset.name) | Select-Object -First 1
            if ($there) {
                if ([string]$there.digest -ceq "sha256:$($asset.sha256)" -and $there.state -ceq 'uploaded') { continue }
                throw "Draft $id has a $($asset.name) that is not the checked file; delete the draft on GitHub and run -Stage draft -Redo."
            }
            $type = if ($asset.name.EndsWith('.zip')) { 'application/zip' } else { 'text/plain' }
            Invoke-PubGh $Ctx @('api', '-X', 'POST', "https://uploads.github.com/repos/$repo/releases/$id/assets?name=$([uri]::EscapeDataString($asset.name))",
                '-H', "Content-Type: $type", '--input', $asset.path) | Out-Null
            Write-PubLog $Ctx "Uploaded $($asset.name) to draft $id."
        }
        $id
    }
    if (!$outcome.done) { return }
    $id = [string](Read-PubJson $statePath).releaseId
    $release = Get-PubReleaseById $Ctx $id
    Assert-PubDraftRelease $Ctx $plan $release $assets
    $files = @(Get-PubFileRecord $dir 'draft-state.json')
    $facts = @{releaseId = $id; tag = $release.tag_name; url = $release.html_url; createdAt = $release.created_at
        assets = @($assets | ForEach-Object { @{name = $_.name; sha256 = $_.sha256; size = $_.size} }); notesSha256 = $plan.notes.sha256}
    $null = Write-PubEvidence $Ctx 'draft' (New-PubEvidence $Ctx 'draft' $planRead $files $facts)
    Write-PubLog $Ctx "Draft $id ready: $($release.html_url). Promote it with -Stage promote."
}

# ---------------------------------------------------------------------------------------------------------------
# Promote: everything checked again on this PC, the server gate, the pushes, the release published by id, the
# published zip downloaded and compared. Phases are recorded in promote\state.json and a re-run resumes.

function Get-PubPromoteState([object]$Ctx) {
    $path = Join-Path (Get-PubStageDir $Ctx 'promote') 'state.json'
    if (Test-Path -LiteralPath $path) { return Read-PubJson $path }
    [pscustomobject]@{phases = [pscustomobject]@{}}
}
function Save-PubPromoteState([object]$Ctx, [object]$State) {
    Write-PubText (Join-Path (Get-PubStageDir $Ctx 'promote') 'state.json') (ConvertTo-PubJsonText $State)
}
function Set-PubPhase([object]$Ctx, [object]$State, [string]$Phase, [string]$Status, [string]$Note = '') {
    if ($Ctx.DryRun) { return }
    $State.phases | Add-Member -NotePropertyName $Phase -NotePropertyValue ([pscustomobject]@{status = $Status; at = (Get-PubNow $Ctx); note = $Note}) -Force
    Save-PubPromoteState $Ctx $State
}
function Test-PubPhaseDone([object]$State, [string]$Phase) { $State.phases.$Phase -and $State.phases.$Phase.status -ceq 'done' }

# Pushes $Commit to $Remote's $Branch only as a fast-forward (or not at all when it is there already).
function Push-PubBranch([object]$Ctx, [string]$Step, [string]$Repo, [string]$Remote, [string]$Commit, [string]$Branch) {
    $tip = Get-PubRemoteRef $Ctx $Repo $Remote "refs/heads/$Branch"
    if ($tip -ceq $Commit) { Write-PubLog $Ctx "$Step`: $Remote $Branch is at $($Commit.Substring(0, 12)) already."; return 'there' }
    if ($tip -and (Test-PubObject $Ctx $Repo $tip) -and (Test-PubAncestor $Ctx $Repo $Commit $tip)) { Write-PubLog $Ctx "$Step`: $Remote $Branch already contains $($Commit.Substring(0, 12))."; return 'contained' }
    if ($tip -and (!(Test-PubObject $Ctx $Repo $tip) -or !(Test-PubAncestor $Ctx $Repo $tip $Commit))) { throw "$Remote $Branch ($tip) would not fast-forward to $Commit; nothing pushed." }
    $outcome = Invoke-PubOutward $Ctx $Step "push $($Commit.Substring(0, 12)) to $Remote $Branch (fast-forward from $(if ($tip) { $tip.Substring(0, 12) } else { 'nothing' }))" {
        $result = & $Ctx.Ops.Push $Repo $Remote $Commit "refs/heads/$Branch"
        if ($result.exitCode) { throw "git push to $Remote $Branch failed: $($result.stderr)" }
        if ((Get-PubRemoteRef $Ctx $Repo $Remote "refs/heads/$Branch") -cne $Commit) { throw "$Remote $Branch is not at $Commit after the push." }
        'pushed'
    }
    if ($outcome.done) { return 'pushed' }
    if ($outcome.skipped) { return 'skipped' }
    'dry-run'
}

# The server gate: the live VPS offers the plan's room build (read-only ssh, no sudo; scripts/publish/vps.sh check).
function Test-PubServer([object]$Ctx, [object]$Plan, [object]$Rooms, [switch]$RoomsConfirmed) {
    $vps = $Ctx.Config.vps
    $hostSha = [string]$Rooms.facts.'sfve-room-host'
    $netSha = [string]$Rooms.facts.'sf4-net'
    if ($hostSha -cnotmatch '^[0-9a-f]{64}$' -or $netSha -cnotmatch '^[0-9a-f]{64}$') { throw 'The rooms evidence names no room host and helper hashes to compare with the VPS.' }
    $result = & $Ctx.Ops.Ssh $vps.host "bash -s -- check '$($Plan.roomBuild.id)' '$hostSha' '$netSha' '$($vps.roomsLib)'" ([IO.File]::ReadAllBytes($Ctx.VpsStagePath)) 120
    $text = ConvertTo-PubText $result
    $line = @($text -split "`n" | Where-Object { $_ -cmatch '^RESULT ' }) | Select-Object -Last 1
    $answer = if ($line) { $line.Substring(7).Trim() } else { 'no-answer' }
    Write-PubLog $Ctx "Server check: $answer"
    if ($answer -ceq 'offered') { return 'offered' }
    if ($answer -ceq 'installed-config-unreadable' -and $RoomsConfirmed) {
        Write-PubLog $Ctx 'The room build is installed and answers for itself; the supervisor''s list is root-only, and the owner confirmed (-RoomsConfirmed) that upgrade.sh rooms/check show it.'
        return 'installed+confirmed'
    }
    if ($answer -ceq 'installed-config-unreadable') { throw "The room build is installed on the VPS, but whether the supervisor offers it needs root to read: run `"sudo bash ~/$($vps.stage)/upgrade.sh check`", and if it lists $($Plan.roomBuild.id), promote again with -RoomsConfirmed." }
    throw "The live server does not offer $($Plan.roomBuild.id) yet ($answer): the owner runs `"ssh -t $($vps.host) sudo bash ~/$($vps.stage)/upgrade.sh rooms`" first."
}

function Invoke-PubPromote([object]$Ctx, [hashtable]$P) {
    $planRead = Read-PubPlan $Ctx
    $plan = $planRead.plan
    $rules = Read-PubRulesForPlan $Ctx $plan $P.Rules
    $dir = Get-PubStageDir $Ctx 'promote'
    if (Test-Path -LiteralPath (Get-PubEvidencePath $Ctx 'promote')) {
        $done = Read-PubEvidence $Ctx 'promote' $planRead
        Write-PubLog $Ctx "$($plan.tag) is promoted already (evidence $($done.sha256)); $($done.facts.url)"
        return
    }
    if (!$Ctx.DryRun) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $state = Get-PubPromoteState $Ctx
    $repo = $Ctx.Config.publicRepo

    # verify: always, every run. The chain, the files on disk, the build again from its files, the draft by id.
    $build = Read-PubEvidence $Ctx 'build' $planRead
    $rooms = Read-PubEvidence $Ctx 'rooms' $planRead
    $draft = Read-PubEvidence $Ctx 'draft' $planRead
    if ($draft.inputs.build -cne $build.sha256 -or $draft.inputs.rooms -cne $rooms.sha256) { throw 'The draft was made from other build or rooms evidence than the current one.' }
    $facts = Assert-PubBuildFiles $Ctx $planRead (Join-Path (Get-PubStageDir $Ctx 'build') 'files') $rules
    if ($facts.zipSha256 -cne $build.facts.zipSha256) { throw 'The zip is not the one the build evidence names.' }
    if ($rooms.facts.buildId -cne $plan.roomBuild.id) { throw 'The rooms evidence is for another room build.' }
    if ((Resolve-PubCommit $Ctx $Ctx.Repo $plan.source.commit) -cne $plan.source.commit -or (Get-PubTree $Ctx $Ctx.Repo $plan.source.commit) -cne $plan.source.tree) { throw 'The plan''s source commit is not here as planned.' }
    $assets = Get-PubReleaseAssets $Ctx $plan
    $id = [string]$draft.facts.releaseId
    $release = Get-PubReleaseById $Ctx $id -AllowMissing
    if (!$release) { throw "Release $id (the draft this pipeline made) is gone from $repo; plan again." }
    $published = Test-PubPhaseDone $state 'publish'
    Assert-PubDraftRelease $Ctx $plan $release $assets -Published:$published
    # Releases are published by id: the draft must be the only release with this tag, and not older than any.
    $named = @(Get-PubReleases $Ctx | Where-Object { $_.tag_name -ceq $plan.tag })
    if ($named.Count -ne 1 -or "$($named[0].id)" -cne $id) { throw "$($named.Count) releases are named $($plan.tag) ($(@($named.id) -join ', ')); only draft $id may be." }
    $newer = @(Get-PubReleases $Ctx | Where-Object { !$_.draft -and "$($_.id)" -cne $id -and $_.tag_name -cmatch '^v[0-9]' -and (Compare-PubTag $_.tag_name $plan.tag) -ge 0 })
    if ($newer.Count) { throw "A release as new or newer is published ($(@($newer.tag_name) -join ', ')); $($plan.tag) is not promoted." }
    $tagNow = Get-PubRemoteRef $Ctx $Ctx.Repo $Ctx.Config.publicRemote "refs/tags/$($plan.tag)"
    if ($tagNow -and $tagNow -cne $plan.source.commit) { throw "The public tag $($plan.tag) points at $tagNow, not $($plan.source.commit)." }
    if ($tagNow -and !$published) { throw "The public tag $($plan.tag) exists although this pipeline has not published; check the repository by hand." }
    Set-PubPhase $Ctx $state 'verify' 'done' "draft $id, zip $($facts.zipSha256)"
    Write-PubLog $Ctx "Verified: plan $($planRead.sha256.Substring(0, 12)), build, rooms and draft $id; zip $($facts.zipSha256)."

    # server: the gate, not skippable.
    if (!(Test-PubPhaseDone $state 'server')) {
        $server = Test-PubServer $Ctx $plan $rooms -RoomsConfirmed:$P.RoomsConfirmed
        Set-PubPhase $Ctx $state 'server' 'done' $server
    }

    # The pushes: the library the README pins, the private release branch, the public main.
    foreach ($phase in @('push-library', 'push-origin', 'push-public')) {
        if (Test-PubPhaseDone $state $phase) { continue }
        $outcome = switch ($phase) {
            'push-library' { if ($plan.library.needsPush) { Push-PubBranch $Ctx $phase $Ctx.LibraryRepo $Ctx.Config.libraryRemote $plan.library.commit $plan.library.branch } else { 'not needed' } }
            'push-origin' { Push-PubBranch $Ctx $phase $Ctx.Repo $Ctx.Config.originRemote $plan.source.commit $plan.releaseBranch }
            'push-public' { Push-PubBranch $Ctx $phase $Ctx.Repo $Ctx.Config.publicRemote $plan.source.commit 'main' }
        }
        if ($outcome -cin @('pushed', 'there', 'contained', 'not needed')) { Set-PubPhase $Ctx $state $phase 'done' $outcome }
        elseif ($outcome -ceq 'skipped') { Set-PubPhase $Ctx $state $phase 'skipped' }
    }

    # publish: by id, naming the commit; the commit must be on the public repository by then.
    if (!(Test-PubPhaseDone $state 'publish')) {
        if (!$Ctx.DryRun -and 'publish' -cnotin @($Ctx.Skip)) {
            $main = Get-PubRemoteRef $Ctx $Ctx.Repo $Ctx.Config.publicRemote 'refs/heads/main'
            if ($main -cne $plan.source.commit -and !($main -and (Test-PubObject $Ctx $Ctx.Repo $main) -and (Test-PubAncestor $Ctx $Ctx.Repo $plan.source.commit $main))) {
                throw "The public main is $main, which does not contain $($plan.source.commit): fast-forward it first (phase push-public)."
            }
        }
        $outcome = Invoke-PubOutward $Ctx 'publish' "publish release $id ($($plan.tag)) at $($plan.source.commit.Substring(0, 12))" {
            $request = Join-Path $dir 'publish-request.json'
            Write-PubText $request (ConvertTo-PubJsonText ([ordered]@{draft = $false; prerelease = $true; target_commitish = $plan.source.commit; tag_name = $plan.tag}))
            Invoke-PubGh $Ctx @('api', '-X', 'PATCH', "repos/$repo/releases/$id", '--input', $request) | Out-Null
            'published'
        }
        if ($outcome.done) { Set-PubPhase $Ctx $state 'publish' 'done' }
        elseif ($outcome.skipped) { Set-PubPhase $Ctx $state 'publish' 'skipped' }
    }

    # confirm: the published release by id, its tag on the commit, and the zip as GitHub serves it.
    if ((Test-PubPhaseDone $state 'publish') -and !$Ctx.DryRun) {
        $live = Get-PubReleaseById $Ctx $id
        Assert-PubDraftRelease $Ctx $plan $live $assets -Published
        $tag = Get-PubRemoteRef $Ctx $Ctx.Repo $Ctx.Config.publicRemote "refs/tags/$($plan.tag)"
        if ($tag -cne $plan.source.commit) { throw "The published tag $($plan.tag) points at '$tag', not $($plan.source.commit)." }
        $download = Join-Path $dir 'downloaded'
        if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download -Recurse -Force }
        New-Item -ItemType Directory -Path $download | Out-Null
        Invoke-PubGh $Ctx @('release', 'download', $plan.tag, '--repo', $repo, '--dir', $download, '--pattern', $plan.zip, '--pattern', "$($plan.zip).sha256") | Out-Null
        foreach ($asset in $assets) {
            if ((Get-PubSha256 (Join-Path $download $asset.name)) -cne $asset.sha256) { throw "The published $($asset.name) is not the checked file." }
        }
        Set-PubPhase $Ctx $state 'confirm' 'done' "downloaded $($plan.zip) $($facts.zipSha256)"
        $skipped = @($PubPromotePhases | Where-Object { $state.phases.$_ -and $state.phases.$_.status -ceq 'skipped' })
        $files = @(@('state.json') + @($assets | ForEach-Object { "downloaded\$($_.name)" }) | ForEach-Object { Get-PubFileRecord $dir $_ })
        $null = Write-PubEvidence $Ctx 'promote' (New-PubEvidence $Ctx 'promote' $planRead $files @{releaseId = $id; url = $live.html_url; tag = $plan.tag
            publishedAt = $live.published_at; zipSha256 = $facts.zipSha256; skipped = $skipped})
        Write-PubLog $Ctx "PUBLISHED $($plan.tag): $($live.html_url) (zip $($facts.zipSha256), downloaded again and compared)."
        if ($skipped.Count) { Write-PubLog $Ctx "Phases skipped on the way: $($skipped -join ', ')." }
        return
    }
    $open = @($PubPromotePhases | Where-Object { !(Test-PubPhaseDone $state $_) })
    if ($Ctx.DryRun) { Write-PubLog $Ctx "Dry run: every check passed; nothing pushed or published. Phases a real run would do: $($open -join ', ')." }
    else { Write-PubLog $Ctx "Not finished: $($open -join ', ') still open (skipped or waiting). Run -Stage promote again to resume." }
}

# ---------------------------------------------------------------------------------------------------------------
# Status: where every stage stands, read-only.

function Show-PubStatus([object]$Ctx) {
    $planPath = Get-PubEvidencePath $Ctx 'plan'
    if (!(Test-Path -LiteralPath $planPath)) { Write-Host "$($Ctx.Label): no plan yet ($($Ctx.Root))."; return }
    $planRead = Read-PubPlan $Ctx
    $plan = $planRead.plan
    Write-Host "$($plan.tag): source $($plan.source.commit) (tree $($plan.source.tree)), library $($plan.library.commit), room build $($plan.roomBuild.id)$(if ($plan.roomBuild.changed) { ' (new)' })"
    Write-Host "plan.json $($planRead.sha256)"
    foreach ($stage in @('build', 'rooms', 'draft', 'promote')) {
        if (!(Test-Path -LiteralPath (Get-PubEvidencePath $Ctx $stage))) {
            $extra = if ($stage -ceq 'promote' -and (Test-Path -LiteralPath (Join-Path (Get-PubStageDir $Ctx 'promote') 'state.json'))) {
                $state = Get-PubPromoteState $Ctx; " (phases: $(@($PubPromotePhases | ForEach-Object { "$_=$(if ($state.phases.$_) { $state.phases.$_.status } else { 'open' })" }) -join ', '))"
            } else { '' }
            Write-Host "$($stage.PadRight(8)) not done$extra"; continue
        }
        try { $evidence = Read-PubEvidence $Ctx $stage $planRead; Write-Host "$($stage.PadRight(8)) done, checked ($($evidence.sha256.Substring(0, 12)))" }
        catch { Write-Host "$($stage.PadRight(8)) BROKEN: $($_.Exception.Message)" }
    }
}

# ---------------------------------------------------------------------------------------------------------------
# Rooms by adoption: a room-host pair built and installed earlier (a kit folder) taken as this release's, when the
# plan's commits would build the same binaries.

# What the Linux build compiles, read from the commits themselves (the same derivation for any commit pair):
#   sfv-ember: data/pick-space.json (embedded), scripts/build-room-host-linux.sh, the catalog sources that script
#              names (CATALOG_SOURCES) and every header they include from sfv-ember's src/.
#   library:   server/roomhost/build-linux.sh and sources.txt, every source sources.txt lists for a data catalog
#              (all groups but host_windows, host_catalog, selection and catalog) and every header those and the
#              sfv-ember sources include from the library's src/ and server/roomhost/; and sf4-net's crate as
#              `cargo build --profile server` reads it (Cargo.toml, Cargo.lock, build.rs, rust-toolchain.toml,
#              resource.rc, src/) with its path dependencies (not dev-dependencies), each the same way.
# Includes resolve as g++ does with -I<sfv>/src -I<library>/src -I<library>/server/roomhost: "..." first beside the
# including file; a name found in neither tree (a system header) is not a source.
function Get-PubRoomInputs([object]$Ctx, [string]$SfvCommit, [string]$LibraryCommit) {
    $trees = @{}
    foreach ($side in @(@('sfv', $Ctx.Repo, $SfvCommit), @('library', $Ctx.LibraryRepo, $LibraryCommit))) {
        $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($line in ((ConvertTo-PubText (Invoke-PubGit $Ctx $side[1] @('ls-tree', '-r', '--name-only', '--full-tree', $side[2]))) -split "`n")) { if ($line.Trim()) { $null = $set.Add($line.Trim()) } }
        $trees[$side[0]] = @{repo = $side[1]; commit = $side[2]; files = $set}
    }
    $read = { param($Side, $Path) Get-PubShow $Ctx $trees[$Side].repo $trees[$Side].commit $Path }
    $normal = { param($Path) $parts = [Collections.Generic.List[string]]::new()
        foreach ($part in ($Path -split '/')) { if ($part -ceq '..') { if ($parts.Count) { $parts.RemoveAt($parts.Count - 1) } } elseif ($part -and $part -cne '.') { $parts.Add($part) } }
        $parts -join '/' }
    $found = @{sfv = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal); library = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)}
    foreach ($fixed in @('data/pick-space.json', 'scripts/build-room-host-linux.sh')) { $null = $found.sfv.Add($fixed) }
    foreach ($fixed in @('server/roomhost/build-linux.sh', 'server/roomhost/sources.txt')) { $null = $found.library.Add($fixed) }
    $queue = [Collections.Generic.Queue[object]]::new()
    $script = & $read 'sfv' 'scripts/build-room-host-linux.sh'
    foreach ($m in [regex]::Matches($script, '\$SFV/(src/[^\s"$]+\.cxx)')) { $queue.Enqueue(@('sfv', $m.Groups[1].Value)) }
    $group = ''
    foreach ($line in ((& $read 'library' 'server/roomhost/sources.txt') -split "`n")) {
        $line = $line.TrimEnd("`r")
        if ($line -cmatch '^# \[([a-z_]+)\]$') { $group = $Matches[1]; continue }
        if (!$line.Trim() -or $line.StartsWith('#')) { continue }
        if ($group -cnotin @('host_windows', 'host_catalog', 'selection', 'catalog')) { $queue.Enqueue(@('library', $line.Trim())) }
    }
    if ($queue.Count -lt 3) { throw 'Could not read the room host''s sources from the build scripts.' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    while ($queue.Count) {
        $side, $path = $queue.Dequeue()
        if (!$seen.Add("${side}:$path")) { continue }
        if (!$trees[$side].files.Contains($path)) { throw "The room host's source $side $path is not in $($trees[$side].commit)." }
        $null = $found[$side].Add($path)
        $dir = if ($path.Contains('/')) { $path.Substring(0, $path.LastIndexOf('/')) } else { '' }
        foreach ($m in [regex]::Matches((& $read $side $path), '(?m)^\s*#\s*include\s*([<"])([^>"]+)[>"]')) {
            $name = $m.Groups[2].Value
            $candidates = @()
            if ($m.Groups[1].Value -ceq '"') { $candidates += , @($side, (& $normal "$dir/$name")) }
            $candidates += @(, @('sfv', (& $normal "src/$name"))) + @(, @('library', (& $normal "src/$name"))) + @(, @('library', (& $normal "server/roomhost/$name")))
            foreach ($candidate in $candidates) {
                if ($trees[$candidate[0]].files.Contains($candidate[1])) { $queue.Enqueue($candidate); break }
            }
        }
    }
    # sf4-net and its path dependencies.
    $crates = [Collections.Generic.Queue[string]]::new(); $crates.Enqueue('rust/sf4-net')
    $crateSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    while ($crates.Count) {
        $crate = $crates.Dequeue()
        if (!$crateSeen.Add($crate)) { continue }
        foreach ($name in @('Cargo.toml', 'Cargo.lock', 'build.rs', 'rust-toolchain.toml', 'resource.rc')) { if ($trees.library.files.Contains("$crate/$name")) { $null = $found.library.Add("$crate/$name") } }
        foreach ($file in $trees.library.files) { if ($file.StartsWith("$crate/src/")) { $null = $found.library.Add($file) } }
        $section = ''
        foreach ($line in ((& $read 'library' "$crate/Cargo.toml") -split "`n")) {
            if ($line -cmatch '^\s*\[([^\]]+)\]') { $section = $Matches[1]; continue }
            if ($section -cmatch 'dev-dependencies' -or $section -cnotmatch 'dependencies') { continue }
            if ($line -cmatch 'path\s*=\s*"([^"]+)"') { $crates.Enqueue((& $normal "$crate/$($Matches[1])")) }
        }
    }
    @{sfv = @($found.sfv); library = @($found.library)}
}

# The kit's commits are the plan's or behind them on the same line, and nothing that goes into the two binaries
# changed between them. Returns the path lists (the union of what each commit pair compiles).
function Assert-PubKitEquivalent([object]$Ctx, [object]$Plan, [string]$KitSource, [string]$KitLibrary) {
    $kitSfv = Resolve-PubCommit $Ctx $Ctx.Repo $KitSource
    $kitLib = Resolve-PubCommit $Ctx $Ctx.LibraryRepo $KitLibrary
    if (!(Test-PubAncestor $Ctx $Ctx.Repo $kitSfv $Plan.source.commit)) { throw "The kit's sfv-ember commit $kitSfv is not an ancestor of the plan's $($Plan.source.commit)." }
    if (!(Test-PubAncestor $Ctx $Ctx.LibraryRepo $kitLib $Plan.library.commit)) { throw "The kit's library commit $kitLib is not an ancestor of the plan's $($Plan.library.commit)." }
    $atKit = Get-PubRoomInputs $Ctx $kitSfv $kitLib
    $atPlan = Get-PubRoomInputs $Ctx $Plan.source.commit $Plan.library.commit
    $paths = @{sfv = @(@($atKit.sfv) + @($atPlan.sfv) | Sort-Object -Unique -CaseSensitive); library = @(@($atKit.library) + @($atPlan.library) | Sort-Object -Unique -CaseSensitive)}
    foreach ($side in @(@('sfv', $Ctx.Repo, $kitSfv, $Plan.source.commit), @('library', $Ctx.LibraryRepo, $kitLib, $Plan.library.commit))) {
        if ($side[2] -ceq $side[3]) { continue }
        $changed = (ConvertTo-PubText (Invoke-PubGit $Ctx $side[1] (@('diff', '--name-only', $side[2], $side[3], '--') + @($paths[$side[0]])))).Trim()
        if ($changed) { throw "The kit is not what the plan's commits build: $($side[0]) changed $(($changed -split "`n") -join ', ') between $($side[2].Substring(0, 12)) and $($side[3].Substring(0, 12))." }
    }
    [pscustomobject]@{sfvCommit = $kitSfv; libraryCommit = $kitLib; paths = $paths}
}

# -Stage rooms -AdoptKit <folder> -KitSource <commit> -KitLibrary <commit>: nothing is built or staged. The kit's
# two binaries are checked as a built pair is (on the same Linux, by build-rooms-linux.sh --check), and the kit
# must be equivalent to the plan's commits (Assert-PubKitEquivalent).
function Invoke-PubRoomsAdopt([object]$Ctx, [object]$PlanRead, [object[]]$Rules, [hashtable]$P) {
    $plan = $PlanRead.plan
    if (!$P.KitSource -or !$P.KitLibrary) { throw '-AdoptKit needs -KitSource <sfv-ember commit> and -KitLibrary <library commit>: the commits the kit was built from.' }
    $folder = $P.AdoptKit
    $bin = if (Test-Path -LiteralPath (Join-Path $folder 'room-build\sfve-room-host')) { Join-Path $folder 'room-build' } else { $folder }
    foreach ($name in @('sfve-room-host', 'sf4-net')) { if (!(Test-Path -LiteralPath (Join-Path $bin $name) -PathType Leaf)) { throw "No $name in the kit at $bin." } }
    $kitHashes = @{'sfve-room-host' = (Get-PubSha256 (Join-Path $bin 'sfve-room-host')); 'sf4-net' = (Get-PubSha256 (Join-Path $bin 'sf4-net'))}
    if (Test-Path -LiteralPath (Join-Path $bin 'BUILD_ID')) {
        $named = ([IO.File]::ReadAllText((Join-Path $bin 'BUILD_ID'))).Trim()
        if ($named -cne $plan.roomBuild.id) { throw "The kit is for room build $named, not the plan's $($plan.roomBuild.id)." }
    }
    $sumsFile = Join-Path $folder 'SHA256SUMS'
    if ((Test-Path -LiteralPath $sumsFile) -and $bin -ne $folder) {
        foreach ($line in [IO.File]::ReadAllLines($sumsFile)) {
            if ($line -cmatch '^([0-9a-f]{64}) [ *]?room-build/(sfve-room-host|sf4-net)$' -and $Matches[1] -cne $kitHashes[$Matches[2]]) { throw "The kit's $($Matches[2]) is not the file its SHA256SUMS names." }
        }
    }
    if (![Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes((Join-Path $bin 'sfve-room-host'))).Contains($plan.roomBuild.id)) { throw "The kit's room host does not carry the plan's room build $($plan.roomBuild.id)." }
    $equivalent = Assert-PubKitEquivalent $Ctx $plan $P.KitSource $P.KitLibrary
    Write-PubLog $Ctx "Kit from sfv-ember $($equivalent.sfvCommit.Substring(0, 12)) and library $($equivalent.libraryCommit.Substring(0, 12)) is equivalent: $($equivalent.paths.sfv.Count) sfv-ember and $($equivalent.paths.library.Count) library paths unchanged."
    if ($Ctx.DryRun) { Write-PubLog $Ctx "Dry run: would check the kit's pair on the build PC's Linux and record it as adopted (room host $($kitHashes.'sfve-room-host'.Substring(0, 12)), helper $($kitHashes.'sf4-net'.Substring(0, 12)))."; return }
    $dir = Get-PubStageDir $Ctx 'rooms'
    $inputs = Join-Path $dir 'kit-in'
    if (Test-Path -LiteralPath $inputs) { Remove-Item -LiteralPath $inputs -Recurse -Force }
    New-Item -ItemType Directory -Path $inputs -Force | Out-Null
    foreach ($name in $kitHashes.Keys) { Copy-Item -LiteralPath (Join-Path $bin $name) -Destination (Join-Path $inputs $name) }
    foreach ($name in $kitHashes.Keys) { if ((Get-PubSha256 (Join-Path $inputs $name)) -cne $kitHashes[$name]) { throw "The kit's $name changed while it was copied." } }
    $out = Join-Path $dir 'out'
    Invoke-PubRoomsWsl $Ctx $plan $inputs $out $P -Check
    $facts = Assert-PubRoomFiles $Ctx $plan $out $Rules -Adopted -KitHashes $kitHashes
    $facts.backend = 'wsl-check'
    $facts.buildScript = Get-PubSha256 $Ctx.RoomScriptPath
    $facts.kit = @{folder = $folder; sfvCommit = $equivalent.sfvCommit; libraryCommit = $equivalent.libraryCommit; sfvPaths = $equivalent.paths.sfv; libraryPaths = $equivalent.paths.library}
    $files = @(@('kit-in\sfve-room-host', 'kit-in\sf4-net', 'out\SHA256SUMS', 'out\result.txt') | ForEach-Object { Get-PubFileRecord $dir $_ })
    $null = Write-PubEvidence $Ctx 'rooms' (New-PubEvidence $Ctx 'rooms' $PlanRead $files $facts)
    Publish-PubRoomKit $Ctx $plan (Read-PubEvidence $Ctx 'rooms' $PlanRead)
}