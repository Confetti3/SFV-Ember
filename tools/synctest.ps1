param([string]$p1 = 'RYU', [string]$p2 = 'KEN', [string]$rules = '', [int]$mash = 31337, [int]$mashFrom = 600, [int]$distance = 8, [int]$schedule = 1, [int]$frames = 7000, [int]$waitSeconds = 420, [switch]$keep)
# One copy in synctest mode: both players mash, every rolled-back stretch is
# first played with wrong inputs, rollbacks of random depth and spacing (up
# to -distance, from seed -schedule; 0 for the fixed schedule), learning
# (every differing region is tallied, the battle goes on). Prints the report
# and the first difference seen in each region.
Set-Location C:\Users\user\Desktop\sf4\sfv-ember
python -I tools\sfve.py kill | Out-Null
python -I tools\sfve.py launch 0 --mode synctest -- '-sfve_pads=01' '-sfve_background=1' "-sfve_mash=$mash" "-sfve_mash_from=$mashFrom" "-sfve_distance=$distance" '-sfve_perturb=1' '-sfve_learn=1' "-sfve_schedule=$schedule" | Out-Null
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
}
$i = $s.IndexOf('"mode":{"ok"')
if ($i -ge 0) { $s.Substring($i, [Math]::Min(700, $s.Length - $i)) } else { 'no synctest report' }
$log = 'G:\SteamLibrary\steamapps\common\StreetFighterV\StreetFighterV\Binaries\Win64\SFVEmber\logs\sfv_ember.log'
Select-String -Path $log -Pattern 'synctest: started|learn:|adapter: expected' | ForEach-Object { $_.Line.Substring(24) }
if (!$keep) { python -I tools\sfve.py kill | Out-Null }
