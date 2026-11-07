param([int]$lag = 0, [int]$delay = 2, [int]$games = 2, [string]$p1 = 'RYU:0:1', [string]$p2 = 'CNL:0:2', [string]$stage = 'S25', [int]$rounds = 1, [int]$seconds = 30, [int]$mashFrom = 300, [int]$seed0 = 101, [int]$seed1 = 202, [int]$gameSeconds = 240, [int]$presses = 0, [string]$shots = '',
	[Parameter(Mandatory)][string]$bridgeExe, [Parameter(Mandatory)][string]$roomsExe, [string]$roomHost = 'D:\sfv-ember-build\sfve-room-host.exe',
	[string]$work = 'D:\sfv-re\scratch\public', [int]$port = 18797, [int]$roomPorts = 52100, [switch]$shellJoin, [switch]$keep)
# Two copies play Ember's public room (EMBER_LIBRARY.md section 24) on a local
# stack: an Ember bridge on http://127.0.0.1:$port, an ember-rooms supervisor
# beside it, and SFV's room host (sfve-room-host) as the room's authority,
# configured for this build and the game "sfv". Each copy makes an Ember ID of
# its own in $work\ember<N> (the helper must be built with -Features selftest,
# scripts/build-helper.ps1, which reads SF4E_SELFTEST_EMBER_DIR; the player's
# own Ember folder is never touched) and trusts the bridge. Copy 0 creates a
# public room of -rounds rounds of -seconds s and enters it; copy 1 finds it in
# the list, takes a ticket and enters. Both take a seat, pick, Ready and play
# -games games with seeded random input, as tools/runroom.ps1 does in a
# private room; then the checkpoints are compared. Prints PASS or FAIL.
#
# The supervisor gives each room two UDP ports from -roomPorts on (100 of
# them). On this PC the supervisor's default range, 45800-45899, is held by
# the system (about 44500-48500 cannot be bound), so the default is 52100.
#
# -shellJoin: copy 1 joins through the shell with its virtual pad instead of
# the pipe (Start, Online play, Public rooms, the room's card), and -shots
# then also takes the Public rooms list and the room.
#
# Everything this starts stops at the end (-keep leaves the copies running);
# $work is left for its logs: bridge.log, rooms.log (the room host's log is the
# supervisor's stderr).
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$logs = 'G:\SteamLibrary\steamapps\common\StreetFighterV\StreetFighterV\Binaries\Win64\SFVEmber\logs'
$logOf = @("$logs\sfv_ember.log", "$logs\sfv_ember.1.log")
function Ctl([int]$i, [string]$line) { python -I tools\sfve.py ctl $i $line }
function Status([int]$i) { (Ctl $i status).Substring(3) | ConvertFrom-Json }
function WaitFor([string]$what, [int]$timeout, [scriptblock]$done) {
	$deadline = (Get-Date).AddSeconds($timeout)
	while ((Get-Date) -lt $deadline) { if (& $done) { return }; Start-Sleep 1 }
	throw "timed out waiting for $what"
}
function Count([int]$i, [string]$pattern) { @(Select-String -Path $logOf[$i] -Pattern $pattern).Count }
function Shot([string]$name) {
	if (!$shots) { return }
	foreach ($i in 0, 1) { python -I tools\sfve.py shot $i "$shots\$name-$i.png" | Out-Null }
}
function Answer([string]$reply) { if ($reply -notlike 'ok*') { throw $reply }; $reply.Substring(3) }
function Press([int]$i, [string]$button) { Ctl $i "pad 0 ${button}:120 -:250" | Out-Null; Start-Sleep -Milliseconds 600; (Status $i).shell }
# Presses $moves in turn until the shell's focus is $focus.
function Seek([int]$i, [string]$focus, [string[]]$moves, [int]$limit = 12) {
	for ($step = 0; $step -lt $limit; $step++) {
		if ((Status $i).shell.focus -eq $focus) { return }
		Press $i $moves[$step % $moves.Count] | Out-Null
	}
	$at = (Status $i).shell
	if ($at.focus -ne $focus) { throw "copy ${i}: could not reach $focus (at $($at.screen)/$($at.focus))" }
}

