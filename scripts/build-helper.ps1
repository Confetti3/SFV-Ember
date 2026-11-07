# Builds Ember's network helper (sf4-net.exe) from an Ember checkout for SFV
# Ember. The helper is Ember's own, unchanged: SFV Ember runs it the way the
# SF4 launcher does and talks to it over its named pipe.
#
# Output: $BuildDir\sf4-net.exe (tools/sfve.py deploy copies it next to the mod).
#
# -Features selftest builds the helper that takes its Ember folder from
# SF4E_SELFTEST_EMBER_DIR (the library's docs/design/TOURNAMENT_SELFTEST.md),
# so two copies on one PC have Ember IDs of their own and neither touches the
# player's: tools/runpublic.ps1 needs it. Build without it afterwards.

param(
	[string]$EmberRepo = '',
	[string]$BuildDir = 'D:\sfv-ember-build',
	[string]$VisualStudioPath = '',
	[string]$Features = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$EmberRepo) { $EmberRepo = Join-Path (Split-Path -Parent $repo) 'sf4-current' }
$manifest = Join-Path $EmberRepo 'rust\sf4-net\Cargo.toml'
if (!(Test-Path $manifest)) { throw "no sf4-net crate at $manifest" }

$freeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
if ($freeGb -lt 120) { throw "C: has $freeGb GB free; stopping before the build (floor 120 GB)" }

# The helper's build script needs the Windows SDK resource compiler.
if (!$VisualStudioPath) { $VisualStudioPath = $env:SF4E_VISUAL_STUDIO_PATH }
if (!$VisualStudioPath) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
	$VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vcvars = Join-Path $VisualStudioPath 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
	if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
}

$env:CARGO_TARGET_DIR = Join-Path $BuildDir 'sf4-net-target'
$featureArgs = if ($Features) { @('--features', $Features) } else { @() }
cargo build --release --locked --manifest-path $manifest -j 4 @featureArgs
if ($LASTEXITCODE) { throw "cargo build failed ($LASTEXITCODE)" }
Copy-Item (Join-Path $env:CARGO_TARGET_DIR 'release\sf4-net.exe') (Join-Path $BuildDir 'sf4-net.exe') -Force
$commit = git -C $EmberRepo rev-parse --short HEAD
Write-Output "built sf4-net.exe from $EmberRepo at $commit$(if ($Features) { " with $Features" })"
