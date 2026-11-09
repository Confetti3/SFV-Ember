# Builds Ember's network helper (sf4-net.exe) from an Ember checkout for SFV
# Ember. The helper is Ember's own, unchanged: SFV Ember runs it the way the
# SF4 launcher does and talks to it over its named pipe.
#
# Output: $BuildDir\sf4-net.exe (tools/sfve.py deploy copies it next to the mod;
# scripts/package.ps1 packages it) and $BuildDir\sf4-net.provenance.json: the
# checkout, its commit and source fingerprint, its Cargo.lock and the binary's
# hash, which scripts/package.ps1 checks against the build's receipt.
#
# -EmberRepo defaults to SFVE_EMBER_ROOT, the library checkout scripts/build.ps1
# builds against, then to sf4-current beside this checkout. cargo runs with
# -j 4 unless told otherwise: one heavy build at a time on this PC.
#
# -Features selftest builds the helper that takes its Ember folder from
# SF4E_SELFTEST_EMBER_DIR (the library's docs/design/TOURNAMENT_SELFTEST.md),
# so two copies on one PC have Ember IDs of their own and neither touches the
# player's: tools/runpublic.ps1 needs it. Build without it afterwards.

param(
	[string]$EmberRepo = $env:SFVE_EMBER_ROOT,
	[string]$BuildDir = 'D:\sfv-ember-build',
	[string]$VisualStudioPath = '',
	[int]$Jobs = 4,
	[string]$Features = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
# The build tree is checked before anything is created: never C:.
. (Join-Path $PSScriptRoot 'BuildPaths.ps1')
$BuildDir = Resolve-BuildPath $BuildDir '-BuildDir'
if (!$EmberRepo) { $EmberRepo = Join-Path (Split-Path -Parent $repo) 'sf4-current' }
$EmberRepo = (Resolve-Path -LiteralPath $EmberRepo).Path
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
cargo build --release --locked --manifest-path $manifest -j $Jobs @featureArgs
if ($LASTEXITCODE) { throw "cargo build failed ($LASTEXITCODE)" }
$helper = Join-Path $BuildDir 'sf4-net.exe'
Copy-Item (Join-Path $env:CARGO_TARGET_DIR 'release\sf4-net.exe') $helper -Force

. (Join-Path $EmberRepo 'scripts\BuildProvenance.ps1')
[ordered]@{
	schema = 1
	emberRoot = $EmberRepo
	emberRevision = (& git -C $EmberRepo rev-parse HEAD)
	emberFingerprint = Get-SourceFingerprint $EmberRepo
	cargoLockSha256 = (Get-FileHash -LiteralPath (Join-Path $EmberRepo 'rust\sf4-net\Cargo.lock') -Algorithm SHA256).Hash
	sha256 = (Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash
	builtAt = (Get-Date).ToUniversalTime().ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BuildDir 'sf4-net.provenance.json') -Encoding UTF8
$commit = git -C $EmberRepo rev-parse --short HEAD
Write-Output "built sf4-net.exe from $EmberRepo at $commit$(if ($Features) { " with $Features" })"
