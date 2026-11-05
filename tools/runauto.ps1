param([int]$lag = 0, [int]$delay = 2, [int]$frame = 3000, [int]$mashFrom = 600, [int]$waitSeconds = 240, [string]$p1 = 'RYU:0:1', [string]$p2 = 'CNL:0:2', [string]$stage = '', [double]$gain = 0, [switch]$keep, [string]$matchend = '', [string]$rounds = '-', [string]$time = '-', [string]$after = '-', [int]$seed0 = 101, [int]$seed1 = 202, [int]$dump = -1, [int]$hashEvery = 0)
# Two linked copies that start their battle from the main menu by themselves.
Set-Location C:\Users\user\Desktop\sf4\sfv-ember
python -I tools\sfve.py kill | Out-Null
$stageArg = if ($stage) { "-sfve_stage=$stage" } else { "-sfve_stage=S25" }
python -I tools\sfve.py launch 0 --mode netplay -- '-sfve_pads=01' '-sfve_background=1' "-sfve_mash=$seed0" "-sfve_mash_from=$mashFrom" "-sfve_delay=$delay" '-sfve_link=1' "-sfve_lag=$lag" "-sfve_hash_every=$hashEvery" "-sfve_fighter=$p1" $stageArg "-sfve_rounds=$rounds" "-sfve_time=$time" "-sfve_after=$after" | Out-Null
python -I tools\sfve.py ctl 0 "freeze $frame" | Out-Null
if ($dump -ge 0) { python -I tools\sfve.py ctl 0 "dump $dump" | Out-Null }
if ($gain) { python -I tools\sfve.py ctl 0 "riftgain $gain" | Out-Null }
if ($matchend) { python -I tools\sfve.py ctl 0 "matchend $matchend" | Out-Null }
python -I tools\sfve.py ctl 0 host | Out-Null
$inv = ''
for ($k = 0; $k -lt 30 -and !$inv; $k++) { Start-Sleep 1; $inv = ([regex]'"invitation":"([^"]+)"').Match((python -I tools\sfve.py ctl 0 status)).Groups[1].Value }
python -I tools\sfve.py launch 1 --mode netplay -- '-sfve_pads=01' '-sfve_background=1' "-sfve_mash=$seed1" "-sfve_mash_from=$mashFrom" "-sfve_delay=$delay" '-sfve_link=1' "-sfve_lag=$lag" "-sfve_hash_every=$hashEvery" "-sfve_fighter=$p2" | Out-Null
python -I tools\sfve.py ctl 1 "freeze $frame" | Out-Null
if ($dump -ge 0) { python -I tools\sfve.py ctl 1 "dump $dump" | Out-Null }
if ($gain) { python -I tools\sfve.py ctl 1 "riftgain $gain" | Out-Null }
if ($matchend) { python -I tools\sfve.py ctl 1 "matchend $matchend" | Out-Null }
python -I tools\sfve.py ctl 1 "join $inv" | Out-Null
$deadline = (Get-Date).AddSeconds($waitSeconds)
while ((Get-Date) -lt $deadline) {
  $s0 = python -I tools\sfve.py ctl 0 status; $s1 = python -I tools\sfve.py ctl 1 status
  if ($s0 -match "`"frame`":$frame," -and $s1 -match "`"frame`":$frame,") { break }
  Start-Sleep 2
}
python -I tools\compare.py | Select-Object -First 2
python -I tools\sfve.py shot 0 D:\sfv-re\shots\f0.png | Out-Null
python -I tools\sfve.py shot 1 D:\sfv-re\shots\f1.png | Out-Null
# The copies sit frozen on the compared frame; close them unless asked to keep them.
if (!$keep) { python -I tools\sfve.py kill | Out-Null }




