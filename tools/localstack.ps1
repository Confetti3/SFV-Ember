# The local stack tools/runpublic.ps1 and tools/runmatch.ps1 share, dot-sourced
# by both: the control pipe's helpers, a scratch Ember bridge on 127.0.0.1, two
# copies with Ember IDs of their own that trust it, and the checks and cleanup
# at the end (the copies' checkpoints compared, PASS or FAIL; the runner exits
# nonzero unless Complete-Run returned $true, see tools/verdict.ps1).
#
# The copies make their Ember IDs in <work>\ember<N>: the helper must be built
# with -Features selftest (scripts/build-helper.ps1), which reads
# SF4E_SELFTEST_EMBER_DIR, so the player's own Ember folder is never touched.
# They run in the background (-sfve_background=1) with the room's commands
# (-sfve_room=1) and seeded random input, and no window is brought forward.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'verdict.ps1')
# The copies this run starts are listed under a run of its own (SFVE_RUN, tools/sfvelib.py), so `sfve.py kill` ends only these.
$env:SFVE_RUN = [guid]::NewGuid().ToString('N')
# The mod logs under %APPDATA%\SFVEmber\logs (mod/Log.cxx), as tools/runroom.ps1 reads them.
$logs = Join-Path $env:APPDATA 'SFVEmber\logs'
$logOf = @("$logs\sfv_ember.log", "$logs\sfv_ember.1.log")

function Ctl([int]$i, [string]$line) { python -I tools\sfve.py ctl $i $line }
function Status([int]$i) { (Ctl $i status).Substring(3) | ConvertFrom-Json }
function WaitFor([string]$what, [int]$timeout, [scriptblock]$done) {
	$deadline = (Get-Date).AddSeconds($timeout)
	while ((Get-Date) -lt $deadline) { if (& $done) { return }; Start-Sleep 1 }
	throw "timed out waiting for $what"
}
function Count([int]$i, [string]$pattern) { @(Select-String -Path $logOf[$i] -Pattern $pattern).Count }
# Both copies' windows to $dir\$name-<copy>.png, when $dir is given.
function Shot([string]$dir, [string]$name) {
	if (!$dir) { return }
	foreach ($i in 0, 1) { python -I tools\sfve.py shot $i "$dir\$name-$i.png" | Out-Null }
}
function Answer([string]$reply) { if ($reply -notlike 'ok*') { throw $reply }; $reply.Substring(3) }
# The last line of copy $i's log matching $pattern, without its time stamp.
function LastLine([int]$i, [string]$pattern) { (Select-String -Path $logOf[$i] -Pattern $pattern | Select-Object -Last 1).Line.Substring(24) }

# A fresh bridge in $dir (`ember-bridge init`, its mock-local connection) that
# listens on 127.0.0.1:$port and names itself by that origin, with $settings'
# fields added to its bridge.json. Returns the configuration's path.
function Initialize-TestBridge([string]$bridgeExe, [string]$dir, [int]$port, [hashtable]$settings = @{}) {
	& $bridgeExe init $dir | Out-Null
	if ($LASTEXITCODE) { throw "ember-bridge init failed ($LASTEXITCODE)" }
	$path = Join-Path $dir 'bridge.json'
	$config = Get-Content $path -Raw | ConvertFrom-Json
	$config.listen = "127.0.0.1:$port"
	$config.origin = "http://127.0.0.1:$port"
	foreach ($key in $settings.Keys) { $config | Add-Member -Force $key $settings[$key] }
	$config | ConvertTo-Json -Depth 20 | Set-Content $path
	$path
}

