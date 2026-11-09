param([int]$firstTo = 1, [int]$lag = 0, [int]$delay = 2, [string]$p1 = 'RYU:0:1', [string]$p2 = 'CNL:0:2', [string]$stage = 'S25', [int]$rounds = 1, [int]$seconds = 30,
	[int]$mashFrom = 300, [int]$seed0 = 101, [int]$seed1 = 202, [int]$gameSeconds = 240, [string]$shots = '',
	[Parameter(Mandatory)][string]$bridgeExe, [string]$work = 'D:\sfv-re\scratch\match', [int]$port = 18799, [switch]$keep)
# Two copies play an organised match (EMBER_LIBRARY.md section 32, step C of
# section 31) end to end on a local stack (tools/localstack.ps1): a scratch
# Ember bridge on http://127.0.0.1:$port whose mock-local connection is the
# platform, with this tool as its provider (a provider credential issued by
# `ember-bridge credential`). The helper must be built with -Features selftest
# (scripts/build-helper.ps1): each copy makes an Ember ID of its own in
# $work\ember<N> and trusts the bridge.
#
# 1. Linking, as a platform links its players: for each copy a link intent
#    (subject player1, player2), whose code the copy claims on mock-local
#    (`link`, Linked accounts' Link account); the claim the bridge holds must
#    be that copy's Ember ID with the fingerprint the copy shows, and exactly
#    that claim is approved. Both copies then list the link (`links`).
# 2. The games apart: a USF4 match for the same two players (ember-room-v1,
#    Ultra only, this build), which the bridge's database holds as theirs and
#    awaiting them. Both copies refresh (`match refresh`): SFV Ember lists no
#    match. It is cancelled (one open match per player and connection).
# 3. The SFV match: `game: "sfv"`, ember-room-v1, first to -firstTo, copy 0
#    in slot 0. Both copies refresh and list it, and only it: SFV's, playable,
#    in their own slot.
# 4. Both save the table rules (-rounds of -seconds s: a match's room opens
#    with the saved settings), pick (-p1 on -stage, -p2) and press Play
#    (`match play`). From there the runtime's own tournament play claims,
#    opens or joins the room, binds and reports; the copies press Ready at the
#    bound table until the match is finished, seeded random input fighting.
# 5. The bridge holds the match completed, its scores those both copies' logs
#    saw (each game's winner by slot, both copies agreeing), and after a
#    refresh both copies list it completed with the same score.
# Then the checkpoints are compared and PASS or FAIL is printed. The settings
# the copies share (%APPDATA%\SFVEmber\settings.json) are put back as they
# were. -shots <folder>: the bound room, the fight and the room after it.
#
# Everything this starts stops at the end (-keep leaves the copies running);
# $work is left for its logs (bridge.log).
. (Join-Path $PSScriptRoot 'localstack.ps1')
if (!(Test-Path $bridgeExe)) { throw "$bridgeExe is not built" }
$build = (Get-Content data\pick-space.json -Raw | ConvertFrom-Json).build
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work, "$work\bridge" | Out-Null
$origin = "http://127.0.0.1:$port"
$connection = 'mock-local'
$settings = Join-Path $env:APPDATA 'SFVEmber\settings.json'
$settingsKept = "$work\settings.json.kept"
$db = "$work\bridge\bridge.sqlite3"
$processes = @()
$failure = ''; $passed = $false
# The shared settings as they were before anything starts: 'kept' (copied to
# $settingsKept) or 'absent'. A copy that fails stops the run here, before
# anything is started, so the end only ever restores a state it captured.
$settingsBefore = if (Test-Path $settings) { Copy-Item $settings $settingsKept; 'kept' } else { 'absent' }

