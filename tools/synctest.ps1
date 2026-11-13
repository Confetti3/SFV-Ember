param([string]$p1 = 'RYU', [string]$p2 = 'KEN', [string]$rules = '', [int]$mash = 31337, [int]$mashFrom = 600, [int]$distance = 8, [int]$schedule = 1, [int]$frames = 7000, [int]$waitSeconds = 420, [switch]$keep, [int]$learn = 1, [switch]$moves, [switch]$practice, [string]$moveset = '', [int]$idle = -1, [switch]$tiny, [int]$tinySides = 3, [switch]$socketLog, [switch]$rngAudit, [switch]$check, [int]$fma3 = -1)
# One copy in synctest mode: both players mash, every rolled-back stretch is
# first played with wrong inputs, rollbacks of random depth and spacing (up
# to -distance, from seed -schedule; 0 for the fixed schedule), learning
# (every differing region is tallied, the battle goes on; -learn 0 stops at
# the first difference instead). Prints the report and the differences.
# -moves mashes the game's system moves (V-Skill, V-Trigger, V-Reversal,
# Critical Art motions, EX, throws: input::Moves) instead of single buttons;
# -practice gives both fighters Training's refilling gauges from the first
# frame, so every V-Trigger and Critical Art asked for is played.
# -moveset supers|cody|projectiles plays one set of moves (input::MoveSet:
# Critical Arts that play out; Z29's knife, pipe, rock and Critical Art;
# projectile specials and V-Triggers left to run out) and implies -moves.
# Every block of moves faces the way its fighter does. -idle 0|1 has that
# player (P1 or P2) stand still, and the other walk forward at the start of
# each block, so its moves land. -tiny draws -tinySides (1 P1, 2 P2, 3 both)
# small. -socketLog logs the fight's reads of the fighters' models
# (SocketReads) and prints them with the report; the `sockets:` lines (the
# reads recorded, given the straight read, missed and conflicting, and the
# microseconds per update the keys cost) are printed either way.
# -rngAudit checks every random draw of a battle update against the rollback
# state (RngAudit; docs/design/beta8/determinism.md): a generator outside it,
# or in it but not reseeded, is logged once with its caller, and the battle's
# end logs the counts (`rng audit:` lines, printed). The `costs:` lines (each
# kind of update's time, save, load, hash and the restore's callbacks, every
# 600 updates) are printed either way.
# -fma3 0|1 plays on the CRT math's SSE2 (0) or FMA3 (1) code (-sfve_fma3;
# without it SSE2, as every mode that rolls back takes;
# docs/design/beta8/rollback-fixes.md); the `crt:` line says which ran.
# -check ends with PASS (the run reached -frames, 0 failures, no conflicting
# socket read, and with -rngAudit no generator outside the rollback state)
# or FAIL, and the exit code says the same.
Set-Location (Split-Path -Parent $PSScriptRoot)
python -I tools\sfve.py kill | Out-Null
$extra = @()
if ($moveset) { $extra += "-sfve_moveset=$moveset" }
if ($idle -ge 0) { $extra += "-sfve_idle=$idle" }
if ($tiny) { $extra += "-sfve_tiny=$tinySides" }
if ($socketLog) { $extra += '-sfve_socket_log=1' }
if ($rngAudit) { $extra += '-sfve_rng_audit=1' }
if ($fma3 -ge 0) { $extra += "-sfve_fma3=$fma3" }
python -I tools\sfve.py launch 0 --mode synctest -- '-sfve_pads=01' '-sfve_background=1' "-sfve_mash=$mash" "-sfve_mash_from=$mashFrom" "-sfve_distance=$distance" '-sfve_perturb=1' "-sfve_learn=$learn" "-sfve_schedule=$schedule" "-sfve_moves=$([int][bool]$moves)" "-sfve_practice=$([int][bool]$practice)" $extra | Out-Null
for ($k = 0; $k -lt 60; $k++) { Start-Sleep 2; if ((python -I tools\sfve.py ctl 0 status) -match '"scene":"0x2D98660"') { break } }
Start-Sleep 3
python -I tools\sfve.py ctl 0 "launch versus $p1 $p2 $rules" | Out-Null
$deadline = (Get-Date).AddSeconds($waitSeconds)
$s = ''
while ((Get-Date) -lt $deadline) {
  Start-Sleep 5
  $s = python -I tools\sfve.py ctl 0 status
  $m = [regex]::Match($s, '"framesRun":(\d+)')
  if ($m.Success -and [int]$m.Groups[1].Value -ge $frames) { break }
  if ($s -match '"mode":\{"ok":false') { break }
}
$i = $s.IndexOf('"mode":{"ok"')
if ($i -ge 0) { $s.Substring($i, [Math]::Min(700, $s.Length - $i)) } else { 'no synctest report' }
$log = Join-Path $env:APPDATA 'SFVEmber\logs\sfv_ember.log'
$pattern = if ($socketLog) { 'synctest:|learn:|adapter: (expected|no |socket|input unit)|socket:|sockets:|tiny:|rng audit|costs:|crt:' } else { 'synctest:|learn:|adapter: (expected|no |socket|input unit)|sockets:|tiny:|rng audit|costs:|crt:' }
Select-String -Path $log -Pattern $pattern | ForEach-Object { $_.Line.Substring(24) }
if (!$keep) { python -I tools\sfve.py kill | Out-Null }
if ($check) {
  $run = [regex]::Match($s, '"framesRun":(\d+)')
  $failures = [regex]::Match($s, '"failures":(\d+)')
  $conflicts = @(Select-String -Path $log -Pattern 'sockets: .* ([1-9]\d*) conflicting' | Select-Object -Last 1)
  $why = @()
  if (-not $run.Success -or [int]$run.Groups[1].Value -lt $frames) { $why += "the run stopped before $frames frames" }
  if (-not $failures.Success -or [int]$failures.Groups[1].Value -ne 0) { $why += "failures: $(if ($failures.Success) { $failures.Groups[1].Value } else { 'no report' })" }
  if ($conflicts.Count) { $why += 'a socket read conflicted with the straight read of its state' }
  if ($rngAudit -and @(Select-String -Path $log -Pattern 'rng audit: a draw from a generator outside').Count) { $why += 'a random generator outside the rollback state was drawn from' }
  if ($why.Count) { "FAIL: $($why -join '; ')"; exit 1 }
  "PASS: $($run.Groups[1].Value) frames, 0 failures"
  exit 0
}
