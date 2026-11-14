#Requires -Version 7
# Tests for the review of the publish pipeline (commit 75138f5): one per finding, each written to fail on the code
# before its fix. Same fixture and fakes as tests/PublishTests.ps1; nothing leaves this PC.
#   pwsh -NoProfile -File tests\PublishReviewTests.ps1 [-Only <name>[,<name>]] [-ScriptsRoot <scripts\publish folder>]
param([string[]]$Only = @(), [string]$ScriptsRoot = '')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$scripts = if ($ScriptsRoot) { $ScriptsRoot } else { Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\publish' }
. (Join-Path $scripts 'SfvPublish.ps1')
. (Join-Path $scripts 'SfvPublishOperations.ps1')
. (Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\publish') 'SfvPublishFakes.ps1')
. (Join-Path $PSScriptRoot 'PublishTestFixture.ps1')

$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$tests = [ordered]@{}

# 1. The Linux script's counting and status, run in bash (WSL): a clean pair ends with STATUS=ok and zero counts.
$tests['shell-count'] = {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ("sfv-shell-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    $fixtures.Add($dir)
    New-Item -ItemType Directory "$dir\in" | Out-Null
    $build = 'sfv-ember/13173604/0.7/55c1d32e7a18ea97/layout-d728d987b541ac1b'
    [IO.File]::WriteAllText("$dir\in\sfve-room-host", "`u{7f}ELF clean host $build", [Text.Encoding]::Latin1)
    [IO.File]::WriteAllText("$dir\in\sf4-net", "`u{7f}ELF clean helper", [Text.Encoding]::Latin1)
    $mnt = ConvertTo-PubWslPath $dir
    $script = ConvertTo-PubWslPath (Join-Path $scripts 'build-rooms-linux.sh')
    & wsl -e bash -c "bash '$script' --check '$mnt/in' '$mnt/out' '$build'" *> $null
    $result = Get-Content "$dir\out\result.txt" -Raw
    Check ($result -match '(?m)^STATUS=ok' -and $result -match '(?m)^PATHS_SFVE_ROOM_HOST=0' -and $result -match '(?m)^PATHS_SF4_NET=0') "clean pair: $result"
    Check ($result -match "(?m)^EMBEDDED=$([regex]::Escape($build))") 'embedded build'
    [IO.File]::WriteAllText("$dir\in\sf4-net", "`u{7f}ELF helper built in /home/someone/x", [Text.Encoding]::Latin1)
    & wsl -e bash -c "bash '$script' --check '$mnt/in' '$mnt/out' '$build'" *> $null
    $result = Get-Content "$dir\out\result.txt" -Raw
    Check ($result -match '(?m)^STATUS=ok' -and $result -match '(?m)^PATHS_SF4_NET=[1-9]') "a path is counted: $result"
    Pass 'build-rooms-linux.sh (bash): a clean pair ends with STATUS=ok and zero path counts; a build path is counted'
}

# 2. -Redo build runs a new laptop job in a new folder: the old result never answers for it.
$tests['redo-fresh'] = {
    $x = Fresh; Through $x 'build'
    $x.World.BuildTweak = { param($f) $f.ctestFailed = 1 }
    Reject { Invoke-PubBuild $x.Ctx (@{} + $x.P + @{Redo = $true}) } '*tests did not all run and pass*'
    $x = Fresh; $null = Plan $x; $x.World.LaptopBusy = 'another job'
    Reject { Invoke-PubBuild $x.Ctx $x.P } '*one heavy build at a time*'
    Check (!(Test-Path "$($x.Ctx.Root)\build\attempt.json")) 'no attempt recorded for a job never started'
    $x.World.LaptopBusy = ''
    Invoke-Quiet { Invoke-PubBuild $x.Ctx $x.P }
    Check (Test-Path "$($x.Ctx.Root)\build\evidence.json") 'built once the laptop is free'
    Pass 'build: -Redo runs a fresh job in a fresh folder; a busy laptop leaves no attempt to wait on'
}

# 3. Skipping the library push stops the public main and the release.
$tests['skip-library'] = {
    $x = Fresh; Through $x 'draft'; $x.Ctx.Skip = @('push-library')
    Reject { Invoke-PubPromote $x.Ctx $x.P 6>$null } '*does not contain*which the README pins*'
    Check (!@($x.World.Pushes | Where-Object { $_ -like 'public *' }).Count -and $x.World.Patches -eq 0) 'nothing public'
    Pass 'promote: with push-library skipped, neither the public main nor the release goes out'
}

# 4. A publish whose reply was lost resumes.
$tests['publish-reply-lost'] = {
    $x = Fresh; Through $x 'draft'; $x.World.PatchThenFail = $true
    Reject { Invoke-PubPromote $x.Ctx $x.P 6>$null } '*failed*'
    $x.World.PatchThenFail = $false
    Invoke-Quiet { Invoke-PubPromote $x.Ctx $x.P }
    Check ((Test-Path "$($x.Ctx.Root)\promote\evidence.json") -and $x.World.Patches -eq 1) 'resumed without a second PATCH'
    Pass 'promote: a release published by a run whose reply was lost is recognised and confirmed'
}

# 5. The server gate is asked on every run until the release is published.
$tests['server-every-run'] = {
    $x = Fresh; Through $x 'draft'; $x.Ctx.Skip = @('publish')
    Invoke-Quiet { Invoke-PubPromote $x.Ctx $x.P }
    $x.Ctx.Skip = @(); $x.World.VpsCheck = 'not-installed'
    Reject { Invoke-PubPromote $x.Ctx $x.P 6>$null } '*does not offer*'
    Check ($x.World.Patches -eq 0) 'not published'
    Pass 'promote: the server check runs again on every run before publishing'
}

# 6. The draft and the tag are read again just before publishing.
$tests['recheck-before-publish'] = {
    $x = Fresh; Through $x 'draft'
    $id = (Read-PubJson "$($x.Ctx.Root)\draft\evidence.json").facts.releaseId
    $script:target = Get-FakeRelease $x.World $id
    $x.World.OnPush = { param($Remote, $Ref) if ($Remote -ceq 'public') { $script:target.body = 'changed while the pushes ran' } }
    Reject { Invoke-PubPromote $x.Ctx $x.P 6>$null } '*notes are not the planned notes*'
    Check ($x.World.Patches -eq 0) 'not published'
    Pass 'promote: the draft is read again by id right before publishing'
}

# 7. A hit never prints the forbidden text, and a bad rule regex is reported by number.
$tests['hits-no-text'] = {
    $x = Fresh; Write-File "$($x.Sfv)\docs\SecretWord.md" 'x'; $null = Commit $x.Sfv 'doc'
    try { Plan $x | Out-Null; throw 'no refusal' } catch { Check ($_.Exception.Message -like '*forbidden*' -and $_.Exception.Message -notlike '*SecretWord*') "repository hit text: $($_.Exception.Message)" }
    $x = Fresh; $null = Plan $x; $x.World.BuildTweak = { param($f) $f.extraName = 'SecretWord.txt' }
    try { Invoke-PubBuild $x.Ctx $x.P 6>$null; throw 'no refusal' } catch { Check ($_.Exception.Message -like '*forbidden*' -and $_.Exception.Message -notlike '*SecretWord*') "package hit text: $($_.Exception.Message)" }
    $x = Fresh; Write-File "$($x.F)\rules.txt" "regex:(Unclosed==>x`n"
    try { Plan $x | Out-Null; throw 'no refusal' } catch { Check ($_.Exception.Message -like '*rules.txt#1*' -and $_.Exception.Message -notlike '*Unclosed*') "regex error text: $($_.Exception.Message)" }
    Pass 'scan: repository and package hits name objects and entries, never the text; a bad pattern is reported by number'
}

# 8. The stages run only the pipeline commit the plan recorded.
$tests['pipeline-pin'] = {
    $x = Fresh; $null = Plan $x
    Write-File "$($x.Sfv)\scripts\more.ps1" '# pipeline change'; $null = Commit $x.Sfv 'pipeline change'
    Reject { Invoke-PubBuild $x.Ctx $x.P } '*plan was made by the pipeline at*'
    $x = Fresh; $null = Plan $x; Write-File "$($x.Sfv)\scripts\dirty.ps1" 'x'
    Reject { Invoke-PubBuild $x.Ctx $x.P } '*uncommitted changes*'
    Pass 'stages: refuse a pipeline checkout that moved or changed since the plan'
}

# 9. The remotes push where the config says.
$tests['remote-urls'] = {
    $x = Fresh; G $x.Sfv @('remote', 'set-url', '--push', 'public', 'https://github.com/Someone/SFV-Ember.git') | Out-Null
    Reject { Plan $x } '*not to github.com/Owner/SFV-Ember*'
    $x = Fresh; G $x.Sfv @('remote', 'set-url', '--push', 'origin', 'git@github.com:Owner/SFV-Ember.git') | Out-Null
    Reject { Plan $x } '*private remote origin pushes to the public repository*'
    $x = Fresh; Through $x 'draft'; G $x.Lib @('remote', 'set-url', '--push', 'origin', 'https://github.com/Owner/Other.git') | Out-Null
    Reject { Invoke-PubPromote $x.Ctx $x.P 6>$null } '*not to github.com/Owner/Library*'
    Check (!$x.World.Pushes.Count) 'nothing pushed'
    Pass 'remotes: plan and promote refuse push URLs other than the configured repositories'
}

# 10. Pushes never carry tags.
$tests['no-follow-tags'] = {
    Check ((New-PubOperations).Push.ToString() -match '--no-follow-tags') 'the push operation passes --no-follow-tags'
    Pass 'push: --no-follow-tags on every push'
}

# 11. Machine names in any case; rules files with NUL bytes or bad UTF-8 are refused.
$tests['rules-and-names'] = {
    $name = @(Get-PubMachineNames | Where-Object { $_.Length -ge 3 })[0]
    $rules = Read-PubRules @((Fresh).F + '\rules.txt') (Get-PubMachineNames)
    Check (@(Find-PubHits $rules ([Text.Encoding]::UTF8.GetBytes("x $($name.ToLowerInvariant()) y")) 't').Count -ge 1) 'lower-case machine name found'
    Check (@(Find-PubHits $rules ([Text.Encoding]::UTF8.GetBytes("x $($name.ToUpperInvariant()) y")) 't').Count -ge 1) 'upper-case machine name found'
    $dir = Join-Path ([IO.Path]::GetTempPath()) ("sfv-rules-" + [Guid]::NewGuid().ToString('N').Substring(0, 8)); $fixtures.Add($dir); New-Item -ItemType Directory $dir | Out-Null
    [IO.File]::WriteAllBytes("$dir\nul.txt", [byte[]](0x6C, 0x69, 0x74, 0x3A, 0x41, 0x00, 0x42, 0x0A))
    Reject { Read-PubRules @("$dir\nul.txt") } '*NUL bytes*'
    [IO.File]::WriteAllBytes("$dir\bad.txt", [byte[]](0x41, 0xFF, 0xFE, 0x42, 0x0A))
    Reject { Read-PubRules @("$dir\bad.txt") } '*not valid UTF-8*'
    Set-Content "$dir\empty.txt" "# nothing`n"
    Reject { Read-PubRules @("$dir\empty.txt") } '*hold no rule*'
    Pass 'rules: machine names match in any case; NUL bytes, bad UTF-8 or no rule refuse the file'
}

# 13. A process that exits while a child it started keeps its output open does not hang the pipeline.
$tests['drain-bound'] = {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $threw = $false
    try { $null = Invoke-PubProcess 'cmd.exe' @('/c', 'start /b ping -n 25 127.0.0.1 >nul & exit 0') $null 60 -DrainSeconds 2 } catch { $threw = $true }
    Check ($threw -and $watch.Elapsed.TotalSeconds -lt 15) "returned after $([int]$watch.Elapsed.TotalSeconds) s, threw: $threw"
    Pass 'process: output still held open after the exit stops the wait within its bound'
}

# The push dry run: a public main that moved since the plan, even to an ancestor of the release, stops the push.
$tests['push-range'] = {
    $x = Fresh; Through $x 'draft'
    $other = Join-Path $x.F 'other2'; G $x.F @('clone', '-q', '-b', 'main', "$($x.F)\public.git", 'other2') | Out-Null
    G $other @('fetch', '-q', $x.Sfv, $x.K) | Out-Null; G $other @('push', '-q', 'origin', "$($x.K):refs/heads/main") | Out-Null
    Reject { Invoke-PubPromote $x.Ctx $x.P 6>$null } '*as when the plan was made*'
    Check (!@($x.World.Pushes | Where-Object { $_ -like 'public *' }).Count) 'nothing pushed to public'
    Pass 'promote: a branch that moved since the plan is not pushed, even when the release would still fast-forward it'
}

$failed = @()
try {
    foreach ($name in $tests.Keys) {
        if ($Only.Count -and $name -notin $Only) { continue }
        try { & $tests[$name] } catch { $failed += $name; Write-Host "FAIL $name`: $($_.Exception.Message)" }
    }
} finally {
    foreach ($f in $fixtures) { if (Test-Path $f) { Get-ChildItem $f -Recurse -Force -File | ForEach-Object { $_.Attributes = 'Normal' }; Remove-Item $f -Recurse -Force -ErrorAction SilentlyContinue } }
}
Write-Host ''
if ($failed.Count) { Write-Host "$($failed.Count) review test(s) failed: $($failed -join ', ')"; exit 1 }
Write-Host "All $script:passed review tests passed."