# Serves the bridge $config names, its output in $work\bridge.log and
# bridge.out, until its profile answers. Returns the process; one that never
# answers is stopped here, before the error goes on.
function Start-TestBridge([string]$bridgeExe, [string]$config, [string]$work) {
	$origin = (Get-Content $config -Raw | ConvertFrom-Json).origin
	$process = Start-Process $bridgeExe -ArgumentList 'serve', "`"$config`"" -PassThru -WindowStyle Hidden -RedirectStandardError "$work\bridge.log" -RedirectStandardOutput "$work\bridge.out"
	try {
		WaitFor 'the bridge' 30 { try { (Invoke-RestMethod "$origin/.well-known/ember-bridge.json").bridge_id } catch { $null } }
	} catch {
		if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
		throw
	}
	$process
}

# Starts copies 0 and 1 (each with its own Ember folder in $work\ember<N> and
# its own seed for the mash from frame $mashFrom, $lag ms apart) and waits for
# both runtimes.
function Start-Copies([string]$work, [int[]]$seeds, [int]$mashFrom, [int]$lag) {
	python -I tools\sfve.py kill | Out-Null
	foreach ($i in 0, 1) {
		New-Item -ItemType Directory -Force "$work\ember$i" | Out-Null
		$env:SF4E_SELFTEST_EMBER_DIR = "$work\ember$i"
		python -I tools\sfve.py launch $i --mode netplay -- '-sfve_pads=01' '-sfve_background=1' '-sfve_room=1' "-sfve_mash=$($seeds[$i])" "-sfve_mash_from=$mashFrom" "-sfve_lag=$lag" | Out-Null
	}
	Remove-Item Env:\SF4E_SELFTEST_EMBER_DIR
	WaitFor 'the runtime on both copies' 120 { (Status 0).ember.host -eq 'running' -and (Status 1).ember.host -eq 'running' }
}

# Each copy makes an Ember ID of its own and trusts the bridge at $origin, as
# the Ember ID and Linked accounts screens do. Returns both Ember IDs.
function Connect-Copies([string]$origin) {
	WaitFor 'an Ember ID on both copies' 90 {
		foreach ($i in 0, 1) { if ((Status $i).public.identity -ne 'ready') { Ctl $i 'ember-id' | Out-Null } }
		(Status 0).public.identity -eq 'ready' -and (Status 1).public.identity -eq 'ready'
	}
	$ids = foreach ($i in 0, 1) { (Status $i).public.emberId }
	if ($ids[0] -eq $ids[1]) { throw 'both copies have the same Ember ID: the helper is not reading SF4E_SELFTEST_EMBER_DIR' }
	foreach ($i in 0, 1) {
		Answer (Ctl $i "bridge $origin") | Out-Null
		WaitFor "copy $i's lookup" 30 { (Status $i).public.inspected -like 'brg_*' }
		$ticket = Answer (Ctl $i 'trust')
		WaitFor "copy $i's trust" 30 { $p = (Status $i).public; "$($p.identityTicket)" -eq $ticket -and $p.identityOk }
	}
	$ids
}

# Waits for copy $i's answer to the identity request "ok <ticket>" gave, and
# fails with the helper's reason when it was refused.
function WaitIdentity([int]$i, [string]$ticket, [string]$what) {
	WaitFor "copy $i's $what" 60 { "$((Status $i).public.identityTicket)" -eq $ticket }
	$p = (Status $i).public
	if (!$p.identityOk) { throw "copy ${i}'s $what was refused: $($p.identityFailure)" }
}

# The end of a run: each copy's room and any desync line, the copies closed
# (unless $keep), the processes stopped, the checkpoints of every game both
# logged compared, and PASS ($passText) or FAIL. Returns $true only for PASS
# (tools/verdict.ps1's rule): the caller finishes its own cleanup, then exits
# nonzero for anything else.
function Complete-Run([string]$failure, [System.Diagnostics.Process[]]$processes, [bool]$keep, [string]$passText) {
	foreach ($i in 0, 1) {
		try { Write-Host ("copy $i status: " + ((Status $i).room | ConvertTo-Json -Compress)) } catch { Write-Host "copy $i is not answering" }
		if (Test-Path $logOf[$i]) { Select-String -Path $logOf[$i] -Pattern 'desync|mismatch' | ForEach-Object { Write-Host ("copy ${i}: " + $_.Line.Substring(24)) } }
	}
	if (!$keep) { python -I tools\sfve.py kill | Out-Null }
	foreach ($process in $processes) { if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force } }
	$checkpoints = Compare-Checkpoints (Get-Checkpoints $logOf[0]) (Get-Checkpoints $logOf[1])
	Write-Host "checkpoints: $($checkpoints.Compared) compared, $($checkpoints.Differing) differing"
	$desyncs = $checkpoints.Differing
	foreach ($i in 0, 1) { if (Test-Path $logOf[$i]) { $desyncs += Count $i 'desync|mismatch' } }
	Write-Verdict $failure $desyncs $checkpoints.Compared $passText
}
