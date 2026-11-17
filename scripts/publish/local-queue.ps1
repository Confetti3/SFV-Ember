#Requires -Version 7
# The build PC's job queue when the build PC is the desktop itself (docs/release/PUBLISHING.md, "The build PC").
# Copied into the config's interactive folder as sfv-start.ps1, beside a file named local-queue that holds the
# Discord SDK archive's path (SF4E_DISCORD_SDK_ARCHIVE, which the player build needs); the
# pipeline calls it as it called the laptop's: `sfv-start.ps1 -Job <name>` starts <name>.cmd and prints STARTED,
# or WAIT while another heavy build runs. The job runs detached (it outlives this call), at below-normal
# priority, holding the heavy-build lock that the desktop's other builds take (one heavy build at a time).
param([Parameter(Mandatory = $true)][string]$Job)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$cmd = Join-Path $here "$Job.cmd"
if (!(Test-Path -LiteralPath $cmd)) { Write-Output "ERROR no job $Job"; exit 0 }
$pidFile = Join-Path $here 'local-queue.pid'
if ((Test-Path -LiteralPath $pidFile) -and (Get-Process -Id ([int](Get-Content -LiteralPath $pidFile -Raw)) -ErrorAction SilentlyContinue)) {
    $running = Join-Path $here 'job.cmd'
    Write-Output "WAIT a job runs ($(if (Test-Path -LiteralPath $running) { Get-Content -LiteralPath $running -TotalCount 1 }))"; exit 0
}
$lock = 'D:\sf4-build\heavy-build.lock'
if (Test-Path -LiteralPath $lock) { Write-Output "WAIT another heavy build holds $lock"; exit 0 }
$sdk = (Get-Content -LiteralPath (Join-Path $here 'local-queue') -Raw).Trim()
if (!$sdk -or !(Test-Path -LiteralPath $sdk)) { Write-Output "ERROR the Discord SDK archive named in local-queue is missing: $sdk"; exit 0 }
# job.cmd names the job now running (the agent's poll reads it).
Copy-Item -LiteralPath $cmd -Destination (Join-Path $here 'job.cmd') -Force
$runner = Join-Path $here 'job-runner.ps1'
@"
`$ErrorActionPreference = 'Continue'
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
New-Item -ItemType File -Path '$lock' -Force | Out-Null
try {
    `$env:CARGO_BUILD_JOBS = '6'
    `$env:SF4E_DISCORD_SDK_ARCHIVE = '$sdk'
    & cmd.exe /c '$(Join-Path $here 'job.cmd')'
} finally { Remove-Item -LiteralPath '$lock' -Force -ErrorAction SilentlyContinue }
"@ | Set-Content -LiteralPath $runner -Encoding utf8
$process = Start-Process -FilePath "$env:ProgramFiles\PowerShell\7\pwsh.exe" -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runner) -WindowStyle Hidden -PassThru
Set-Content -LiteralPath $pidFile -Value $process.Id -NoNewline
Write-Output "STARTED $Job (pid $($process.Id))"
