# The one verdict rule of the PowerShell acceptance runners (runroom.ps1,
# runpublic.ps1, runmatch.ps1, through localstack.ps1), dot-sourced by them; the
# Python runners follow it through sfvelib.verdict, tools/compare.py by its exit
# code. PASS needs evidence: at least one checkpoint compared on both copies and
# no difference, no desync line, no failure. Anything else is FAIL, and a runner
# ends with a nonzero exit code after its cleanup:
#   $passed = Write-Verdict ...   (or Complete-Run in localstack.ps1)
#   ... cleanup ...
#   exit $(if ($passed) { 0 } else { 1 })

# Copy log $log's checkpoint hashes by game and frame ("<game>/<frame>"): a
# frame far below the last one starts the next game. A log that was never
# written has none.
function Get-Checkpoints([string]$log) {
	$hashes = @{}; $game = 0; $previous = -1
	if (!(Test-Path $log)) { return $hashes }
	foreach ($m in Select-String -Path $log -Pattern 'checkpoint: frame (\d+) hash (\w+)') {
		$frame = [int]$m.Matches[0].Groups[1].Value
		if ($frame -lt $previous - 600) { $game++ }
		$previous = $frame; $hashes["$game/$frame"] = $m.Matches[0].Groups[2].Value
	}
	$hashes
}

# Two copies' checkpoints (Get-Checkpoints) compared: how many both logged and
# how many of those differ.
function Compare-Checkpoints($first, $second) {
	$common = @($first.Keys | Where-Object { $second.ContainsKey($_) })
	[pscustomobject]@{ Compared = $common.Count; Differing = @($common | Where-Object { $first[$_] -ne $second[$_] }).Count }
}

# Prints PASS ($passText) or FAIL with its reason, and returns $true only for
# PASS: no $failure, no desync line ($desyncs counts the differing checkpoints
# too) and at least one checkpoint compared ($compared).
function Write-Verdict([string]$failure, [int]$desyncs, [int]$compared, [string]$passText) {
	$why = if ($failure) { $failure } elseif ($desyncs) { "$desyncs desync line(s)" } elseif ($compared -lt 1) { 'no checkpoints compared' }
	if ($why) { Write-Host "FAIL: $why"; return $false }
	Write-Host "PASS: $passText"
	$true
}
