#Requires -Version 7
# The real commands of the publish pipeline: every process it starts goes through New-PubOperations' table, which
# tests/PublishTests.ps1 replaces. Git refuses `push` here: pushes are their own operation (Push), reached only
# through Invoke-PubOutward.

# Runs a program with exact bytes in and out, a timeout, and no shell in between.
# Its output streams are drained for at most $DrainSeconds after it exits (a child it left behind may hold them open).
function Invoke-PubProcess([string]$FilePath, [string[]]$Arguments, [byte[]]$Stdin = $null, [int]$TimeoutSeconds = 600, [int]$DrainSeconds = 30) {
    $info = [Diagnostics.ProcessStartInfo]::new($FilePath)
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true; $info.RedirectStandardInput = $true
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($info)
    $out = [IO.MemoryStream]::new()
    $copy = $process.StandardOutput.BaseStream.CopyToAsync($out)
    $err = $process.StandardError.ReadToEndAsync()
    try {
        if ($Stdin) { $process.StandardInput.BaseStream.Write($Stdin, 0, $Stdin.Length) }
        $process.StandardInput.Close()
    } catch { }
    if (!$process.WaitForExit([Math]::Max(1, $TimeoutSeconds) * 1000)) {
        try { $process.Kill($true) } catch { }
        throw "$FilePath timed out after $TimeoutSeconds s ($($Arguments[0..([Math]::Min(2, $Arguments.Count - 1))] -join ' '))"
    }
    $process.WaitForExit()
    if (!$copy.Wait($DrainSeconds * 1000) -or !$err.Wait($DrainSeconds * 1000)) {
        throw "$FilePath exited, but its output was still open $DrainSeconds s later (a process it started holds it)"
    }
    [pscustomobject]@{exitCode = $process.ExitCode; bytes = $out.ToArray(); stderr = $err.Result}
}

function New-PubOperations {
    @{
        Git = {
            param([string]$Repo, [string[]]$Arguments, [byte[]]$Stdin)
            if ('push' -cin $Arguments) { throw 'git push goes through the Push operation only.' }
            Invoke-PubProcess 'git' (@('-C', $Repo) + $Arguments) $Stdin 900
        }
        # Never a tag along with the branch; -DryRun asks the remote what the push would do (git push --dry-run).
        Push = {
            param([string]$Repo, [string]$Remote, [string]$Commit, [string]$Ref, [bool]$DryRun)
            Invoke-PubProcess 'git' (@('-C', $Repo, 'push', '--porcelain', '--no-follow-tags') + @(if ($DryRun) { '--dry-run' }) + @($Remote, "${Commit}:$Ref")) $null 600
        }
        Gh = { param([string[]]$Arguments) Invoke-PubProcess 'gh' $Arguments $null 1800 }
        Ssh = {
            param([string]$HostAlias, [string]$Command, [byte[]]$Stdin, [int]$TimeoutSeconds)
            Invoke-PubProcess 'ssh' @('-o', 'ConnectTimeout=15', '-o', 'BatchMode=yes', '-o', 'ServerAliveInterval=30', $HostAlias, $Command) $Stdin $TimeoutSeconds
        }
        Scp = {
            param([string]$From, [string]$To, [int]$TimeoutSeconds)
            Invoke-PubProcess 'scp' @('-q', '-o', 'ConnectTimeout=15', '-o', 'BatchMode=yes', $From, $To) $null $TimeoutSeconds
        }
        Tar = { param([string]$Parent, [string]$Name, [string]$Out) Invoke-PubProcess 'tar' @('-cf', $Out, '-C', $Parent, $Name) $null 300 }
        Sleep = { param([int]$Seconds) Start-Sleep -Seconds $Seconds }
        Now = { [DateTime]::UtcNow }
    }
}
