# Configures, builds and tests SFV Ember (x64).
#
# Build tree and vcpkg packages, build trees, downloads and binary cache live
# on D: (the maintainer's C: must stay above 100 GB free); the script stops before
# building when C: is under 120 GB.
#
# -EmberRoot (or SFVE_EMBER_ROOT) names the Ember library checkout SFV Ember
# builds on (docs/design/EMBER_LIBRARY.md section 4); its overlay triplet gives
# spdlog the wide file names the library's log sinks open.
#
# Two kinds of build (src/mod/BuildKind.hxx; EMBER_LIBRARY.md section 41):
#   - by default a development build, what the test tools run: Launcher.exe's
#     --instance and --game-option, the mod's -sfve_* test options and the
#     control pipe's commands all work;
#   - with -Player a player build, which takes none of them: the only kind
#     scripts\package.ps1 packages. Give it a -BuildDir of its own (for
#     example D:\sfv-ember-build-player), so the tools' tree stays a
#     development one.
# The receipt records which kind it is (buildKind).

param(
	[string]$BuildDir = 'D:\sfv-ember-build',
	[string]$EmberRoot = $env:SFVE_EMBER_ROOT,
	[string]$VcpkgCache = 'D:\sf4-build\vcpkg-cache',
	[string]$VcpkgRoot = '',
	[string]$VisualStudioPath = '',
	# The pinned Discord SDK archive (the Ember library's cmake/discord-sdk-pin.json):
	# with it the build makes the Discord companion, ember-discord.exe.
	[string]$DiscordSdkArchive = $env:SF4E_DISCORD_SDK_ARCHIVE,
	[ValidateSet('RelWithDebInfo', 'Debug', 'Release')][string]$Config = 'RelWithDebInfo',
	# A player build, for a package: no test switches.
	[switch]$Player,
	[switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# Every place this build writes to is checked before anything is created: never C:.
. (Join-Path $PSScriptRoot 'BuildPaths.ps1')
$BuildDir = Resolve-BuildPath $BuildDir '-BuildDir'
$VcpkgCache = Resolve-BuildPath $VcpkgCache '-VcpkgCache'

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
	"-DSFVE_EMBER_SOURCE_ROOT=$EmberRoot" "-DSFVE_DISCORD_SDK_ARCHIVE=$DiscordSdkArchive" `
	"-DSFVE_DEV_BUILD=$(if ($Player) { 'OFF' } else { 'ON' })"
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
& $cmake --build $BuildDir
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
if (!$SkipTests) {
	& $ctest --test-dir $BuildDir --output-on-failure
	if ($LASTEXITCODE) { throw "tests failed ($LASTEXITCODE)" }
}

# The build's receipt (build-provenance.json in the build tree), which
# scripts/package.ps1 checks and the package carries: both checkouts that
# made it, at which commits and with which source fingerprints (the
# library's Get-SourceFingerprint: every tracked and untracked file's hash),
# the binaries it made and whether its tests passed. A build without its
# tests gets one too, marked so, and package.ps1 refuses it.
. (Join-Path $EmberRoot 'scripts\BuildProvenance.ps1')
$EmberRoot = (Resolve-Path -LiteralPath $EmberRoot).Path
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
# Whether this build made the Discord companion: the configured build's own
# answer (CMakeLists.txt makes ember-discord only with an SDK archive), never
# whether an ember-discord.exe from an earlier build is lying in the tree.
$discordCompanion = [bool](Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^SFVE_DISCORD_SDK_ARCHIVE:[A-Z]+=.+$' -Quiet)
# The companion's whole runtime goes in the receipt: its exe, the SDK's DLL it
# loads and the record of what it was built with.
$binaryNames = @('Launcher.exe', 'Updater.exe', 'sfv_ember.dll') +
	@(if ($discordCompanion) { 'ember-discord.exe', 'discord_partner_sdk.dll', 'discord-build.json' })
$receipt = [ordered]@{
	schema = 1
	product = 'SFV Ember'
	# SFVE_VERSION as CMakeLists.txt makes it: the project's version and its suffix.
	version = '{0}-{1}' -f (Select-String -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Pattern '^project\(sfv_ember VERSION ([0-9.]+)').Matches[0].Groups[1].Value,
		(Select-String -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Pattern '^set\(SFVE_VERSION "\$\{PROJECT_VERSION\}-([^"]+)"\)').Matches[0].Groups[1].Value
	sourceRoot = $repo
	sourceRevision = (& git -C $repo rev-parse HEAD)
	sourceFingerprint = Get-SourceFingerprint $repo
	emberRoot = $EmberRoot
	emberRevision = (& git -C $EmberRoot rev-parse HEAD)
	emberFingerprint = Get-SourceFingerprint $EmberRoot
	buildRoot = $BuildDir
	config = $Config
	# The configured build's own answer, as discordCompanion below.
	buildKind = if (Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^SFVE_DEV_BUILD:BOOL=OFF$' -Quiet) { 'player' } else { 'development' }
	testsPassed = !$SkipTests
	tests = @(if (!$SkipTests) { & $ctest --test-dir $BuildDir -N | Select-String '^\s*Test\s+#\d+: (\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value } })
	discordCompanion = $discordCompanion
	binaries = @(foreach ($name in $binaryNames) {
		[ordered]@{ path = $name; sha256 = (Get-FileHash -LiteralPath (Join-Path $BuildDir $name) -Algorithm SHA256).Hash }
	})
	builtAt = (Get-Date).ToUniversalTime().ToString('o')
}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $BuildDir 'build-provenance.json') -Encoding UTF8
Write-Output "build receipt: $(Join-Path $BuildDir 'build-provenance.json') (tests $(if ($SkipTests) { 'skipped' } else { 'passed' }))"
