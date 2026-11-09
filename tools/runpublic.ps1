param([int]$lag = 0, [int]$delay = 2, [int]$games = 2, [string]$p1 = 'RYU:0:1', [string]$p2 = 'CNL:0:2', [string]$stage = 'S25', [int]$rounds = 1, [int]$seconds = 30, [int]$mashFrom = 300, [int]$seed0 = 101, [int]$seed1 = 202, [int]$gameSeconds = 240, [int]$presses = 0, [string]$shots = '',
	[Parameter(Mandatory)][string]$bridgeExe, [Parameter(Mandatory)][string]$roomsExe, [string]$roomHost = $(if ($env:SFVE_BUILD_DIR) { Join-Path $env:SFVE_BUILD_DIR 'sfve-room-host.exe' } else { 'D:\sfv-ember-build\sfve-room-host.exe' }),
	[string]$work = 'D:\sfv-re\scratch\public', [int]$port = 18797, [int]$roomPorts = 52100, [switch]$shellJoin, [switch]$ranked, [switch]$keep)
# Two copies play Ember's public room (EMBER_LIBRARY.md section 24) on a local
# stack (tools/localstack.ps1): an Ember bridge on http://127.0.0.1:$port, an ember-rooms supervisor
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
# -ranked (EMBER_LIBRARY.md section 28): the bridge shows classes, and before
# the room opens both Ember IDs get a Discord account straight in the bridge's
# database (Discord's sign-in is not run), copy 0 with a settled SFV rating
# (Grand Master, band 8) and a USF4 one (B+, band 6), copy 1 with none (still
# placing). Then: both copies' rooms show copy 0 with SFV's Grand Master and
# copy 1 placing; the games are rated in SFV's rating alone (USF4's row
# untouched, none made for copy 1); copy 0 takes part on SFV's board as
# "SFV 1"; its standing and copy 1's board say so in SFV's league names; the
# public board lists it for game=sfv and not for USF4; each copy's online
# record counts its games; and copy 1 opens Online play, Rankings with its pad
# (shots ranked-*.png with -shots).
#
# Everything this starts stops at the end (-keep leaves the copies running);
# $work is left for its logs: bridge.log, rooms.log (the room host's log is the
# supervisor's stderr).
. (Join-Path $PSScriptRoot 'localstack.ps1')
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
New-Item -ItemType Directory -Force $work, "$work\bridge" | Out-Null
$helper = Join-Path $gameDir 'sf4-net.exe'
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
	$config = Initialize-TestBridge $bridgeExe "$work\bridge" $port @{
		integration_secrets = "$work\bridge\integration-secrets.json"
		rooms = @{ supervisor_url = "http://127.0.0.1:$roomsPort"; skill_badges = [bool]$ranked }
	}
	@{ format = 'ember-bridge-integrations'; version = 1; rooms_supervisor_secret = $secret } | ConvertTo-Json | Set-Content "$work\bridge\integration-secrets.json"
	$processes += Start-TestBridge $bridgeExe $config $work
	"bridge $origin; supervisor 127.0.0.1:$roomsPort; build $build"

	Start-Copies $work @($seed0, $seed1) $mashFrom $lag
	# An Ember ID of each copy's own, and the bridge trusted.
	$ids = Connect-Copies $origin
	"Ember IDs: $($ids -join ', ')"

	$db = "$work\bridge\bridge.sqlite3"
	function Rows([string]$sql) { $text = python -I tools\bridgedb.py rows $db $sql; if ($LASTEXITCODE) { throw "bridgedb: $text" }; @($text | ConvertFrom-Json) }
	if ($ranked) {
		# Each copy reads its SFV standing first: no Discord account yet (and the
		# bridge then knows both Ember IDs).
		foreach ($i in 0, 1) {
			WaitFor "copy $i's first standing" 60 {
				$s = (Status $i).ranking.standing
				if (!$s.loaded -and !$s.loading) { Ctl $i 'ranking standing' | Out-Null }
				$s.loaded
			}
			$s = (Status $i).ranking.standing
			if ($s.kind -ne 'no_account') { throw "copy ${i}'s first standing is $($s.kind) ($($s.error))" }
		}
		# Discord accounts for both Ember IDs, copy 0 settled in both games.
		"seeded: " + (python -I tools\bridgedb.py seed $db $ids[0] $ids[1])
		if ($LASTEXITCODE) { throw 'the seed failed' }
		$usf4Before = Rows "SELECT rating, deviation, played_at FROM skill_ratings WHERE game = 'usf4'"
		$recordBefore = foreach ($i in 0, 1) { $r = (Status $i).ranking.record; $r.wins + $r.losses }
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
		Shot $shots 'public-list'
		Seek 1 "pr-room:$room" @('DOWN') 16
		Shot $shots 'public-list-focus'
		Press 1 'A' | Out-Null
		WaitFor 'both copies in the room' 90 { $s0 = Status 0; $s1 = Status 1; $s0.room.room -eq 'joined' -and $s1.room.room -eq 'joined' -and $s0.room.members -eq 2 -and $s1.room.members -eq 2 }
		Start-Sleep 2
		Shot $shots 'public-room-shell'
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
	if ($ranked) {
		# Each ticket carried its player's SFV class: on both copies the room
		# shows one member as SFV's Grand Master, settled, and one placing.
		WaitFor 'the classes in both rooms' 30 {
			$seen = foreach ($i in 0, 1) {
				$badges = @((Status $i).ranking.members | ForEach-Object { $_.badge })
				# Its name is the game's own for that league (Grand Master in
				# English), or the English one until the game gave it.
				@($badges | Where-Object { $_.kind -eq 'class' -and $_.band -eq 8 -and $_.settled -and $_.name }).Count -eq 1 -and
					@($badges | Where-Object { $_.kind -eq 'placing' }).Count -eq 1
			}
			-not ($seen -contains $false)
		}
		foreach ($i in 0, 1) { "copy $i's room: " + ((Status $i).ranking.members | ConvertTo-Json -Compress -Depth 4) }
		if ($shots) {
			# The room as copy 1's shell shows it, the marks beside the names.
			if (!(Status 1).shell.shown) { Press 1 'START' | Out-Null }
			Start-Sleep 3
			Shot $shots 'ranked-room'
			Press 1 'B' | Out-Null
		}
	}
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
		Shot $shots "game$game-fight"
		WaitFor "game $game's result on both copies" $gameSeconds { (Count 0 'netplay: match result') -gt $results[0] -and (Count 1 'netplay: match result') -gt $results[1] }
		WaitFor "both copies back in the room after game $game" 90 { (Count 0 'battle closed at its match') -gt $closes[0] -and (Count 1 'battle closed at its match') -gt $closes[1] -and (Status 0).room.room -eq 'joined' -and (Status 1).room.room -eq 'joined' }
		Start-Sleep 3
		Shot $shots "game$game-room"
		foreach ($i in 0, 1) {
			$mode = (Status $i).battle.mode
			"copy ${i}: " + (Select-String -Path $logOf[$i] -Pattern 'netplay: match result' | Select-Object -Last 1).Line.Substring(24) + " rollbacks $($mode.rollbacks), longest $($mode.longestRollback)"
		}
	}
	$hosted = Invoke-RestMethod "http://127.0.0.1:$roomsPort/rooms" -Headers @{ Authorization = "Bearer $secret" }
	"room host: " + ($hosted | ConvertTo-Json -Compress -Depth 6)
	# The listing's faces (section 28): SFV's host lists each member's shared
	# pick, never the profile main both copies have (fighter 0): two fighters,
	# both picks, so not the same one twice.
	$faces = @(@($hosted)[0].details.fighters)
	if ($faces.Count -ne 2 -or $faces -contains 255 -or $faces[0] -eq $faces[1]) { throw "the listing's faces are not the two shared picks: $($faces -join ', ')" }
	"listing faces: $($faces -join ', ') (the shared picks)"
	if ($ranked) {
		# Rated once each, in SFV's rating alone.
		WaitFor 'the games rated by the bridge' 60 { (Rows 'SELECT COUNT(*) AS n FROM rated_games')[0].n -ge $games }
		$rated = Rows 'SELECT COUNT(*) AS n FROM rated_games'
		if ($rated[0].n -ne $games) { throw "the bridge rated $($rated[0].n) game(s) of $games" }
		$sfv = Rows "SELECT discord_user_id AS account, rating, deviation, band FROM skill_ratings WHERE game = 'sfv' ORDER BY discord_user_id"
		"SFV ratings: " + ($sfv | ConvertTo-Json -Compress)
		if ($sfv.Count -ne 2) { throw "SFV's rating holds $($sfv.Count) account(s), not 2" }
		$usf4 = Rows "SELECT rating, deviation, played_at FROM skill_ratings WHERE game = 'usf4'"
		if (($usf4 | ConvertTo-Json -Compress) -ne ($usf4Before | ConvertTo-Json -Compress)) { throw "USF4's rating changed: $($usf4 | ConvertTo-Json -Compress)" }
		"USF4's rating untouched: " + ($usf4 | ConvertTo-Json -Compress)
		"sittings: " + ((Rows 'SELECT game, games FROM rated_pairs') | ConvertTo-Json -Compress)
		# Each copy's online record counted its games.
		foreach ($i in 0, 1) {
			$r = (Status $i).ranking.record
			"copy $i's record: $($r.wins) W / $($r.losses) L"
			if ($r.wins + $r.losses - $recordBefore[$i] -ne $games) { throw "copy ${i}'s record counted $($r.wins + $r.losses - $recordBefore[$i]) game(s) of $games" }
		}
		# Copy 0 takes part on SFV's board as "SFV 1"; its standing is an SFV league.
		$choice = Answer (Ctl 0 'ranking take-part GB SFV 1')
		WaitFor "copy 0's choice" 30 { "$((Status 0).ranking.chose)" -eq $choice }
		$standing = (Status 0).ranking
		if ($standing.choiceFailure) { throw "copy 0's choice was refused: $($standing.choiceFailure)" }
		"copy 0's standing: " + ($standing.standing | ConvertTo-Json -Compress -Depth 4)
		if ($standing.standing.kind -ne 'ranked' -or $standing.standing.badge.kind -ne 'class' -or !$standing.standing.badge.name -or $standing.standing.name -ne 'SFV 1') {
			throw "copy 0's standing is not a listed SFV league"
		}
		# Copy 1 reads SFV's board: copy 0 on it, under SFV's league name.
		WaitFor "copy 1's board" 30 {
			$b = (Status 1).ranking.board
			if (!$b.loading) { Ctl 1 'ranking board' | Out-Null }
			@($b.players | Where-Object { $_.name -eq 'SFV 1' }).Count -eq 1
		}
		$board = (Status 1).ranking.board.players
		"copy 1's board: " + ($board | ConvertTo-Json -Compress)
		if (@($board | Where-Object { $_.name -eq 'SFV 1' -and $_.class -eq $standing.standing.badge.name -and !$_.own }).Count -ne 1) { throw "copy 1's board does not show SFV 1 at its league" }
		# The website's boards: SFV's lists copy 0, USF4's does not.
		$publicSfv = Invoke-RestMethod "$origin/v1/ranking/board/public?game=sfv"
		$publicUsf4 = Invoke-RestMethod "$origin/v1/ranking/board/public"
		"public boards: sfv " + ($publicSfv | ConvertTo-Json -Compress -Depth 5) + "; usf4 " + ($publicUsf4 | ConvertTo-Json -Compress -Depth 5)
		if (@($publicSfv.players | Where-Object { $_.listing.name -eq 'SFV 1' }).Count -ne 1 -or @($publicUsf4.players).Count -ne 0) { throw 'the public boards do not keep the games apart' }
		# Each copy opens Online play, Rankings with its pad: copy 0 its own
		# standing (SFV's league), copy 1 the board's page. Online play opens
		# the room while a copy is in one, so both leave it first.
		if ($shots) {
			# Copy 1's standing was read before the seed; read it again.
			Ctl 1 'ranking standing' | Out-Null
			foreach ($i in 0, 1) { Ctl $i leave | Out-Null }
			WaitFor 'both copies out of the room' 30 { (Status 0).room.room -eq 'idle' -and (Status 1).room.room -eq 'idle' }
			foreach ($i in 0, 1) {
				if (!(Status $i).shell.shown) { Press $i 'START' | Out-Null }
				foreach ($n in 1..4) { if ((Status $i).shell.screen -eq 'home') { break }; Press $i 'B' | Out-Null }
				Seek $i 'online' @('UP')
				Press $i 'A' | Out-Null
				# The first visit to Online play asks a welcome screen first.
				if ((Status $i).shell.screen -eq 'welcome') {
					Seek $i 'welcome-skip' @('DOWN', 'DOWN', 'RIGHT')
					Press $i 'A' | Out-Null
					Seek $i 'online' @('UP')
					Press $i 'A' | Out-Null
				}
				Seek $i 'rankings' @('DOWN') 16
				Press $i 'A' | Out-Null
				WaitFor "copy ${i}'s Rankings" 30 { $s = Status $i; $s.shell.screen -eq 'rankings' -and $s.ranking.standing.kind -eq 'ranked' }
				Start-Sleep 3
				"copy ${i}'s shell: " + ((Status $i).shell | ConvertTo-Json -Compress)
			}
			Shot $shots 'ranked-rankings'
			# Both turn to the board's page (RB).
			foreach ($i in 0, 1) { Press $i 'RB' | Out-Null }
			WaitFor 'the boards' 30 { (Status 0).ranking.board.loaded -and (Status 1).ranking.board.loaded }
			Start-Sleep 3
			Shot $shots 'ranked-board'
		}
	}
} catch {
	$failure = $_.Exception.Message
}
# SFV's league icons as the game loaded them, or why not, and its league
# names as the game gave them (SfvArt).
foreach ($i in 0, 1) {
	if ($ranked -and (Test-Path $logOf[$i])) { Select-String -Path $logOf[$i] -Pattern "art: /Game/CommonAsset/LeagueIcon/|ranks: SFV's leagues" | ForEach-Object { "copy ${i}: " + $_.Line.Substring(24) } }
}
$passed = Complete-Run $failure $processes $keep.IsPresent "$games game(s) in a public room, no desync"
# Room hosts the supervisor started outlive it when it is killed: end ours.
Get-Process sfve-room-host -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $roomHost } | Stop-Process -Force
exit $(if ($passed) { 0 } else { 1 })
