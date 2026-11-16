# The one verdict rule of the PowerShell acceptance runners (runroom.ps1,
# runpublic.ps1, runmatch.ps1, through localstack.ps1), dot-sourced by them; the
# Python runners follow it through sfvelib.verdict, tools/compare.py by its exit
# code. PASS needs evidence: at least one checkpoint compared on both copies and
# no difference, no desync line, no failure. Anything else is FAIL, and a runner
# ends with a nonzero exit code after its cleanup:
#   $passed = Write-Verdict ...   (or Complete-Run in localstack.ps1)
#   ... cleanup ...
#   exit $(if ($passed) { 0 } else { 1 })

# The logs' evidence is read the way tools/sfvelib.py reads it (parse_checkpoints,
# DESYNC_PATTERN, compare_checkpoints): the same checkpoint line, the same key
# (game and frame), the same desync lines. Change one, change the other.
#
# A desync's lines ("desync: checkpoint F is H here and H at the other player",
# its region lines, a load "mismatch"), but not the report writer's own
# "desync report:" lines and not a start state's repair (its report is filed as
# a desync's is; the match then plays from the same state). Select-String is
# case-insensitive, as sfvelib.DESYNC_PATTERN's (?i).
$DesyncPattern = '^(?!.*desync report)(?!.*start state).*(desync|mismatch)'

# Copy log $log's desync lines (Select-String matches); none for a log that was never written.
function Get-DesyncLines([string]$log) {
	if (Test-Path $log) { @(Select-String -Path $log -Pattern $DesyncPattern) } else { @() }
}

# A control-pipe command that must be taken: the caller's Ctl (copy index, command
# line) answered, and the answer must start with "ok". A refusal, an error or no
# answer stops the run with the command in the message, so a pad press, a pick or
# a delay that was refused is never counted as done. Returns the reply.
function CtlOk([int]$i, [string]$line) {
	$reply = Ctl $i $line
	if ("$reply" -notlike 'ok*') { throw "copy ${i}: '$line' was refused: $reply" }
	"$reply"
}

# Copy log $log's checkpoint hashes by game and frame ("<game>/<frame>"): a
# frame far below the last one starts the next game. A log that was never
# written has none.
function Get-Checkpoints([string]$log) {
	$hashes = @{}; $game = 0; $previous = -1
	if (!(Test-Path $log)) { return $hashes }
	foreach ($m in Select-String -Path $log -Pattern 'checkpoint: frame (\d+) hash ([0-9a-f]+)') {
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
