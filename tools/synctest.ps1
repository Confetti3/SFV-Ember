param([string]$p1 = 'RYU', [string]$p2 = 'KEN', [string]$rules = '', [int]$mash = 31337, [int]$mashFrom = 600, [int]$distance = 8, [int]$schedule = 1, [int]$frames = 7000, [int]$waitSeconds = 420, [switch]$keep, [int]$learn = 1, [switch]$moves, [switch]$practice, [string]$moveset = '', [int]$idle = -1, [switch]$tiny, [int]$tinySides = 3, [switch]$socketLog)
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
# (SocketProbe) and prints them with the report.
Set-Location (Split-Path -Parent $PSScriptRoot)
python -I tools\sfve.py kill | Out-Null
$extra = @()
if ($moveset) { $extra += "-sfve_moveset=$moveset" }
if ($idle -ge 0) { $extra += "-sfve_idle=$idle" }
if ($tiny) { $extra += "-sfve_tiny=$tinySides" }
if ($socketLog) { $extra += '-sfve_socket_log=1' }
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
$pattern = if ($socketLog) { 'synctest:|learn:|adapter: (expected|no )|socket:|tiny:' } else { 'synctest:|learn:|adapter: (expected|no )|tiny:' }
Select-String -Path $log -Pattern $pattern | ForEach-Object { $_.Line.Substring(24) }
if (!$keep) { python -I tools\sfve.py kill | Out-Null }
