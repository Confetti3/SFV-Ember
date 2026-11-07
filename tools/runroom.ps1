param([int]$lag = 0, [int]$delay = 2, [int]$games = 2, [string]$p1 = 'RYU:0:1', [string]$p2 = 'CNL:0:2', [string]$stage = 'S25', [int]$rounds = 1, [int]$seconds = 30, [int]$mashFrom = 300, [int]$seed0 = 101, [int]$seed1 = 202, [int]$hashEvery = 0, [int]$gameSeconds = 240, [string]$shots = '', [int]$presses = 0, [int]$rematches = -1, [int]$firstTo = 0, [switch]$padAnswers, [string]$unowned0 = '', [string]$unowned1 = '', [int]$watchers = 0, [switch]$watchLate, [int]$watcherLeaves = 0, [switch]$keep)
# Two copies play Ember's private room (EMBER_LIBRARY.md section 12): copy 0
# hosts it, copy 1 joins by its invitation, both pick and Ready, the match
# launches from the main menu and both play it with seeded random input, and
# both go back to the room. Repeated for -games games. -presses N presses DOWN
# N times on each copy's virtual pad at the main menu before the first Ready
# (the room holds the pads from the game; EMBER_LIBRARY.md section 13's
# frame-0 desync followed such presses). -shots names a folder
# for a screenshot of each copy in each fight and back in the room. Prints the
# copies' match results, any desync, and PASS or FAIL.
#
# -rematches N (EMBER_LIBRARY.md section 19): the room's table has Instant
# rematch on (and a first-to-N set with -firstTo), and one battle plays N + 1
# games: over each of the first N WINs both copies answer the post-match
# prompt Rematch ("rematch yes"), and the game restarts in place on both;
# over the last, copy 0 answers Rematch and copy 1 Back to room ("rematch
# no"), and both go back to the room. The table's score must count each game.
# -games is then ignored. -shots also takes the prompt over each WIN.
# -padAnswers answers on the prompt itself with each copy's virtual pad (A
# Rematch, B Back to room), as a player does, instead of the pipe.
#
# -unowned0 / -unowned1 give copy 0 / copy 1 a -sfve_unowned list
# (EMBER_LIBRARY.md section 23): content that copy's player may not pick.
# Marking the other copy's pick shows that an opponent's pick plays as picked
# whatever this copy owns: both copies must load the same picks.
#
# -watchers 1 (EMBER_LIBRARY.md section 25): a third copy, copy 2, joins the
# room and watches table 0 ("watchtable 0", the room board's Watch) before
# the first Ready. It must watch every game, decide every WIN on the
# fighters' frame, and its checkpoints are compared with the fighters' (the
# same game and frame). -watchLate has it Watch only once the first game's
# fight is on: as SF4's room has it, a member who watches during a game
# watches from the next one, so it must sit the first game out and watch the
# rest. -watcherLeaves N has it Stop watching ("stopwatching", the shell's
# Stop watching) N seconds into the first game it watches: it goes back to
# the room, and the fighters' game goes on unaffected.
$ErrorActionPreference = 'Stop'
# The checkout this script is in, whichever it is.
Set-Location (Split-Path -Parent $PSScriptRoot)
# The mod logs under %APPDATA%\SFVEmber\logs (mod/Log.cxx), whether the launcher or the proxy started it.
$logs = Join-Path $env:APPDATA 'SFVEmber\logs'
$logOf = @("$logs\sfv_ember.log", "$logs\sfv_ember.1.log", "$logs\sfv_ember.2.log")
if ($watchers -lt 0 -or $watchers -gt 1) { throw '-watchers is 0 or 1' }
$copies = if ($watchers) { @(0, 1, 2) } else { @(0, 1) }
# The fighters' game (from 0) each of the watcher's battles was, in order.
$watchedGames = New-Object System.Collections.Generic.List[int]
$watcherLeft = $false
function Ctl([int]$i, [string]$line) { python -I tools\sfve.py ctl $i $line }
function Status([int]$i) { (Ctl $i status).Substring(3) | ConvertFrom-Json }
function WaitFor([string]$what, [int]$timeout, [scriptblock]$done) {
  $deadline = (Get-Date).AddSeconds($timeout)
  while ((Get-Date) -lt $deadline) { if (& $done) { return }; Start-Sleep 1 }
  throw "timed out waiting for $what"
}
function Answer([int]$i, [string]$say) {
  if (!$padAnswers) { Ctl $i "rematch $say" | Out-Null; return }
  $button = if ($say -eq 'yes') { 'A' } else { 'B' }
  Ctl $i "pad 0 ${button}:120 -:250" | Out-Null
}
function Count([int]$i, [string]$pattern) {
  # A copy not launched in this run (the watcher's) counts nothing.
  if ($copies -notcontains $i -or -not (Test-Path $logOf[$i])) { return 0 }
  @(Select-String -Path $logOf[$i] -Pattern $pattern).Count
}
function Shot([string]$name) {
  if (!$shots) { return }
  foreach ($i in $copies) { python -I tools\sfve.py shot $i "$shots\$name-$i.png" | Out-Null }
}
# Copy 2 watches table 0 (the room board's Watch); `$next`: from the next game.
function Watch([bool]$next) {
  WaitFor "copy 2's Watch to be taken" 30 { (Ctl 2 'watchtable 0') -like 'ok*' }
  WaitFor 'copy 2 watching table 0' 30 { $w = (Status 2).room.watching; $w -and $w.table -eq 0 }
  $w = (Status 2).room.watching
  if ($next -and -not $w.next) { throw 'copy 2 watches the game in play; a watcher who comes during a game watches from the next' }
  "copy 2 watches table 0 $(if ($w.next) { 'from the next game' } else { 'from this game on' })"
}
# A game the watcher should see: its stream starts with the fighters' game
# `$game` (from 1), and its battle is that game's for the checkpoints.
function AwaitWatcher([int]$game, [int]$before) {
  WaitFor "copy 2's stream of game $game" 120 { (Count 2 'netplay: room match watched') -gt $before }
  $script:watchedGames.Add($game - 1)
}

