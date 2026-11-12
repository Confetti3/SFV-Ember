param([int]$lag = 0, [int]$delay = 2, [int]$games = 2, [string]$p1 = 'RYU:0:1', [string]$p2 = 'CNL:0:2', [string]$stage = 'S25', [int]$rounds = 1, [int]$seconds = 30, [int]$mashFrom = 300, [int]$seed0 = 101, [int]$seed1 = 202, [int]$hashEvery = 0, [int]$gameSeconds = 240, [string]$shots = '', [int]$presses = 0, [int]$rematches = -1, [int]$firstTo = 0, [switch]$padAnswers, [switch]$quickAnswer, [switch]$leaveAnswer, [string]$unowned0 = '', [string]$unowned1 = '', [switch]$gateOff0, [switch]$gateOff1, [string]$uninstalled0 = '', [string]$uninstalled1 = '', [string]$uninstalled2 = '', [int]$watchers = 0, [switch]$watchLate, [int]$watcherLeaves = 0, [string]$res0 = '', [string]$res1 = '', [switch]$moves, [switch]$practice, [int]$code0 = -1, [int]$code1 = -1, [switch]$keep, [switch]$tiny, [string]$size0 = '', [string]$size1 = '', [int]$skipAfter = -1, [int]$skipCopy = 1, [switch]$skipPad, [int]$skipMax = 700, [string]$moveset = '', [int]$idle = -1, [switch]$socketLog, [switch]$rngAudit, [string]$startPerturb = '')
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
# -quickAnswer: copy 0 also answers Rematch the moment its prompt shows (on
# its pad with -padAnswers), as a player still pressing from the fight does.
# -leaveAnswer: over the last WIN copy 1 leaves the room (the pipe's `leave`)
# instead of Back to room; the runtime's end of its battle must ride as its
# decline, so both copies still decide on the same frame.
#
# -firstTo N without -rematches (EMBER_LIBRARY.md section 28): a first-to-N
# set played a battle a game. After each game both copies' rooms must agree
# on the table's score; the game that gives a side its Nth win must end the
# set, record it on both members (the status's room.sets, the Members card's
# record) and clear the score. -games of 2N - 1 or more must end a set.
#
# -res0 / -res1 ("1920x1080") give copy 0 / copy 1 its own window size
# (SFVE_RES for that copy's launch), as two players' screens differ.
# -moves: each copy mashes the game's system moves (input::Moves: V-Skill,
# V-Trigger, V-Reversal, Critical Art motions, EX, throws).
# -moveset supers|cody|projectiles: one set of moves instead (input::MoveSet;
# implies -moves). -idle 0|1: that side's player stands still, and the other
# walks forward at the start of each block of moves. Every block faces the
# way its fighter does. -socketLog: each copy logs the fight's reads of the
# fighters' models (SocketReads); the lines are printed per copy at the end.
# -rngAudit: each copy checks every random draw of a battle update against
# the rollback state (RngAudit; docs/design/beta8/determinism.md) and its
# `rng audit:` lines are printed at the end. -startPerturb "R,O,A": copy 1
# (P2) adds A to the dword at offset O of region R of its start state before
# the start state's agreement (-sfve_start_perturb), which must repair it:
# the run fails unless copy 1 logs `start: repaired`. Each copy's `start:`,
# `costs:` and `input unit +0x2178` lines are printed at the end.
# -practice: both copies' fighters get Training's refilling gauges from the
# first frame (as a Practice room's), so every V-Trigger and Critical Art plays.
# -unowned0 / -unowned1 give copy 0 / copy 1 a -sfve_unowned list
# (EMBER_LIBRARY.md section 23): content that copy's player may not pick.
# Marking the other copy's pick shows that an opponent's pick plays as picked
# whatever this copy owns: both copies must load the same picks.
#
# -gateOff0 / -gateOff1 start copy 0 / copy 1 with Ember's ownership gate
# off (-sfve_ownership=0, development): that copy's player may pick a look
# the account's entitlement cache lacks (EMBER_LIBRARY.md section 41), such
# as a costume SFV's menus never offered this copy. Each copy's pick reply
# is printed: one that reads "(fitted)" was changed to a look that copy's
# player can ready, so the game played is not the one asked for.
#
# -uninstalled0 / -uninstalled1 / -uninstalled2 give copy 0, 1 or 2 a
# -sfve_uninstalled list of stage codes (EMBER_LIBRARY.md section 38): stages
# that copy treats as not installed. The copies must still share a build id;
# -stage RND plays Random, which must land only on stages both fighters have.
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
#
# The decided match's scenes (docs/design/beta8/skip.md): every game prints
# how many frames each copy spent from the decision to the result and the
# skips applied, which must be the same frames on both copies (and the
# watcher's), with the result on the same frame. The seeded scripts press
# nothing in the scenes, so a plain run plays them whole (about 1545 frames)
# and must apply no skip. -skipAfter N: copy -skipCopy (0 or 1, default 1)
# presses an attack N frames into each game's scenes (the pipe's
# "skipafter"); at N >= 60 (IntroSkip.hxx's guard) both copies must skip, the
# scenes then taking fewer than -skipMax frames (default 700); below 60 the
# press falls inside the guard and no copy may apply a skip (the unskipped
# length varies with the fighters and the finish, so no frame count is
# checked). -skipPad presses copy -skipCopy's virtual pad (X) instead,
# about N frames (default 90) after its log shows the decision: the player's
# own path, through the pads.
$ErrorActionPreference = 'Stop'
# The checkout this script is in, whichever it is.
Set-Location (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'verdict.ps1')
# The copies this run starts are listed under a run of its own (SFVE_RUN, tools/sfvelib.py), so `sfve.py kill` ends only these.
$env:SFVE_RUN = [guid]::NewGuid().ToString('N')
# The mod logs under %APPDATA%\SFVEmber\logs (mod/Log.cxx), whether the launcher or the proxy started it.
$logs = Join-Path $env:APPDATA 'SFVEmber\logs'
$logOf = @("$logs\sfv_ember.log", "$logs\sfv_ember.1.log", "$logs\sfv_ember.2.log")
# A desync's lines; a start state's repair (its report is filed as a desync's
# is) is not one: the match then plays from the same state.
$desyncPattern = '^(?!.*desync report)(?!.*start state).*(desync|mismatch)'
if ($watchers -lt 0 -or $watchers -gt 1) { throw '-watchers is 0 or 1' }
$copies = if ($watchers) { @(0, 1, 2) } else { @(0, 1) }
# The fighters' game (from 0) each of the watcher's battles was, in order.
$watchedGames = New-Object System.Collections.Generic.List[int]
$watcherLeft = $false
function Ctl([int]$i, [string]$line) { python -I tools\sfve.py ctl $i $line }
function Status([int]$i) { (Ctl $i status).Substring(3) | ConvertFrom-Json }
# A copy whose match ended diverged (SfvVerification: "the fight ends") or
# whose table's game was cancelled never reports the result a wait is for,
# so every wait ends at once with that as the failure.
function Ended {
  foreach ($i in $copies) { if ((Count $i 'desync: checkpoint .* the fight ends|game was cancelled') -gt 0) { return $i } }
  -1
}
function WaitFor([string]$what, [int]$timeout, [scriptblock]$done) {
  $deadline = (Get-Date).AddSeconds($timeout)
  while ((Get-Date) -lt $deadline) {
    if (& $done) { return }
    $ended = Ended
    if ($ended -ge 0) { throw "copy ${ended}'s game diverged or was cancelled while waiting for $what" }
    Start-Sleep 1
  }
  throw "timed out waiting for $what"
}
function Answer([int]$i, [string]$say) {
  # Leaving the room over the WIN: the runtime ends the battle, which the
  # held frames carry as this fighter's decline (RoomBattle::DeclineToExit).
  if ($say -eq 'leave') { Ctl $i 'leave' | Out-Null; return }
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
# The same, without waiting: for a shot taken while the run must answer in
# time. A shot of three copies can take several seconds (the 2026-11-11 S6
# run's B answer reached its prompt after the 10 s countdown because two
# shots were taken in between). Returns the shot processes, which are also
# kept for WaitShots.
$pendingShots = New-Object System.Collections.Generic.List[object]
function ShotAsync([string]$name, [string]$folder = $shots) {
  if (!$shots) { return @() }
  $started = foreach ($i in $copies) {
    # Start-Process runs in the process's directory, not the script's location.
    Start-Process -FilePath python -ArgumentList @('-I', 'tools\sfve.py', 'shot', "$i", "`"$folder\$name-$i.png`"") -WorkingDirectory (Get-Location).Path -WindowStyle Hidden -PassThru
  }
  foreach ($p in $started) { $script:pendingShots.Add($p) }
  return $started
}
# Waits for every shot ShotAsync started (at most `$seconds`).
function WaitShots([int]$seconds = 30) {
  foreach ($p in $script:pendingShots) { if (-not $p.WaitForExit($seconds * 1000)) { try { $p.Kill() } catch {} } }
  $script:pendingShots.Clear()
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

# The decided match's scenes. The guard before a press there counts
# (intro_skip::kScenesGuardFrames).
$guardFrames = 60
if ($skipCopy -lt 0 -or $skipCopy -gt 1) { throw '-skipCopy is 0 or 1' }
$skipWanted = $skipPad -or $skipAfter -ge $guardFrames
# Each copy's games as its log has them: frames from the decision to the
# result, the result's frame, and the skips applied before it ("<frame>:
# <what>"), in order.
function ScenesOf([int]$i) {
  $games = New-Object System.Collections.Generic.List[object]
  if ($copies -notcontains $i -or -not (Test-Path $logOf[$i])) { return ,$games }
  $skips = @()
  $pattern = 'netplay: the scenes took (\d+) frame\(s\), from the decision at frame (\d+) to the result at frame (\d+) \((\d+) skip|netplay: scenes skipped at frame (\d+): (.*)$'
  foreach ($m in Select-String -Path $logOf[$i] -Pattern $pattern) {
    $g = $m.Matches[0].Groups
    if ($g[1].Success) {
      $games.Add([pscustomobject]@{ Took = [int]$g[1].Value; From = [int]$g[2].Value; Result = [int]$g[3].Value; Skips = [int]$g[4].Value; At = $skips })
      $skips = @()
    } else {
      $skips += "$($g[5].Value): $($g[6].Value)"
    }
  }
  return ,$games
}
$scenesChecked = 0
# Game `$game`'s scenes on both fighters' copies: the same result frame and
# the same skips, and skipped (or not) as the run asked.
function CheckScenes([int]$game) {
  # Logged right after the result, which the caller has seen on both copies.
  $deadline = (Get-Date).AddSeconds(10)
  do {
    $s0 = ScenesOf 0; $s1 = ScenesOf 1
    $logged = $s0.Count -gt $script:scenesChecked -and $s1.Count -gt $script:scenesChecked
    if (-not $logged) { Start-Sleep -Milliseconds 250 }
  } while (-not $logged -and (Get-Date) -lt $deadline)
  if (-not $logged) {
    "game ${game}: no decided scenes logged on both copies"
    if ($skipWanted) { throw "game ${game}: no decided scenes to skip" }
    return
  }
  $a = $s0[$script:scenesChecked]; $b = $s1[$script:scenesChecked]
  $script:scenesChecked++
  $applied = if ($a.At.Count) { $a.At -join '; ' } else { 'none' }
  "game ${game}: scenes $($a.Took) frames on copy 0 (decided at $($a.From)), $($b.Took) on copy 1 (decided at $($b.From)); result at frame $($a.Result); skips applied: $applied"
  if ($a.Result -ne $b.Result) { throw "game ${game}: the copies took the result at frames $($a.Result) and $($b.Result)" }
  if (($a.At -join '|') -ne ($b.At -join '|')) { throw "game ${game}: the copies applied different skips: copy 0 [$($a.At -join '; ')], copy 1 [$($b.At -join '; ')]" }
  if ($skipWanted) {
    if ($a.Skips -lt 1) { throw "game ${game}: copy ${skipCopy}'s press skipped nothing" }
    foreach ($took in $a.Took, $b.Took) { if ($took -ge $skipMax) { throw "game ${game}: the skipped scenes still took $took frames (want fewer than $skipMax)" } }
  } else {
    # No press after the guard (none at all, or -skipAfter inside it): no
    # copy may apply a skip. Not a frame count: the unskipped scenes' length
    # depends on the fighters and the finish (1545 and 1865 frames seen).
    foreach ($s in $a, $b) { if ($s.Skips -gt 0) { throw "game ${game}: the scenes were skipped though no press came after the guard" } }
  }
}
# -shots, through a game's decided scenes until copy 0 logs its result:
# "game<N>-after-skip" about a second after copy 0 first applies a skip, and
# "game<N>-before-result" from about a second before the result (rolling
# shots from the decision on, a new set as soon as the last one is done; the
# one nearest a second before is kept). `$results` and `$decided0` are copy
# 0's counts of result and decision lines at the game's start. With
# `$rolling` false only the after-skip shot is taken. Every shot runs
# without blocking (ShotAsync), so the result is seen within a quarter
# second: the last WIN's B mash after it must land inside the 2 s guard.
function ScenesShots([int]$game, [int]$results0, [int]$decided0, [bool]$rolling) {
  if (!$shots) { return }
  $skips0 = Count 0 'netplay: scenes skipped at frame'
  $dir = Join-Path $shots "rolling-$game"
  New-Item -ItemType Directory -Force $dir | Out-Null
  $taken = New-Object System.Collections.Generic.List[object]
  $skipSeen = $null
  $afterSkipTaken = $false
  $deadline = (Get-Date).AddSeconds($gameSeconds)
  while ((Count 0 'netplay: match result') -le $results0) {
    if ((Get-Date) -gt $deadline) { throw "game ${game}: no result within $gameSeconds s" }
    if (-not $skipSeen -and (Count 0 'netplay: scenes skipped at frame') -gt $skips0) { $skipSeen = Get-Date }
    if ($skipSeen -and -not $afterSkipTaken -and ((Get-Date) - $skipSeen).TotalSeconds -ge 1) {
      $afterSkipTaken = $true
      ShotAsync "game$game-after-skip" | Out-Null
      "game ${game}: shot a second after copy 0's first skip"
    }
    $busy = $taken.Count -and ($taken[$taken.Count - 1].Shots | Where-Object { -not $_.HasExited })
    if ($rolling -and -not $busy -and (Count 0 'netplay: the match is decided at frame') -gt $decided0) {
      $name = "$($taken.Count)"
      $taken.Add([pscustomobject]@{ At = Get-Date; Name = (Join-Path $dir $name); Shots = @(ShotAsync $name $dir) })
      # Only the last few can be the one kept.
      if ($taken.Count -gt 8) { Get-ChildItem "$($taken[$taken.Count - 9].Name)-*.png" -ErrorAction SilentlyContinue | Remove-Item -Force }
    }
    Start-Sleep -Milliseconds 250
  }
  $resultAt = Get-Date
  # Of the sets already written, the one nearest a second before the result;
  # nothing waits for a set still being taken (the prompt counts down), and
  # the rolling folders go at the end of the run (RemoveRollingShots).
  $done = $taken | Where-Object { -not ($_.Shots | Where-Object { -not $_.HasExited }) }
  $best = $done | Sort-Object { [Math]::Abs(($resultAt - $_.At).TotalSeconds - 1) } | Select-Object -First 1
  if ($best) {
    foreach ($i in $copies) { if (Test-Path "$($best.Name)-$i.png") { Move-Item "$($best.Name)-$i.png" (Join-Path $shots "game$game-before-result-$i.png") -Force } }
    "game ${game}: shot $([int]($resultAt - $best.At).TotalMilliseconds) ms before copy 0's result was seen"
  }
}
# At the end of the run: every shot finished, and the rolling shots' folders gone.
function RemoveRollingShots {
  if (!$shots) { return }
  WaitShots 30
  Get-ChildItem $shots -Directory -Filter 'rolling-*' -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
}
# -skipPad: copy -skipCopy's pad presses X about -skipAfter frames (default
# 90) after its log shows game's decision, the count of those lines being
# `$before` at the game's start.
function PadSkip([int]$before) {
  WaitFor "copy ${skipCopy}'s decision" $gameSeconds { (Count $skipCopy 'netplay: the match is decided at frame') -gt $before }
  $frames = if ($skipAfter -ge 0) { $skipAfter } else { 90 }
  Start-Sleep -Milliseconds ([int]($frames * 1000 / 60))
  Ctl $skipCopy 'pad 0 X:100 -:150' | Out-Null
  "copy ${skipCopy} pressed X on its pad about $frames frames into the scenes"
}

python -I tools\sfve.py kill | Out-Null
$seeds = @($seed0, $seed1, 303)
$unowned = @($unowned0, $unowned1, '')
$gateOff = @($gateOff0, $gateOff1, $false)
$uninstalled = @($uninstalled0, $uninstalled1, $uninstalled2)
foreach ($i in $copies) {
  $marks = @()
  if ($unowned[$i]) { $marks += "-sfve_unowned=$($unowned[$i])" }
  if ($gateOff[$i]) { $marks += '-sfve_ownership=0' }
  if ($uninstalled[$i]) { $marks += "-sfve_uninstalled=$($uninstalled[$i])" }
  if ($moveset) { $marks += "-sfve_moveset=$moveset" }
  if ($idle -ge 0) { $marks += "-sfve_idle=$idle" }
  if ($socketLog) { $marks += '-sfve_socket_log=1' }
  if ($rngAudit) { $marks += '-sfve_rng_audit=1' }
  if ($startPerturb -and $i -eq 1) { $marks += "-sfve_start_perturb=$startPerturb" }
  $resOf = @($res0, $res1, '')
  $priorRes = $env:SFVE_RES
  if ($resOf[$i]) { $env:SFVE_RES = $resOf[$i] }
  python -I tools\sfve.py launch $i --mode netplay -- '-sfve_pads=01' '-sfve_background=1' '-sfve_room=1' "-sfve_mash=$($seeds[$i])" "-sfve_mash_from=$mashFrom" "-sfve_lag=$lag" "-sfve_hash_every=$hashEvery" "-sfve_moves=$([int][bool]$moves)" "-sfve_practice=$([int][bool]$practice)" $marks | Out-Null
  $env:SFVE_RES = $priorRes
}
$failure = ''
try {
  WaitFor 'the runtime on every copy' 120 { foreach ($i in $copies) { if ((Status $i).ember.host -ne 'running') { return $false } }; $true }
  if ($skipAfter -ge 0 -and -not $skipPad) {
    WaitFor "copy ${skipCopy}'s skip press to be scheduled" 30 { (Ctl $skipCopy "skipafter $skipAfter") -like 'ok*' }
    "copy ${skipCopy} presses an attack $skipAfter frames into each game's scenes$(if ($skipAfter -lt $guardFrames) { ' (inside the guard: nothing may be skipped)' })"
  }
  $instant = if ($rematches -ge 0) { 1 } else { 0 }
  WaitFor 'room commands to be taken' 30 { (Ctl 0 "room $rounds $seconds $instant $firstTo $([int][bool]$tiny)") -like 'ok*' }
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
  # -tiny opens the room with Tiny fighters allowed; -size0/-size1 tiny|normal
  # pick that copy's size (EMBER_LIBRARY.md section 59).
  $sizeOf = @($size0, $size1) | ForEach-Object { if ($_) { " size $_" } else { '' } }
  Write-Host "copy 0 pick: $(Ctl 0 "pick $p1 $stage$($sizeOf[0])")"
  Write-Host "copy 1 pick: $(Ctl 1 "pick $p2$($sizeOf[1])")"
  # -code0 / -code1: that copy's player's costume code (EMBER_LIBRARY.md
  # section 55), held on both copies as the battle loads.
  if ($code0 -ge 0) { Write-Host "copy 0 code: $(Ctl 0 "code $code0")" }
  if ($code1 -ge 0) { Write-Host "copy 1 code: $(Ctl 1 "code $code1")" }
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
      $decidedBefore = Count $skipCopy 'netplay: the match is decided at frame'
      $decided0 = Count 0 'netplay: the match is decided at frame'
      Start-Sleep 20
      Shot "game$game-fight"
      if ($skipPad) { PadSkip $decidedBefore }
      # -quickAnswer presses as the prompt shows, and the last WIN's B mash
      # below must land within 2 s of the result: those take no rolling
      # shots, only the after-skip one.
      ScenesShots $game $results[0] $decided0 (-not ($quickAnswer -or ($last -and $padAnswers)))
      if ($quickAnswer) {
        $deadline = (Get-Date).AddSeconds($gameSeconds)
        while (-not (Status 0).battle.mode.rematch.prompt) {
          if ((Get-Date) -gt $deadline) { throw "copy 0 never showed game ${game}'s prompt" }
          Start-Sleep -Milliseconds 50
        }
        Answer 0 'yes'
        "game ${game}: copy 0 pressed A as its prompt showed"
      }
      # Over the WIN the library's Branding::leaveGuardSeconds (2 s in SFV
      # Ember) ignores Back to room for 2 s after the prompt opens, so a B
      # mashed to skip the scenes declines nothing. Copy 1 mashes B as soon as
      # its own log has the result (the prompt opens with it); the status
      # pipe is too slow to aim inside 2 s. Its real B comes later.
      if ($last -and $padAnswers -and -not $leaveAnswer -and $firstTo -eq 0) {
        $deadline = (Get-Date).AddSeconds($gameSeconds)
        while ((Count 1 'netplay: match result') -le $results[1]) {
          if ((Get-Date) -gt $deadline) { throw "game ${game}: copy 1 never logged its result" }
          Start-Sleep -Milliseconds 50
        }
        $mashed = Get-Date
        Ctl 1 'pad 0 B:100 -:150' | Out-Null
        Ctl 1 'pad 0 B:100 -:150' | Out-Null
        $took = [int]((Get-Date) - $mashed).TotalMilliseconds
        if ($took -gt 1800) { throw "game ${game}: the mash took $took ms, past the guard; the check proves nothing" }
        "game ${game}: copy 1 mashed B twice within $took ms of its result"
        Start-Sleep -Milliseconds (2600 - $took)
        if ((Count 1 "the WIN's answers") -gt $answers[1]) { throw "game ${game}: copy 1's B inside the 2 s guard declined the rematch" }
        "game ${game}: copy 1's B inside the guard declined nothing"
      }
      WaitFor "game $game's result on both copies" $gameSeconds { (Count 0 'netplay: match result') -gt $results[0] -and (Count 1 'netplay: match result') -gt $results[1] }
      CheckScenes $game
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
        # The prompt counts down 10 s: its shots must not hold the answers back.
        ShotAsync "game$game-prompt" | Out-Null
        # Rematch ("yes") is parked by the runtime as the choice the held frames
        # carry; asked again until this copy's input shows it.
        $say = if ($last) { @('yes', $(if ($leaveAnswer) { 'leave' } else { 'no' })) } else { @('yes', 'yes') }
        Answer 0 $say[0]
        if ($shots) {
          Start-Sleep -Milliseconds 700
          ShotAsync "game$game-prompt-one-chose" | Out-Null
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
        # With -leaveAnswer copy 1 left the room: its battle closes, and it is in none.
        WaitFor 'every copy back in the room' 90 { foreach ($i in $copies) { if ((Count $i 'battle closed at its match') -eq 0 -or ((Status $i).room.room -ne 'joined' -and -not ($leaveAnswer -and $i -eq 1))) { return $false } }; $true }
        if ($leaveAnswer -and (Count 1 'the room asks the battle to end over the WIN') -eq 0) { throw "copy 1's leave over the WIN was not carried as its decline" }
        Start-Sleep 3
        Shot "game$game-room"
        # Rematch on the results (the library's Branding::rematchOnResults):
        # copy 1's B over the WIN is taken back, so its results show the prompt
        # with Rematch focused, and copy 0's prompt waits there with its own
        # Rematch standing as its Ready. Copy 1's A there readies it, and the
        # room starts the next game from the results.
        if ($padAnswers -and -not $leaveAnswer -and -not $setEnded) {
          foreach ($i in 0, 1) { $s = (Status $i).shell; "copy ${i}: results shown $($s.shown), screen '$($s.screen)', focus '$($s.focus)'" }
          WaitFor "the results' prompt on copy 0" 15 { $s = (Status 0).shell; $s.shown -and $s.screen -eq 'post-match' }
          WaitFor "the results' prompt with Rematch focused on copy 1" 15 { $s = (Status 1).shell; $s.shown -and $s.screen -eq 'post-match' -and $s.focus -eq 'again' }
          $entered = @((Count 0 "entering the room's match"), (Count 1 "entering the room's match"))
          Answer 1 'yes'
          "game ${game}: copy 1 pressed A on its results"
          WaitFor "the next game from Rematch on the results" 60 { (Count 0 "entering the room's match") -gt $entered[0] -and (Count 1 "entering the room's match") -gt $entered[1] }
          WaitFor "the next game's battle on both copies" 90 { (Count 0 "room's battle has loaded") -gt $game - 1 -and (Count 1 "room's battle has loaded") -gt $game - 1 }
          Start-Sleep 10
          Shot "game$game-results-rematch"
          "game ${game}: Rematch on the results started the next game on both copies"
        }
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
  # A first-to-N set across battles (-firstTo without -rematches): each game
  # counts on the table's score, and the game that gives a side N wins ends
  # the set, which the room records on both members (sets won and lost) and
  # clears the score for the next.
  # Scores are kept by copy (a seat's score is table.score[its slot]).
  $setGames = 0; $setsDone = 0; $lastScore = @(0, 0); $setsWon0 = 0
  for ($game = 1; $rematches -lt 0 -and $game -le $games; $game++) {
    $starts = @((Count 0 'netplay: room match as P'), (Count 1 'netplay: room match as P'))
    $results = @((Count 0 'netplay: match result'), (Count 1 'netplay: match result'))
    $closes = @((Count 0 'battle closed at its match'), (Count 1 'battle closed at its match'))
    $wstart = Count 2 'netplay: room match watched'
    $wcloses = Count 2 "the room's battle closed"
    $decidedBefore = Count $skipCopy 'netplay: the match is decided at frame'
    $decided0 = Count 0 'netplay: the match is decided at frame'
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
    if ($skipPad) { PadSkip $decidedBefore }
    ScenesShots $game $results[0] $decided0 $true
    WaitFor "game $game's result on both copies" $gameSeconds { (Count 0 'netplay: match result') -gt $results[0] -and (Count 1 'netplay: match result') -gt $results[1] }
    CheckScenes $game
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
    # The watcher's result line, when it saw the game's end (not after a Stop watching).
    $seen = if ($watched) { Select-String -Path $logOf[2] -Pattern 'netplay: match result' | Select-Object -Last 1 }
    if ($seen -and -not ($watcherLeft -and $watchedGames.Count -eq 1 -and $watchedGames[0] -eq $game - 1)) {
      "copy 2: " + $seen.Line.Substring(24)
    }
    if ($firstTo -gt 0) {
      $setGames++
      WaitFor "the room's set after game $game" 30 {
        $r0 = (Status 0).room; $r1 = (Status 1).room
        $scored = ($r0.table.score[0] + $r0.table.score[1]) -eq $setGames -and ($r1.table.score[0] + $r1.table.score[1]) -eq $setGames
        $ended = ($r0.sets.won + $r0.sets.lost) -gt $setsDone -and ($r1.sets.won + $r1.sets.lost) -gt $setsDone
        $scored -or $ended
      }
      $r0 = (Status 0).room; $r1 = (Status 1).room
      if (($r0.sets.won + $r0.sets.lost) -gt $setsDone) {
        # The set ended on this game: its winner had N - 1 wins before it.
        $winner = if ($r0.sets.won -gt $setsWon0) { 0 } else { 1 }
        if ($lastScore[$winner] -ne $firstTo - 1) { throw "game ${game} ended the set from $($lastScore -join '-'), not at $($firstTo - 1) wins" }
        if ($r0.sets.won -ne $r1.sets.lost -or $r0.sets.lost -ne $r1.sets.won) { throw "the copies' rooms record the sets apart: copy 0 $($r0.sets.won)-$($r0.sets.lost), copy 1 $($r1.sets.won)-$($r1.sets.lost)" }
        if ($r0.table.score[0] + $r0.table.score[1] -ne 0) { throw "the score was not cleared for the next set: $($r0.table.score -join '-')" }
        "game ${game}: the set is over, copy $winner won it $(if ($winner -eq 0) { $firstTo } else { $lastScore[0] })-$(if ($winner -eq 1) { $firstTo } else { $lastScore[1] }); sets: copy 0 $($r0.sets.won) W / $($r0.sets.lost) L, copy 1 $($r1.sets.won) W / $($r1.sets.lost) L"
        $setsDone++; $setGames = 0; $lastScore = @(0, 0)
        $setsWon0 = $r0.sets.won
        # The table's rotation after a set (winner stays): with nobody
        # queued both play on; a copy the rotation stood up sits again.
        "after the set: copy 0 at slot $($r0.slot), copy 1 at slot $($r1.slot)"
        foreach ($i in 0, 1) {
          if ((Status $i).room.slot -lt 0) { foreach ($seat in 0, 1) { if ((Status $i).room.slot -lt 0) { Ctl $i "sit 0 $seat" | Out-Null; Start-Sleep 2 } } }
        }
      } else {
        if (($r1.table.score -join '-') -ne ($r0.table.score -join '-')) { throw "the copies' rooms disagree on the score: $($r0.table.score -join '-') and $($r1.table.score -join '-')" }
        $score = @($r0.table.score[$r0.slot], $r0.table.score[$r1.slot])
        if (($score | Measure-Object -Maximum).Maximum -ge $firstTo) { throw "the set reached $($score -join '-') without ending" }
        "game ${game}: set score copy 0 $($score[0]) - $($score[1]) copy 1 (first to $firstTo)"
        $lastScore = $score
      }
    }
  }
  if ($firstTo -gt 0 -and $rematches -lt 0 -and $games -ge 2 * $firstTo - 1 -and $setsDone -eq 0) { throw "$games games of a first-to-$firstTo set ended no set" }
  if ($watchers -and -not $watchedGames.Count) { throw 'copy 2 watched no game' }
} catch {
  $failure = $_.Exception.Message
}
foreach ($i in $copies) {
  try { "copy $i status: " + ((Status $i).room | ConvertTo-Json -Compress) } catch { "copy $i is not answering" }
  if (Test-Path $logOf[$i]) { Select-String -Path $logOf[$i] -Pattern $desyncPattern | ForEach-Object { "copy ${i}: " + $_.Line.Substring(24) } }
}
# The copies close here, whatever happened above; the logs stay.
RemoveRollingShots
if (!$keep) { python -I tools\sfve.py kill | Out-Null }
# Each copy logs its checkpoint hashes (every 60 confirmed frames); tools/verdict.ps1
# compares them and holds the verdict rule: PASS needs a compared checkpoint.
$h0 = Get-Checkpoints $logOf[0]; $h1 = Get-Checkpoints $logOf[1]
$checkpoints = Compare-Checkpoints $h0 $h1
"checkpoints: $($checkpoints.Compared) compared, $($checkpoints.Differing) differing"
$desyncs = (Count 0 $desyncPattern) + (Count 1 $desyncPattern) + $checkpoints.Differing
if ($watchers) {
  # The watcher's battle k is the fighters' game $watchedGames[k]: its
  # checkpoints against copy 0's of that game and frame (copy 0's equal
  # copy 1's above).
  $h2 = Get-Checkpoints $logOf[2]
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
  # The watcher's scenes: each game it saw ended on the fighters' result
  # frame with the fighters' skips.
  $fought = @((ScenesOf 0) | ForEach-Object { "result $($_.Result), skips [$($_.At -join '; ')]" })
  foreach ($w in (ScenesOf 2)) {
    $line = "result $($w.Result), skips [$($w.At -join '; ')]"
    if ($fought -notcontains $line) {
      "copy 2's scenes differ from the fighters': $line"
      if (-not $failure) { $failure = "the watcher's scenes ended apart from the fighters' ($line)" }
    }
  }
  $desyncs += (Count 2 $desyncPattern) + $watcherDiffering.Count
  if (-not $failure -and -not $watchedKeys.Count) { $failure = "no watcher checkpoint could be compared" }
}
if ($socketLog) {
  foreach ($i in $copies) {
    if (Test-Path $logOf[$i]) { Select-String -Path $logOf[$i] -Pattern 'socket: frame' | ForEach-Object { "copy ${i}: " + $_.Line.Substring(24) } }
  }
}
foreach ($i in $copies) {
  if (Test-Path $logOf[$i]) {
    Select-String -Path $logOf[$i] -Pattern 'start: |costs: |input unit \+0x2178|rng audit' | ForEach-Object { "copy ${i}: " + $_.Line.Substring(24) }
  }
}
if ($startPerturb -and -not $failure -and (Count 1 'start: repaired after') -eq 0) {
  $failure = "copy 1's start state was perturbed ($startPerturb) and not repaired"
}
$played = if ($rematches -ge 0) { $rematches + 1 } else { $games }
$passed = Write-Verdict $failure $desyncs $checkpoints.Compared "$played game(s), no desync"
exit $(if ($passed) { 0 } else { 1 })