# The provider's calls, with its credential. A refusal throws the bridge's
# error body.
function Provider([string]$method, [string]$path, $body = $null, [switch]$keyed) {
	$headers = @{ Authorization = "Bearer $script:token" }
	if ($keyed) { $headers['idempotency-key'] = [guid]::NewGuid().ToString() }
	$call = @{ Method = $method; Uri = "$origin$path"; Headers = $headers }
	if ($null -ne $body) { $call.Body = $body | ConvertTo-Json -Depth 8 -Compress; $call.ContentType = 'application/json' }
	try { Invoke-RestMethod @call } catch { throw "$method $path refused: $($_.ErrorDetails.Message)" }
}
# A first-to-$games match of $game for both linked players, copy 0 in slot 0.
function NewMatch([string]$game, [int]$games, [string]$edition, [object[]]$players) {
	Provider POST '/v1/matches' @{
		external_match_id = "runmatch-$game-$([guid]::NewGuid().ToString('N').Substring(0, 8))"; game = $game
		participants = @(foreach ($i in 0, 1) { @{ participant_id = $players[$i].participant; ember_id = $players[$i].emberId; slot = $i } })
		rules = @{ games_to_win = $games; draw_policy = 'replay_no_score'; native_rules_profile = 'ember-room-v1'; edition_policy = $edition
			character_policy = 'unrestricted_between_games'; stage_policy = 'p1_selects'; input_delay_policy = 'ember_existing_ready_policy' }
		observer_policy = 'authorized_only'; result_policy = 'two_player_agreement_or_review'; required_build_id = $build
		metadata = @{ round_label = 'runmatch' }
	} -keyed
}
# Both copies' assignment lists, refreshed: each refresh finished with no
# error for this bridge. Returns each copy's matches.
function Refresh([string]$bridgeId) {
	$before = foreach ($i in 0, 1) { (Status $i).match.list.finished }
	foreach ($i in 0, 1) { Answer (Ctl $i 'match refresh') | Out-Null }
	WaitFor 'both copies'' assignment lists' 30 { (Status 0).match.list.finished -gt $before[0] -and (Status 1).match.list.finished -gt $before[1] }
	foreach ($i in 0, 1) {
		$list = (Status $i).match.list
		if ($list.error -or $list.bridge -ne $bridgeId) { throw "copy ${i}'s refresh failed: $($list.error) ($($list.bridge))" }
		, @($list.matches)
	}
}
function Rows([string]$sql) { $text = python -I tools\bridgedb.py rows $db $sql; if ($LASTEXITCODE) { throw "bridgedb: $text" }; @($text | ConvertFrom-Json) }
# Copy $i's game winners (0 P1, 1 P2) from its log's match results, from line $from on.
function Winners([int]$i, [int]$from) {
	@(Select-String -Path $logOf[$i] -Pattern 'netplay: match result (\{.*?\}) at frame' | Select-Object -Skip $from | ForEach-Object { ($_.Matches[0].Groups[1].Value | ConvertFrom-Json).winner })
}