foreach ($exe in $bridgeExe, $roomsExe, $roomHost) { if (!(Test-Path $exe)) { throw "$exe is not built" } }
$space = Get-Content data\pick-space.json -Raw | ConvertFrom-Json
$build = $space.build
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work, "$work\bridge", "$work\ember0", "$work\ember1" | Out-Null
$helper = 'G:\SteamLibrary\steamapps\common\StreetFighterV\StreetFighterV\Binaries\Win64\sf4-net.exe'
$origin = "http://127.0.0.1:$port"
$roomsPort = $port + 10
$secret = -join ((1..48) | ForEach-Object { '{0:x}' -f (Get-Random -Maximum 16) })
$processes = @()
$failure = ''
try {
	# The supervisor, with this build's SFV room host. Its helper is the copies'.
	Set-Content "$work\rooms.secret" $secret -NoNewline
	@{
		bind = "127.0.0.1:$roomsPort"; secret_file = "$work\rooms.secret"; max_rooms = 4; port_range = @($roomPorts, ($roomPorts + 99))
		builds = @{ $build = @{ room_host = $roomHost; helper = $helper; game = 'sfv' } }
	} | ConvertTo-Json -Depth 5 | Set-Content "$work\rooms.json"
	$processes += Start-Process $roomsExe -ArgumentList "`"$work\rooms.json`"" -PassThru -WindowStyle Hidden -RedirectStandardError "$work\rooms.log" -RedirectStandardOutput "$work\rooms.out"
	WaitFor 'the supervisor' 15 { try { (Invoke-RestMethod "http://127.0.0.1:$roomsPort/health") -eq 'ok' } catch { $false } }
	# The bridge, with public rooms on that supervisor.
	& $bridgeExe init "$work\bridge" | Out-Null
	$config = Get-Content "$work\bridge\bridge.json" -Raw | ConvertFrom-Json
	$config.listen = "127.0.0.1:$port"
	$config.origin = $origin
	$config | Add-Member -Force integration_secrets "$work\bridge\integration-secrets.json"
	$config | Add-Member -Force rooms @{ supervisor_url = "http://127.0.0.1:$roomsPort" }
	$config | ConvertTo-Json -Depth 20 | Set-Content "$work\bridge\bridge.json"
	@{ format = 'ember-bridge-integrations'; version = 1; rooms_supervisor_secret = $secret } | ConvertTo-Json | Set-Content "$work\bridge\integration-secrets.json"
	$processes += Start-Process $bridgeExe -ArgumentList 'serve', "`"$work\bridge\bridge.json`"" -PassThru -WindowStyle Hidden -RedirectStandardError "$work\bridge.log" -RedirectStandardOutput "$work\bridge.out"
	WaitFor 'the bridge' 30 { try { (Invoke-RestMethod "$origin/.well-known/ember-bridge.json").bridge_id } catch { $null } }
	"bridge $origin; supervisor 127.0.0.1:$roomsPort; build $build"

	python -I tools\sfve.py kill | Out-Null
	$seeds = @($seed0, $seed1)
	foreach ($i in 0, 1) {
		$env:SF4E_SELFTEST_EMBER_DIR = "$work\ember$i"
		python -I tools\sfve.py launch $i --mode netplay -- '-sfve_pads=01' '-sfve_background=1' '-sfve_room=1' "-sfve_mash=$($seeds[$i])" "-sfve_mash_from=$mashFrom" "-sfve_lag=$lag" | Out-Null
	}
	Remove-Item Env:\SF4E_SELFTEST_EMBER_DIR
	WaitFor 'the runtime on both copies' 120 { (Status 0).ember.host -eq 'running' -and (Status 1).ember.host -eq 'running' }

	# An Ember ID of each copy's own, and the bridge trusted.
	WaitFor 'an Ember ID on both copies' 90 {
		foreach ($i in 0, 1) { if ((Status $i).public.identity -ne 'ready') { Ctl $i 'ember-id' | Out-Null } }
		(Status 0).public.identity -eq 'ready' -and (Status 1).public.identity -eq 'ready'
	}
	$ids = foreach ($i in 0, 1) { (Status $i).public.emberId }
	"Ember IDs: $($ids -join ', ')"
	if ($ids[0] -eq $ids[1]) { throw 'both copies have the same Ember ID: the helper is not reading SF4E_SELFTEST_EMBER_DIR' }
	foreach ($i in 0, 1) {
		Answer (Ctl $i "bridge $origin") | Out-Null
		WaitFor "copy $i's lookup" 30 { (Status $i).public.inspected -like 'brg_*' }
		$ticket = Answer (Ctl $i 'trust')
		WaitFor "copy $i's trust" 30 { $p = (Status $i).public; "$($p.identityTicket)" -eq $ticket -and $p.identityOk }
	}

	# Copy 0 opens a public room and enters it.
	$create = Answer (Ctl 0 "public create $rounds $seconds SFV public test")
	WaitFor "the room's creation" 60 { $p = (Status 0).public; "$($p.answered)" -eq $create -and ($p.admitted -or $p.failure) }
	$p = (Status 0).public
	if ($p.failure) { throw "the create was refused: $($p.failure)" }
	$room = $p.admitted
	"created room $room"
	Answer (Ctl 0 'public enter') | Out-Null
	WaitFor 'copy 0 in the room' 60 { $s = Status 0; $s.room.room -eq 'joined' -and $s.room.members -eq 1 }
	if ($shellJoin) {
		# Copy 1 joins from the shell's own Public rooms list with its pad, as a
		# player does: Start, Online play, Public rooms, the room's card.
		Press 1 'START' | Out-Null
		if ((Status 1).shell.screen -eq 'welcome') { Seek 1 'welcome-skip' @('DOWN', 'DOWN', 'RIGHT'); Press 1 'A' | Out-Null }
		Seek 1 'online' @('UP')
		Press 1 'A' | Out-Null
		Seek 1 'public-rooms' @('DOWN')
		Press 1 'A' | Out-Null
		WaitFor 'the room on copy 1''s Public rooms screen' 60 { @((Status 1).public.rooms | Where-Object { $_.id -eq $room }).Count -eq 1 }
		Start-Sleep 2
		"copy 1's shell: " + ((Status 1).shell | ConvertTo-Json -Compress)
		Shot 'public-list'
		Seek 1 "room:$room" @('DOWN') 16
		Shot 'public-list-focus'
		Press 1 'A' | Out-Null
		WaitFor 'both copies in the room' 90 { $s0 = Status 0; $s1 = Status 1; $s0.room.room -eq 'joined' -and $s1.room.room -eq 'joined' -and $s0.room.members -eq 2 -and $s1.room.members -eq 2 }
		Start-Sleep 2
		Shot 'public-room-shell'
		Press 1 'B' | Out-Null
	}
	# Copy 1 finds it in the list and takes a ticket.
	if (!$shellJoin) {
	WaitFor 'the room in copy 1''s list' 60 {
		$p = (Status 1).public
		if (!$p.loading) { Ctl 1 'public list' | Out-Null }
		@($p.rooms | Where-Object { $_.id -eq $room }).Count -eq 1
	}
	"copy 1 lists: " + (((Status 1).public.rooms | ForEach-Object { "$($_.name) ($($_.members))" }) -join ', ')
	$join = Answer (Ctl 1 "public join $room")
	WaitFor "copy 1's ticket" 60 { $p = (Status 1).public; "$($p.answered)" -eq $join -and ($p.admitted -or $p.failure) }
	if ((Status 1).public.failure) { throw "the ticket was refused: $((Status 1).public.failure)" }
	Answer (Ctl 1 'public enter') | Out-Null
	WaitFor 'both copies in the room' 90 { $s0 = Status 0; $s1 = Status 1; $s0.room.room -eq 'joined' -and $s1.room.room -eq 'joined' -and $s0.room.members -eq 2 -and $s1.room.members -eq 2 }
	}

	foreach ($i in 0, 1) { Ctl $i "sit 0 $i" | Out-Null }
	WaitFor 'both copies seated' 60 { (Status 0).room.slot -eq 0 -and (Status 1).room.slot -eq 1 }
	foreach ($i in 0, 1) { Ctl $i "delay $delay" | Out-Null }
	Ctl 0 "pick $p1 $stage" | Out-Null
	Ctl 1 "pick $p2" | Out-Null
	for ($n = 0; $n -lt $presses; $n++) {
		foreach ($i in 0, 1) { Ctl $i 'pad 0 DOWN:120 -:250' | Out-Null }
		Start-Sleep -Milliseconds 600
	}
	for ($game = 1; $game -le $games; $game++) {
		$starts = @((Count 0 'netplay: room match as P'), (Count 1 'netplay: room match as P'))
		$results = @((Count 0 'netplay: match result'), (Count 1 'netplay: match result'))
		$closes = @((Count 0 'battle closed at its match'), (Count 1 'battle closed at its match'))
		WaitFor "game $game's sessions" 180 {
			foreach ($i in 0, 1) { if ((Status $i).room.canReady) { Ctl $i ready | Out-Null } }
			(Count 0 'netplay: room match as P') -gt $starts[0] -and (Count 1 'netplay: room match as P') -gt $starts[1]
		}
		Start-Sleep 20
		Shot "game$game-fight"
		WaitFor "game $game's result on both copies" $gameSeconds { (Count 0 'netplay: match result') -gt $results[0] -and (Count 1 'netplay: match result') -gt $results[1] }
		WaitFor "both copies back in the room after game $game" 90 { (Count 0 'battle closed at its match') -gt $closes[0] -and (Count 1 'battle closed at its match') -gt $closes[1] -and (Status 0).room.room -eq 'joined' -and (Status 1).room.room -eq 'joined' }
		Start-Sleep 3
		Shot "game$game-room"
		foreach ($i in 0, 1) {
			$mode = (Status $i).battle.mode
			"copy ${i}: " + (Select-String -Path $logOf[$i] -Pattern 'netplay: match result' | Select-Object -Last 1).Line.Substring(24) + " rollbacks $($mode.rollbacks), longest $($mode.longestRollback)"
		}
	}
	"room host: " + ((Invoke-RestMethod "http://127.0.0.1:$roomsPort/rooms" -Headers @{ Authorization = "Bearer $secret" }) | ConvertTo-Json -Compress -Depth 6)
} catch {
	$failure = $_.Exception.Message
}
foreach ($i in 0, 1) {
	try { "copy $i status: " + ((Status $i).room | ConvertTo-Json -Compress) } catch { "copy $i is not answering" }
	if (Test-Path $logOf[$i]) { Select-String -Path $logOf[$i] -Pattern 'desync|mismatch' | ForEach-Object { "copy ${i}: " + $_.Line.Substring(24) } }
}
if (!$keep) { python -I tools\sfve.py kill | Out-Null }
foreach ($process in $processes) { if (!$process.HasExited) { Stop-Process -Id $process.Id -Force } }
# Room hosts the supervisor started outlive it when it is killed: end ours.
Get-Process sfve-room-host -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $roomHost } | Stop-Process -Force
function Checkpoints([int]$i) {
	$hashes = @{}; $game = 0; $previous = -1
	if (!(Test-Path $logOf[$i])) { return $hashes }
	foreach ($m in Select-String -Path $logOf[$i] -Pattern 'checkpoint: frame (\d+) hash (\w+)') {
		$frame = [int]$m.Matches[0].Groups[1].Value
		if ($frame -lt $previous - 600) { $game++ }
		$previous = $frame; $hashes["$game/$frame"] = $m.Matches[0].Groups[2].Value
	}
	$hashes
}
$h0 = Checkpoints 0; $h1 = Checkpoints 1
$common = @($h0.Keys | Where-Object { $h1.ContainsKey($_) })
$differing = @($common | Where-Object { $h0[$_] -ne $h1[$_] })
"checkpoints: $($common.Count) compared, $($differing.Count) differing"
$desyncs = $differing.Count
foreach ($i in 0, 1) { if (Test-Path $logOf[$i]) { $desyncs += Count $i 'desync|mismatch' } }
if ($failure) { "FAIL: $failure" } elseif ($desyncs) { "FAIL: $desyncs desync line(s)" } elseif ($common.Count -eq 0) { 'FAIL: no checkpoints compared' } else { "PASS: $games game(s) in a public room, no desync" }
