# Configures, builds and tests SFV Ember (x64).
#
# Build tree and vcpkg packages, build trees, downloads and binary cache live
# on D: (the maintainer's C: must stay above 100 GB free); the script stops before
# building when C: is under 120 GB.
#
# -EmberRoot (or SFVE_EMBER_ROOT) names the Ember library checkout SFV Ember
# builds on (docs/design/EMBER_LIBRARY.md section 4); its overlay triplet gives
# spdlog the wide file names the library's log sinks open.

param(
	[string]$BuildDir = 'D:\sfv-ember-build',
	[string]$EmberRoot = $env:SFVE_EMBER_ROOT,
	[string]$VcpkgCache = 'D:\sf4-build\vcpkg-cache',
	[string]$VcpkgRoot = '',
	[string]$VisualStudioPath = '',
	[ValidateSet('RelWithDebInfo', 'Debug', 'Release')][string]$Config = 'RelWithDebInfo',
	[switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$freeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
if ($freeGb -lt 120) { throw "C: has $freeGb GB free; stopping before the build (floor 120 GB)" }

if (!$EmberRoot -or !(Test-Path (Join-Path $EmberRoot 'cmake\ember-x64-probe\CMakeLists.txt'))) {
	throw "Name the Ember library checkout with -EmberRoot or SFVE_EMBER_ROOT (got '$EmberRoot')"
}
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

# vcpkg keeps its build trees, packages and downloads beside itself and its
# binary cache under the user profile unless told otherwise: both on C:.
foreach ($folder in 'archives', 'buildtrees', 'packages', 'downloads') {
	New-Item -ItemType Directory -Force (Join-Path $VcpkgCache $folder) | Out-Null
}
$env:VCPKG_BINARY_SOURCES = "clear;files,$VcpkgCache\archives,readwrite"
$vcpkgOptions = "--x-buildtrees-root=$VcpkgCache\buildtrees;--x-packages-root=$VcpkgCache\packages;--downloads-root=$VcpkgCache\downloads"
$triplet = 'x64-windows-static-md-wchar-filenames'
& $cmake -S $repo -B $BuildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Config" `
	"-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
	"-DVCPKG_TARGET_TRIPLET=$triplet" "-DVCPKG_HOST_TRIPLET=$triplet" `
	"-DVCPKG_OVERLAY_PORTS=$repo\vcpkg-overlays\ports" "-DVCPKG_OVERLAY_TRIPLETS=$EmberRoot\vcpkg-overlays\triplets" `
	"-DVCPKG_INSTALL_OPTIONS=$vcpkgOptions" "-DVCPKG_INSTALLED_DIR=$BuildDir\vcpkg_installed" `
	"-DSFVE_EMBER_SOURCE_ROOT=$EmberRoot"
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
& $cmake --build $BuildDir
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
if (!$SkipTests) {
	& $ctest --test-dir $BuildDir --output-on-failure
	if ($LASTEXITCODE) { throw "tests failed ($LASTEXITCODE)" }
}