python -I tools\sfve.py kill | Out-Null
$seeds = @($seed0, $seed1, 303)
$unowned = @($unowned0, $unowned1, '')
foreach ($i in $copies) {
  $marks = @()
  if ($unowned[$i]) { $marks += "-sfve_unowned=$($unowned[$i])" }
  python -I tools\sfve.py launch $i --mode netplay -- '-sfve_pads=01' '-sfve_background=1' '-sfve_room=1' "-sfve_mash=$($seeds[$i])" "-sfve_mash_from=$mashFrom" "-sfve_lag=$lag" "-sfve_hash_every=$hashEvery" $marks | Out-Null
}
$failure = ''
try {
  WaitFor 'the runtime on every copy' 120 { foreach ($i in $copies) { if ((Status $i).ember.host -ne 'running') { return $false } }; $true }
  $instant = if ($rematches -ge 0) { 1 } else { 0 }
  WaitFor 'room commands to be taken' 30 { (Ctl 0 "room $rounds $seconds $instant $firstTo") -like 'ok*' }
  $inv = ''
  WaitFor 'the invitation' 60 { $script:inv = Ctl 0 invite; $script:inv -like 'ok *' }
  $inv = $inv.Substring(3)
  WaitFor 'the join to be taken' 30 { (Ctl 1 "join $inv") -like 'ok*' }
  WaitFor 'both copies in the room' 90 { $s0 = Status 0; $s1 = Status 1; $s0.room.room -eq 'joined' -and $s1.room.room -eq 'joined' -and $s0.room.members -eq 2 -and $s1.room.members -eq 2 }
  if ($watchers) {
    WaitFor "copy 2's join to be taken" 30 { (Ctl 2 "join $inv") -like 'ok*' }
    WaitFor 'all three copies in the room' 90 { foreach ($i in $copies) { $s = Status $i; if ($s.room.room -ne 'joined' -or $s.room.members -ne 3) { return $false } }; $true }
    if (-not $watchLate) { Watch $false }
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
  if ($rematches -ge 0) {
    if ($watchLate -or $watcherLeaves) { throw '-watchLate and -watcherLeaves are for -games runs, not -rematches' }
    $starts = @((Count 0 'netplay: room match as P'), (Count 1 'netplay: room match as P'))
    $wstart = Count 2 'netplay: room match watched'
    WaitFor "the battle's sessions" 180 {
      foreach ($i in 0, 1) { if ((Status $i).room.canReady) { Ctl $i ready | Out-Null } }
      (Count 0 'netplay: room match as P') -gt $starts[0] -and (Count 1 'netplay: room match as P') -gt $starts[1]
    }
    if ($watchers) { AwaitWatcher 1 $wstart }
    $generation = (Status 0).room.table.generation
    for ($game = 1; $game -le $rematches + 1; $game++) {
      $last = $game -eq $rematches + 1
      $results = @((Count 0 'netplay: match result'), (Count 1 'netplay: match result'))
      $answers = @((Count 0 "the WIN's answers"), (Count 1 "the WIN's answers"), (Count 2 "the WIN's answers"))
      Start-Sleep 20
      Shot "game$game-fight"
      WaitFor "game $game's result on both copies" $gameSeconds { (Count 0 'netplay: match result') -gt $results[0] -and (Count 1 'netplay: match result') -gt $results[1] }
      # A game that ends the first-to-N set offers no rematch: both copies
      # decline at once, and the WIN may be over before the status shows it.
      $decided = { (Count 0 "the WIN's answers") -gt $answers[0] -and (Count 1 "the WIN's answers") -gt $answers[1] }
      WaitFor "the WIN's prompt on both copies after game $game" 30 {
        $r0 = (Status 0).battle.mode.rematch; $r1 = (Status 1).battle.mode.rematch
        ($r0.prompt -and $r1.prompt -and $r0.offered -and $r1.offered) -or (& $decided)
      }
      $setEnded = & $decided
      if ($setEnded) {
        if (-not $last) { throw "game ${game}'s WIN offered no rematch (did the set end early?)" }
        "game ${game}: no rematch offered (the set is over); both copies declined"
      } else {
        Start-Sleep -Milliseconds 1500 # the prompt opens over the WIN
        Shot "game$game-prompt"
        # Rematch ("yes") is parked by the runtime as the choice the held frames
        # carry; asked again until this copy's input shows it.
        $say = if ($last) { @('yes', 'no') } else { @('yes', 'yes') }
        Answer 0 $say[0]
        if ($shots) {
          Start-Sleep -Milliseconds 700
          Shot "game$game-prompt-one-chose"
        }
        Answer 1 $say[1]
        WaitFor "both copies' decision over game $game's WIN" 30 {
          foreach ($i in 0, 1) {
            $r = (Status $i).battle.mode.rematch
            if ($r.prompt -and $say[$i] -eq 'yes' -and -not ($r.choices -band (1 -shl $i))) { Answer $i 'yes' }
          }
          & $decided
        }
      }
      # The watcher plays P1's stream a little behind: it decides from the
      # fighters' answers once it reaches their frame.
      if ($watchers) { WaitFor "copy 2's decision over game $game's WIN" 60 { (Count 2 "the WIN's answers") -gt $answers[2] } }
      $decisions = foreach ($i in $copies) { (Select-String -Path $logOf[$i] -Pattern "the WIN's answers" | Select-Object -Last 1).Line.Substring(24) }
      foreach ($i in $copies) { "copy ${i}: " + $decisions[$i] }
      $frames = foreach ($d in $decisions) { if ($d -match 'frame (\d+):') { $Matches[1] } }
      if (@($frames | Select-Object -Unique).Count -ne 1) { throw "the copies decided on different frames ($($frames -join ' / '))" }
      $want = if ($last) { 'back to the room' } else { 'rematch in place' }
      foreach ($d in $decisions) { if ($d -notlike "*$want") { throw "game ${game}: expected '$want', got: $d" } }
      if ($last) {
        WaitFor 'every copy back in the room' 90 { foreach ($i in $copies) { if ((Count $i 'battle closed at its match') -eq 0 -or (Status $i).room.room -ne 'joined') { return $false } }; $true }
        Start-Sleep 3
        Shot "game$game-room"
      }
      # The room counts every game of the match: the same generation, one more on the score.
      if (-not $last) {
        WaitFor "the room's score after game $game" 30 {
          $t = (Status 0).room.table; $t1 = (Status 1).room.table
          ($t.score[0] + $t.score[1]) -eq $game -and ($t1.score[0] + $t1.score[1]) -eq $game
        }
      }
      $t = (Status 0).room.table
      if ($t) { "game ${game}: room score $($t.score -join '-'), generation $($t.generation) (battle's $generation)" } else { "game ${game}: the match ended; copy 0 has no seat now" }
      if (-not $last -and $t.generation -ne $generation) { throw "the room started a new match ($($t.generation)) instead of going on" }
      foreach ($i in 0, 1) {
        $mode = (Status $i).battle.mode
        "copy ${i}: rollbacks $($mode.rollbacks), longest $($mode.longestRollback), held frames $($mode.heldFrames), held replays $($mode.heldReplays), games in place $($mode.rematch.games)"
      }
    }
  }
  for ($game = 1; $rematches -lt 0 -and $game -le $games; $game++) {
    $starts = @((Count 0 'netplay: room match as P'), (Count 1 'netplay: room match as P'))
    $results = @((Count 0 'netplay: match result'), (Count 1 'netplay: match result'))
    $closes = @((Count 0 'battle closed at its match'), (Count 1 'battle closed at its match'))
    $wstart = Count 2 'netplay: room match watched'
    $wcloses = Count 2 "the room's battle closed"
    # The watcher sees this game unless it watches from the next one, or left.
    $watched = $watchers -and -not $watcherLeft -and -not ($watchLate -and $game -eq 1)
    # A seat whose fighter changes takes back the other seat's Ready (the
    # room's rule), and a Ready carries its pick: Ready whichever copy can,
    # until both sessions start.
    WaitFor "game $game's sessions" 180 {
      foreach ($i in 0, 1) { if ((Status $i).room.canReady) { Ctl $i ready | Out-Null } }
      (Count 0 'netplay: room match as P') -gt $starts[0] -and (Count 1 'netplay: room match as P') -gt $starts[1]
    }
    if ($watched) { AwaitWatcher $game $wstart }
    if ($watched -and $watcherLeaves -and $watchedGames.Count -eq 1) {
      # Stop watching mid-game: the watcher goes back to the room and leaves
      # the table; the fighters play on.
      Start-Sleep $watcherLeaves
      Shot "game$game-before-stop-watching"
      WaitFor "copy 2's Stop watching to be taken" 30 { (Ctl 2 stopwatching) -like 'ok*' }
      WaitFor 'copy 2 back in the room, watching nothing' 90 { (Count 2 "the room's battle closed") -gt $wcloses -and (Status 2).room.room -eq 'joined' -and -not (Status 2).room.watching }
      $script:watcherLeft = $true
      "copy 2 stopped watching game $game " + (Select-String -Path $logOf[2] -Pattern "the room's battle closed" | Select-Object -Last 1).Line.Substring(24)
      foreach ($i in 0, 1) { if ((Status $i).battle.mode.live -ne $true) { throw "copy $i's session ended when the watcher left" } }
      Start-Sleep 5
    } else {
      Start-Sleep 20 # past the intro, into the fight
    }
    if ($watchLate -and $game -eq 1) { Watch $true } # during the game: from the next one
    Shot "game$game-fight"
    WaitFor "game $game's result on both copies" $gameSeconds { (Count 0 'netplay: match result') -gt $results[0] -and (Count 1 'netplay: match result') -gt $results[1] }
    WaitFor "both copies back in the room after game $game" 90 { (Count 0 'battle closed at its match') -gt $closes[0] -and (Count 1 'battle closed at its match') -gt $closes[1] -and (Status 0).room.room -eq 'joined' -and (Status 1).room.room -eq 'joined' }
    if ($watched -and -not $watcherLeft) {
      WaitFor "copy 2 back in the room after game $game" 90 { (Count 2 "the room's battle closed") -gt $wcloses -and (Status 2).room.room -eq 'joined' }
    }
    Start-Sleep 3
    Shot "game$game-room"
    foreach ($i in 0, 1) {
      # The battle status as last published, in the game's last frames.
      $mode = (Status $i).battle.mode
      "copy ${i}: " + (Select-String -Path $logOf[$i] -Pattern 'netplay: match result' | Select-Object -Last 1).Line.Substring(24) + " rollbacks $($mode.rollbacks), longest $($mode.longestRollback)"
    }
    if ($watched -and -not ($watcherLeft -and $watchedGames.Count -eq 1 -and $watchedGames[0] -eq $game - 1)) {
      "copy 2: " + (Select-String -Path $logOf[2] -Pattern 'netplay: match result' | Select-Object -Last 1).Line.Substring(24)
    }
  }
  if ($watchers -and -not $watchedGames.Count) { throw 'copy 2 watched no game' }
} catch {
  $failure = $_.Exception.Message
}
foreach ($i in $copies) {
  try { "copy $i status: " + ((Status $i).room | ConvertTo-Json -Compress) } catch { "copy $i is not answering" }
  Select-String -Path $logOf[$i] -Pattern 'desync|mismatch' | ForEach-Object { "copy ${i}: " + $_.Line.Substring(24) }
}
# The copies close here, whatever happened above; the logs stay.
if (!$keep) { python -I tools\sfve.py kill | Out-Null }
# Each copy logs its checkpoint hashes (every 60 confirmed frames); a frame
# number that falls back starts the next game.
function Checkpoints([int]$i) {
  $hashes = @{}; $game = 0; $previous = -1
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
$desyncs = (Count 0 'desync|mismatch') + (Count 1 'desync|mismatch') + $differing.Count
if ($watchers) {
  # The watcher's battle k is the fighters' game $watchedGames[k]: its
  # checkpoints against copy 0's of that game and frame (copy 0's equal
  # copy 1's above).
  $h2 = Checkpoints 2
  $watchedKeys = @(); $watcherDiffering = @()
  foreach ($key in $h2.Keys) {
    $battle, $frame = $key -split '/'
    if ([int]$battle -ge $watchedGames.Count) { continue }
    $theirs = "$($watchedGames[[int]$battle])/$frame"
    if (-not $h0.ContainsKey($theirs)) { continue }
    $watchedKeys += $theirs
    if ($h0[$theirs] -ne $h2[$key]) { $watcherDiffering += $theirs }
  }
  "watcher checkpoints: $($watchedKeys.Count) compared with the fighters', $($watcherDiffering.Count) differing (games watched: $(($watchedGames | ForEach-Object { $_ + 1 }) -join ', '))"
  $desyncs += (Count 2 'desync|mismatch') + $watcherDiffering.Count
  if (-not $failure -and -not $watchedKeys.Count) { $failure = "no watcher checkpoint could be compared" }
}
$played = if ($rematches -ge 0) { $rematches + 1 } else { $games }
if ($failure)  { "FAIL: $failure" } elseif ($desyncs) { "FAIL: $desyncs desync line(s)" } else { "PASS: $played game(s), no desync" }
