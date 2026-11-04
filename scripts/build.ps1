# Configures, builds and tests SFV Ember (x64).
#
# Build tree and vcpkg packages live on D: (the maintainer's C: must stay above 100 GB
# free); the script stops before building when C: is under 120 GB.

param(
	[string]$BuildDir = 'D:\sfv-ember-build',
	[string]$VcpkgRoot = '',
	[string]$VisualStudioPath = '',
	[ValidateSet('RelWithDebInfo', 'Debug', 'Release')][string]$Config = 'RelWithDebInfo',
	[switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$freeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
if ($freeGb -lt 120) { throw "C: has $freeGb GB free; stopping before the build (floor 120 GB)" }

if (!$VcpkgRoot) { $VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path (Split-Path -Parent $repo) 'vcpkg' } }
if (!(Test-Path "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake")) { throw "vcpkg not found at $VcpkgRoot" }
if (!$VisualStudioPath) { $VisualStudioPath = $env:SF4E_VISUAL_STUDIO_PATH }
if (!$VisualStudioPath) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
	$VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vcvars = Join-Path $VisualStudioPath 'VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path $vcvars)) { throw "vcvars64.bat not found under $VisualStudioPath" }

# Import the x64 developer environment into this PowerShell session.
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
	if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
}
$cmakeDir = Join-Path $VisualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $cmakeDir 'CMake\bin\cmake.exe'
$ctest = Join-Path $cmakeDir 'CMake\bin\ctest.exe'
$ninja = Join-Path $cmakeDir 'Ninja\ninja.exe'

& $cmake -S $repo -B $BuildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Config" `
	"-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
	-DVCPKG_TARGET_TRIPLET=x64-windows-static-md "-DVCPKG_OVERLAY_PORTS=$repo\vcpkg-overlays\ports" `
	"-DVCPKG_INSTALLED_DIR=$BuildDir\vcpkg_installed"
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
& $cmake --build $BuildDir
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
if (!$SkipTests) {
	& $ctest --test-dir $BuildDir --output-on-failure
	if ($LASTEXITCODE) { throw "tests failed ($LASTEXITCODE)" }
}