try {
	$config = Initialize-TestBridge $bridgeExe "$work\bridge" $port
	$script:token = (& $bridgeExe credential $config provider $connection 'runmatch provider' | Select-Object -Last 1).Trim()
	if ($LASTEXITCODE -or !$script:token) { throw "the provider credential was not issued ($LASTEXITCODE)" }
	$processes += Start-TestBridge $bridgeExe $config $work
	$bridgeId = (Invoke-RestMethod "$origin/.well-known/ember-bridge.json").bridge_id
	"bridge $bridgeId at $origin; provider $connection; build $build"

	Start-Copies $work @($seed0, $seed1) $mashFrom $lag
	$ids = Connect-Copies $origin
	"Ember IDs: $($ids -join ', ')"
	foreach ($i in 0, 1) {
		$own = (Status $i).match.build
		if ($own -ne $build) { throw "copy $i plays build $own, not data\pick-space.json's $build" }
	}

	# 1. Each copy links to its provider player with a code, as a platform's
	# player types it into Link account; the provider approves exactly the
	# claim whose Ember ID and fingerprint are that copy's.
	$players = @()
	foreach ($i in 0, 1) {
		$subject = "player$($i + 1)"
		$intent = Provider POST '/v1/link-intents' @{ subject = $subject; display_label = "Player $($i + 1)" }
		WaitIdentity $i (Answer (Ctl $i "link $connection $($intent.code)")) 'link claim'
		$fingerprint = (Status $i).match.claimFingerprint
		$claim = (Provider GET "/v1/link-intents/$($intent.intent_id)").claim
		if ($claim.state -ne 'pending' -or $claim.ember_id -ne $ids[$i] -or !$fingerprint -or $claim.fingerprint -ne $fingerprint) {
			throw "copy ${i}'s claim is not its own: $($claim | ConvertTo-Json -Compress) (the copy shows $fingerprint)"
		}
		$approved = Provider POST "/v1/link-intents/$($intent.intent_id)/approve" @{ claim_id = $claim.claim_id; ember_id = $ids[$i]; subject = $subject; replace = $false }
		WaitFor "copy $i's link in its list" 60 {
			WaitIdentity $i (Answer (Ctl $i 'links')) 'links'
			@((Status $i).match.links | Where-Object { $_.connection -eq $connection }).Count -eq 1
		}
		"copy $i linked: $subject, participant $($approved.participant_id), fingerprint $fingerprint"
		$players += [pscustomobject]@{ emberId = $ids[$i]; participant = $approved.participant_id }
	}

	# 2. A USF4 match for the same players: the bridge holds it as theirs,
	# and SFV Ember does not list it.
	$usf4 = NewMatch 'usf4' 1 'ultra_only' $players
	$held = Rows "SELECT m.game, m.state, COUNT(p.ember_id) AS players FROM matches m JOIN match_participants p ON p.match_id = m.id WHERE m.id = '$($usf4.match_id)' AND p.ember_id IN ('$($ids[0])', '$($ids[1])') GROUP BY m.id"
	"USF4 match $($usf4.match_id): the bridge holds " + ($held | ConvertTo-Json -Compress)
	if ($held.Count -ne 1 -or $held[0].game -ne 'usf4' -or $held[0].players -ne 2) { throw 'the bridge does not hold the USF4 match for both players' }
	$lists = Refresh $bridgeId
	foreach ($i in 0, 1) {
		"copy $i lists with a USF4 match open: $($lists[$i].Count) match(es)"
		if ($lists[$i].Count -ne 0) { throw "copy $i lists $($lists[$i] | ConvertTo-Json -Compress) beside a USF4 match" }
	}
	Provider POST "/v1/matches/$($usf4.match_id)/cancel" @{ reason = 'runmatch: the USF4 match only checks the games are apart'; expected_revision = $usf4.revision } -keyed | Out-Null

	# 3. The SFV match, listed by both copies in their own slots.
	$match = NewMatch 'sfv' $firstTo 'not_applicable' $players
	"SFV match $($match.match_id), first to $firstTo"
	$lists = Refresh $bridgeId
	foreach ($i in 0, 1) {
		"copy $i lists: " + ($lists[$i] | ConvertTo-Json -Compress -Depth 4)
		$row = @($lists[$i])
		if ($row.Count -ne 1 -or $row[0].matchId -ne $match.match_id -or $row[0].game -ne 'sfv' -or !$row[0].playable -or $row[0].slot -ne $i -or $row[0].gamesToWin -ne $firstTo) {
			throw "copy $i does not list the SFV match alone, playable, in slot $i"
		}
	}

	# 4. The table rules a match's room opens with, the picks, and Play.
	foreach ($i in 0, 1) {
		WaitFor "copy $i at the main menu" 60 { (Status $i).room.atMainMenu }
		Answer (Ctl $i "rules $rounds $seconds") | Out-Null
		WaitFor "copy $i's table rules" 15 { $r = (Status $i).match.rules; $r.rounds -eq $rounds -and $r.seconds -eq $seconds }
		Ctl $i "delay $delay" | Out-Null
	}
	Answer (Ctl 0 "pick $p1 $stage") | Out-Null
	Answer (Ctl 1 "pick $p2") | Out-Null
	$resultsBefore = foreach ($i in 0, 1) { Count $i 'netplay: match result' }
	foreach ($i in 0, 1) { Answer (Ctl $i "match play $($match.match_id)") | Out-Null }
	WaitFor 'both copies seated in the match''s room' 180 {
		foreach ($i in 0, 1) { if ((Status $i).match.phase -eq 'failed') { throw "copy ${i}'s match failed: $((Status $i).match.reason)" } }
		$s0 = Status 0; $s1 = Status 1
		$s0.match.phase -eq 'in_room' -and $s1.match.phase -eq 'in_room' -and $s0.room.slot -eq 0 -and $s1.room.slot -eq 1
	}
	foreach ($i in 0, 1) { "copy ${i}: in the match's room as P$((Status $i).room.slot + 1), host $((Status $i).room.host)" }
	Shot $shots 'match-room'

	# The games: Ready at the bound table whenever it is open, until the
	# runtime says the match is over on both copies.
	$deadline = (Get-Date).AddSeconds(($gameSeconds + 120) * (2 * $firstTo - 1) + 120)
	$shot = $false
	while ($true) {
		$s = foreach ($i in 0, 1) { Status $i }
		if ($s[0].match.phase -eq 'finished' -and $s[1].match.phase -eq 'finished') { break }
		foreach ($i in 0, 1) {
			if ($s[$i].match.phase -eq 'failed') { throw "copy ${i}'s match failed: $($s[$i].match.reason)" }
			if ($s[$i].room.canReady) { Ctl $i ready | Out-Null }
		}
		if (!$shot -and $s[0].room.match -eq 'playing' -and $s[1].room.match -eq 'playing') { Start-Sleep 20; Shot $shots 'match-fight'; $shot = $true }
		if ((Get-Date) -gt $deadline) { throw "the match did not finish (copy 0 $($s[0].match.phase), copy 1 $($s[1].match.phase))" }
		Start-Sleep 2
	}
	foreach ($i in 0, 1) { "copy ${i}: the match finished ($((Status $i).match.reason)); last result " + (LastLine $i 'netplay: match result') }
	Start-Sleep 3
	Shot $shots 'match-after'

	# 5. The bridge's result and the copies' own.
	$winners = foreach ($i in 0, 1) { , (Winners $i $resultsBefore[$i]) }
	"games' winners by slot: copy 0 saw $($winners[0] -join ' '), copy 1 saw $($winners[1] -join ' ')"
	if (($winners[0] -join ',') -ne ($winners[1] -join ',')) { throw 'the copies saw different winners' }
	$wins = @(@($winners[0] | Where-Object { $_ -eq 0 }).Count, @($winners[0] | Where-Object { $_ -eq 1 }).Count)
	if (($wins | Measure-Object -Maximum).Maximum -ne $firstTo) { throw "the copies' games ($($wins -join ' - ')) do not decide a first to $firstTo" }
	$completed = $null
	WaitFor 'the match completed on the bridge' 60 { $script:completed = Provider GET "/v1/matches/$($match.match_id)"; $script:completed.state -eq 'completed' }
	"bridge: $($completed.state), scores " + ($completed.scores | ConvertTo-Json -Compress) + '; games ' + (($completed.attempts | ForEach-Object { "$($_.state)" + $(if ($_.facts) { ' ' + ($_.facts | ConvertTo-Json -Compress -Depth 6) }) }) -join '; ')
	foreach ($i in 0, 1) {
		$score = @($completed.scores | Where-Object { $_.ember_id -eq $ids[$i] })
		if ($score.Count -ne 1 -or $score[0].wins -ne $wins[$i]) { throw "the bridge's score for copy $i is not the $($wins[$i]) game(s) both copies saw it win" }
	}
	"winner: copy $([array]::IndexOf($wins, $firstTo)) ($($ids[[array]::IndexOf($wins, $firstTo)])), $($wins -join ' - ') on the bridge and on both copies"
	WaitFor 'both copies out of the match''s room' 60 { (Status 0).room.room -eq 'idle' -and (Status 1).room.room -eq 'idle' }
	$lists = Refresh $bridgeId
	foreach ($i in 0, 1) {
		$row = @($lists[$i] | Where-Object { $_.matchId -eq $match.match_id })
		"copy $i lists after the match: " + ($row | ConvertTo-Json -Compress -Depth 4)
		if ($row.Count -ne 1 -or $row[0].state -ne 'completed' -or ($row[0].wins -join ',') -ne ($wins -join ',')) { throw "copy $i does not list the match completed at $($wins -join ' - ')" }
	}
} catch {
	$failure = $_.Exception.Message
} finally {
	foreach ($i in 0, 1) { try { "copy $i match: " + ((Status $i).match | ConvertTo-Json -Compress -Depth 5) } catch { } }
	$passed = Complete-Run $failure $processes $keep.IsPresent "an organised SFV match, first to $firstTo, completed on the bridge as both copies saw it, no desync"
	# Put the settings back as captured, once no copy can write them any more.
	if ($keep -or (Get-Process StreetFighterV -ErrorAction SilentlyContinue)) {
		"settings left as the copies saved them (a copy still runs); the ones from before are in $settingsKept"
	} elseif ($settingsBefore -eq 'kept') {
		Copy-Item $settingsKept $settings -Force
	} elseif (Test-Path $settings) {
		Remove-Item $settings
	}
}
exit $(if ($passed) { 0 } else { 1 })
