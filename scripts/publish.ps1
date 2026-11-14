#Requires -Version 7
# Publishes an SFV Ember release in stages (docs/release/PUBLISHING.md). Runs on the desktop; the Windows build runs
# on the build PC over SSH, the Linux room hosts in its WSL, and the VPS only gets files (the owner runs sudo).
#
#   scripts\publish.ps1 -Stage plan    -Label 0.1.0-beta.9 -Source b9/integrate -Notes <notes.md> [-DiscordDraft <file>]
#   scripts\publish.ps1 -Stage build   -Label 0.1.0-beta.9
#   scripts\publish.ps1 -Stage rooms   -Label 0.1.0-beta.9 [-AdoptKit <kit folder> -KitSource <sfv commit> -KitLibrary <library commit>]
#   scripts\publish.ps1 -Stage draft   -Label 0.1.0-beta.9
#   scripts\publish.ps1 -Stage promote -Label 0.1.0-beta.9 [-RoomsConfirmed]
#   scripts\publish.ps1 -Stage status  -Label 0.1.0-beta.9
#
# Every stage takes -DryRun (every check, no change anywhere), -Rules <file>[,<file>] (else the config's), and is
# safe to run again: a finished stage is checked and left alone, an unfinished one resumes. -Redo supersedes a stage
# and every later one; -NewAttempt starts a fresh build PC attempt; -Skip <step>[,<step>] skips outward steps
# (vps-stage, draft, push-library, push-origin, push-public, publish). State: <StateRoot>\<label>\, outside the
# repository; machine paths come from <StateRoot>\config.json.
param(
    [Parameter(Mandatory = $true)][ValidateSet('plan', 'build', 'rooms', 'draft', 'promote', 'status')][string]$Stage,
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$Source = '', [string]$Library = '', [string]$Notes = '', [string]$DiscordDraft = '',
    [string]$AdoptKit = '', [string]$KitSource = '', [string]$KitLibrary = '',
    [string[]]$Rules = @(), [string[]]$AllowIdentity = @(),
    [string]$StateRoot = 'D:\sfv-ember-dist\publish', [string]$ConfigPath = '',
    [ValidateSet('vps-stage', 'draft', 'push-library', 'push-origin', 'push-public', 'publish')][string[]]$Skip = @(),
    [ValidateSet('', 'wsl', 'server1')][string]$LinuxBackend = '',
    [switch]$DryRun, [switch]$Redo, [switch]$Replan, [switch]$NewAttempt, [switch]$Force, [switch]$RoomsConfirmed
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
. (Join-Path $PSScriptRoot 'publish\SfvPublish.ps1')
. (Join-Path $PSScriptRoot 'publish\SfvPublishOperations.ps1')

$null = ConvertFrom-PubLabel $Label
$pipelineRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$StateRoot = [IO.Path]::GetFullPath($StateRoot)
if ($StateRoot.StartsWith($pipelineRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'The state folder must be outside the repository.' }
if (!$ConfigPath) { $ConfigPath = Join-Path $StateRoot 'config.json' }
if (!(Test-Path -LiteralPath $ConfigPath)) { throw "No pipeline config at $ConfigPath (docs/release/PUBLISHING.md shows one)." }
$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
foreach ($key in @('publicRepo', 'publicRemote', 'originRemote', 'libraryRepo', 'libraryRemote', 'libraryBranch')) { if (!$config.$key) { throw "config.json has no $key." } }
if (!$Rules.Count) { $Rules = @($config.rules) }
$root = Join-Path $StateRoot $Label
New-Item -ItemType Directory -Path $root -Force | Out-Null

$ctx = [pscustomobject]@{
    Label = $Label; Root = $root; Config = $config; Ops = (New-PubOperations); DryRun = [bool]$DryRun; Skip = @($Skip)
    Repo = $pipelineRoot; PipelineRoot = $pipelineRoot; LibraryRepo = $config.libraryRepo
    AgentPath = (Join-Path $PSScriptRoot 'publish\laptop-agent.ps1'); RoomScriptPath = (Join-Path $PSScriptRoot 'publish\build-rooms-linux.sh')
    VpsStagePath = (Join-Path $PSScriptRoot 'publish\vps.sh')
}
$parameters = @{Source = $Source; Library = $Library; Notes = $Notes; Rules = $Rules; DiscordDraft = $DiscordDraft; AllowIdentity = $AllowIdentity
    Replan = [bool]$Replan; Redo = [bool]$Redo; NewAttempt = [bool]$NewAttempt; Force = [bool]$Force; LinuxBackend = $LinuxBackend; RoomsConfirmed = [bool]$RoomsConfirmed
    AdoptKit = $AdoptKit; KitSource = $KitSource; KitLibrary = $KitLibrary}

# One run per label at a time.
$lock = $null
try {
    if ($Stage -cne 'status') {
        try { $lock = [IO.File]::Open((Join-Path $root 'publish.lock'), 'OpenOrCreate', 'ReadWrite', 'None') }
        catch { throw "Another publish run holds $root\publish.lock." }
        Write-PubLog $ctx "== $Stage $Label$(if ($DryRun) { ' (dry run)' })$(if ($Skip.Count) { " skip: $($Skip -join ',')" })"
    }
    switch ($Stage) {
        'plan' { $null = Invoke-PubPlan $ctx $parameters }
        'build' { Invoke-PubBuild $ctx $parameters }
        'rooms' { Invoke-PubRooms $ctx $parameters }
        'draft' { Invoke-PubDraft $ctx $parameters }
        'promote' { Invoke-PubPromote $ctx $parameters }
        'status' { Show-PubStatus $ctx }
    }
} catch {
    if ($Stage -cne 'status') { Write-PubLog $ctx "FAILED $Stage`: $($_.Exception.Message)" }
    throw
} finally {
    if ($lock) { $lock.Dispose() }
}
