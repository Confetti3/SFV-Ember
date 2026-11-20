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
# Two kinds of build (src/mod/BuildKind.hxx; EMBER_LIBRARY.md section 42):
#   - by default a development build, what the test tools run: Launcher.exe's
#     --instance and --game-option, the mod's -sfve_* test options and the
#     control pipe's commands all work;
#   - with -Player a player build, which takes none of them: the only kind
#     scripts\package.ps1 packages. It goes to D:\sfv-ember-build-player
#     unless -BuildDir says otherwise, so the tools' tree stays a
#     development one.
# The receipt records which kind it is (buildKind).
#
# On a Linux build PC (docs/release/PUBLISHING.md, "The build PC") the same
# build is cross-compiled: clang-cl and lld-link against the Microsoft CRT and
# Windows SDK that xwin splats (cmake/cross/clang-cl-x64.cmake; SF4E_XWIN and
# SF4E_LLVM_BIN name them), vcpkg's packages with the same toolchain (the
# library's triplet with it chainloaded, release packages only, Detours from
# cmake/cross/vcpkg-ports), and the tests run through Wine (-TestEmulator).
# -BuildDir and -VcpkgCache are named there, outside the home folder
# (BuildPaths.ps1); objects always carry their debug information (/Z7), as
# with ccache on Windows, and no compiler cache is used. Every receipt's
# toolchain record says which compiler, linker, CRT and SDK made the build.

param(
	# D:\sfv-ember-build by default, D:\sfv-ember-build-player with -Player.
	[string]$BuildDir = '',
	[string]$EmberRoot = $env:SFVE_EMBER_ROOT,
	[string]$VcpkgCache = $(if ($IsWindows) { 'D:\sf4-build\vcpkg-cache' } else { $env:SFVE_VCPKG_CACHE }),
	[string]$VcpkgRoot = '',
	[string]$VisualStudioPath = '',
	# The pinned Discord SDK archive (the Ember library's cmake/discord-sdk-pin.json):
	# with it the build makes the Discord companion, ember-discord.exe.
	[string]$DiscordSdkArchive = $env:SF4E_DISCORD_SDK_ARCHIVE,
	[ValidateSet('RelWithDebInfo', 'Debug', 'Release')][string]$Config = 'RelWithDebInfo',
	# A player build, for a package: no test switches.
	[switch]$Player,
	[switch]$SkipTests,
	# Compiles go through ccache unless this is given (see below).
	[switch]$NoCompilerCache,
	# Linux only: the program that runs a Windows test program (CMAKE_CROSSCOMPILING_EMULATOR; Wine).
	[string]$TestEmulator = $env:SFVE_WINE_EMULATOR
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# Every place this build writes to is checked before anything is created: never C:.
. (Join-Path $PSScriptRoot 'BuildPaths.ps1')
if (!$BuildDir -and !$IsWindows) { throw 'Name the build tree (-BuildDir) on Linux' }
if (!$VcpkgCache -and !$IsWindows) { throw 'Name the vcpkg cache folder (-VcpkgCache or SFVE_VCPKG_CACHE) on Linux' }
if (!$BuildDir) { $BuildDir = if ($Player) { 'D:\sfv-ember-build-player' } else { 'D:\sfv-ember-build' } }
$BuildDir = Resolve-BuildPath $BuildDir '-BuildDir'
$VcpkgCache = Resolve-BuildPath $VcpkgCache '-VcpkgCache'

if ($IsWindows) {
	$freeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
	if ($freeGb -lt 120) { throw "C: has $freeGb GB free; stopping before the build (floor 120 GB)" }
} else {
	# The source paths are compiled into the binaries too.
	Assert-OutsideHome $repo 'This checkout'
	if ($EmberRoot) { Assert-OutsideHome $EmberRoot '-EmberRoot' }
}

if (!$EmberRoot -or !(Test-Path (Join-Path $EmberRoot 'cmake\ember-x64-probe\CMakeLists.txt'))) {
	throw "Name the Ember library checkout with -EmberRoot or SFVE_EMBER_ROOT (got '$EmberRoot')"
}
if (!$VcpkgRoot) { $VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path (Split-Path -Parent $repo) 'vcpkg' } }
if (!(Test-Path "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake")) { throw "vcpkg not found at $VcpkgRoot" }
if (!$IsWindows) {
	# The cross toolchain (cmake/cross/clang-cl-x64.cmake): no vcvars; CMake, CTest and Ninja are the Linux ones
	# on PATH.
	$crossToolchain = Join-Path $repo 'cmake/cross/clang-cl-x64.cmake'
	foreach ($name in 'SF4E_XWIN', 'SF4E_LLVM_BIN') {
		if (!(Test-Path -LiteralPath "$([Environment]::GetEnvironmentVariable($name))" -PathType Container)) { throw "$name must name a folder (the xwin splat, the LLVM tools)" }
	}
	foreach ($tool in 'clang-cl', 'lld-link', 'llvm-lib', 'llvm-rc', 'llvm-mt') {
		if (!(Test-Path -LiteralPath (Join-Path $env:SF4E_LLVM_BIN $tool))) { throw "No $tool in SF4E_LLVM_BIN ($env:SF4E_LLVM_BIN)" }
	}
	if (!$TestEmulator -or !(Get-Command $TestEmulator -ErrorAction SilentlyContinue)) { throw "Name the program that runs a Windows test (-TestEmulator or SFVE_WINE_EMULATOR; got '$TestEmulator')" }
	$cmake = (Get-Command cmake -CommandType Application | Select-Object -First 1).Source
	$ctest = (Get-Command ctest -CommandType Application | Select-Object -First 1).Source
	$ninja = (Get-Command ninja -CommandType Application | Select-Object -First 1).Source
}
if ($IsWindows) {
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
}

# vcpkg keeps its build trees, packages and downloads beside itself and its
# binary cache under the user profile unless told otherwise: both on C:.
foreach ($folder in 'archives', 'buildtrees', 'packages', 'downloads') {
	New-Item -ItemType Directory -Force (Join-Path $VcpkgCache $folder) | Out-Null
}
# (Join-Path writes the separator of the platform; on Windows these are the same strings as before.)
$env:VCPKG_BINARY_SOURCES = "clear;files,$(Join-Path $VcpkgCache 'archives'),readwrite"
$vcpkgOptions = "--x-buildtrees-root=$(Join-Path $VcpkgCache 'buildtrees');--x-packages-root=$(Join-Path $VcpkgCache 'packages');--downloads-root=$(Join-Path $VcpkgCache 'downloads')"

# What the build is made from, taken before it starts and checked again after
# its tests: a tree that changed meanwhile has no receipt (the receipt names
# these, and scripts\package.ps1 trusts that the binaries were built from them).
. (Join-Path $EmberRoot 'scripts\BuildProvenance.ps1')
$EmberRoot = (Resolve-Path -LiteralPath $EmberRoot).Path
$sourceRevision = (& git -C $repo rev-parse HEAD)
$sourceFingerprint = Get-SourceFingerprint $repo
$emberRevision = (& git -C $EmberRoot rev-parse HEAD)
$emberFingerprint = Get-SourceFingerprint $EmberRoot

# Compiles go through ccache, which serves any compile whose inputs it has seen
# before (another build tree, a switched branch, a rebuilt dev or player tree).
# It cannot cache /Zi, so objects carry their debug information (/Z7); the
# linker still writes the PDB. -NoCompilerCache, or no ccache 4.6+ on PATH,
# builds without it; every configure states the choice, so a tree never keeps
# an old one. The receipt records the ccache version a build used.
$ccacheVersion = $null
$ccache = if ($NoCompilerCache -or !$IsWindows) { $null } else { (Get-Command ccache -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source }
if ($ccache) {
	# Its cache lives on D: like every other place this build writes to.
	if (!$env:CCACHE_DIR) { $env:CCACHE_DIR = 'D:\sf4-build\ccache' }
	New-Item -ItemType Directory -Force $env:CCACHE_DIR | Out-Null
	$ccacheVersion =[regex]::Match((& $ccache --version | Select-Object -First 1), '\d+(\.\d+)+').Value
	if (!$ccacheVersion -or [version]$ccacheVersion -lt [version]'4.6') { Write-Warning "ccache '$ccacheVersion' is older than 4.6: building without it."; $ccache = $null; $ccacheVersion = $null }
}
$compilerCacheOptions = if ($ccache) {
	"-DCMAKE_C_COMPILER_LAUNCHER=$ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=$ccache", '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded'
} elseif ($IsWindows) { '-UCMAKE_C_COMPILER_LAUNCHER', '-UCMAKE_CXX_COMPILER_LAUNCHER', '-UCMAKE_MSVC_DEBUG_INFORMATION_FORMAT' }
# Linux: no cache, and the debug information of a ccache build.
else { '-UCMAKE_C_COMPILER_LAUNCHER', '-UCMAKE_CXX_COMPILER_LAUNCHER', '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded' }
Write-Host "Compiler cache: $(if ($ccache) { "ccache $ccacheVersion" } else { 'off' })"

$triplet = 'x64-windows-static-md-wchar-filenames'
if ($IsWindows) {
& $cmake -S $repo -B $BuildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Config" `
	"-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
	"-DVCPKG_TARGET_TRIPLET=$triplet" "-DVCPKG_HOST_TRIPLET=$triplet" `
	"-DVCPKG_OVERLAY_PORTS=$repo\vcpkg-overlays\ports" "-DVCPKG_OVERLAY_TRIPLETS=$EmberRoot\vcpkg-overlays\triplets" `
	"-DVCPKG_INSTALL_OPTIONS=$vcpkgOptions" "-DVCPKG_INSTALLED_DIR=$BuildDir\vcpkg_installed" `
	"-DSFVE_EMBER_SOURCE_ROOT=$EmberRoot" "-DSFVE_DISCORD_SDK_ARCHIVE=$DiscordSdkArchive" `
	"-DSFVE_DEV_BUILD=$(if ($Player) { 'OFF' } else { 'ON' })" @compilerCacheOptions
} else {
	# Import libraries are found case-insensitively on Windows; the splat has them in lower case. Every name the
	# two checkouts spell otherwise (Bcrypt, AdvApi32, ...) gets a link in the splat.
	& python3 (Join-Path $repo 'cmake/cross/case-links.py') $env:SF4E_XWIN $repo $EmberRoot
	if ($LASTEXITCODE) { throw "case-links.py failed ($LASTEXITCODE)" }
	# The library's triplet with the cross toolchain chainloaded into every port build, release packages only
	# (what this build links); vcpkg's own tools are built for Linux (host triplet x64-linux).
	$triplets = Join-Path $BuildDir 'cross-triplets'
	New-Item -ItemType Directory -Force $triplets | Out-Null
	$tripletText = [IO.File]::ReadAllText((Join-Path $EmberRoot "vcpkg-overlays/triplets/$triplet.cmake"))
	$tripletText += "`n# build.ps1 on Linux: clang-cl (cmake/cross/clang-cl-x64.cmake); release packages only.`nset(VCPKG_CHAINLOAD_TOOLCHAIN_FILE `"$crossToolchain`")`nset(VCPKG_LOAD_VCVARS_ENV OFF)`nset(VCPKG_BUILD_TYPE release)`nset(VCPKG_ENV_PASSTHROUGH SF4E_XWIN SF4E_LLVM_BIN)`n"
	$tripletPath = Join-Path $triplets "$triplet.cmake"
	# Rewritten only when it changes: vcpkg hashes the triplet into every package's ABI.
	if (!(Test-Path -LiteralPath $tripletPath) -or [IO.File]::ReadAllText($tripletPath) -cne $tripletText) { [IO.File]::WriteAllText($tripletPath, $tripletText) }
	# Detours's own port builds with nmake, on Windows only: cmake/cross/vcpkg-ports has it built with CMake.
	$overlayPorts = @((Join-Path $repo 'vcpkg-overlays/ports'), (Join-Path $repo 'cmake/cross/vcpkg-ports')) -join ';'
	& $cmake -S $repo -B $BuildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Config" `
		"-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake')" "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$crossToolchain" `
		"-DVCPKG_TARGET_TRIPLET=$triplet" '-DVCPKG_HOST_TRIPLET=x64-linux' `
		"-DVCPKG_OVERLAY_PORTS=$overlayPorts" "-DVCPKG_OVERLAY_TRIPLETS=$triplets" '-DVCPKG_APPLOCAL_DEPS=OFF' `
		"-DVCPKG_INSTALL_OPTIONS=$vcpkgOptions" "-DVCPKG_INSTALLED_DIR=$(Join-Path $BuildDir 'vcpkg_installed')" `
		"-DCMAKE_CROSSCOMPILING_EMULATOR=$((Get-Command $TestEmulator).Source)" `
		"-DSFVE_EMBER_SOURCE_ROOT=$EmberRoot" "-DSFVE_DISCORD_SDK_ARCHIVE=$DiscordSdkArchive" `
		"-DSFVE_DEV_BUILD=$(if ($Player) { 'OFF' } else { 'ON' })" @compilerCacheOptions
}
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
& $cmake --build $BuildDir
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
if (!$SkipTests) {
	& $ctest --test-dir $BuildDir --output-on-failure
	if ($LASTEXITCODE) { throw "tests failed ($LASTEXITCODE)" }
}
if ($sourceRevision -ne (& git -C $repo rev-parse HEAD) -or $sourceFingerprint -ne (Get-SourceFingerprint $repo)) {
	throw 'SFV Ember source changed while it was being built; no receipt is made for a tree the binaries were not built from. Build again.'
}
if ($emberRevision -ne (& git -C $EmberRoot rev-parse HEAD) -or $emberFingerprint -ne (Get-SourceFingerprint $EmberRoot)) {
	throw 'The Ember library source changed while SFV Ember was being built; no receipt is made. Build again.'
}

# The build's receipt (build-provenance.json in the build tree), which
# scripts/package.ps1 checks and the package carries: both checkouts that
# made it, at which commits and with which source fingerprints (the
# library's Get-SourceFingerprint: every tracked and untracked file's hash),
# the binaries it made and whether its tests passed. A build without its
# tests gets one too, marked so, and package.ps1 refuses it.
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
# Whether this build made the Discord companion: the configured build's own
# answer (CMakeLists.txt makes ember-discord only with an SDK archive), never
# whether an ember-discord.exe from an earlier build is lying in the tree.
$discordCompanion = [bool](Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^SFVE_DISCORD_SDK_ARCHIVE:[A-Z]+=.+$' -Quiet)
# The companion's whole runtime goes in the receipt: its exe, the SDK's DLL it
# loads and the record of what it was built with.
$binaryNames = @('Launcher.exe', 'Updater.exe', 'sfv_ember.dll') +
	@(if ($discordCompanion) { 'ember-discord.exe', 'discord_partner_sdk.dll', 'discord-build.json' })
# SFVE_VERSION as CMakeLists.txt makes it: the project's version, with the
# suffix a beta adds ("${PROJECT_VERSION}-beta.N"); a final has none.
$cmakeText = Get-Content -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Raw
if ($cmakeText -cnotmatch '(?m)^project\(sfv_ember VERSION ([0-9.]+)') { throw 'CMakeLists.txt names no sfv_ember version.' }
$sfveVersion = $Matches[1]
if ($cmakeText -cmatch '(?m)^set\(SFVE_VERSION "\$\{PROJECT_VERSION\}-([^"]+)"\)') { $sfveVersion = "$sfveVersion-$($Matches[1])" }
elseif ($cmakeText -cnotmatch '(?m)^set\(SFVE_VERSION "\$\{PROJECT_VERSION\}"\)') { throw 'CMakeLists.txt sets SFVE_VERSION in a form this script does not read.' }
# The toolchain, as the tools and the files they build with say it: on Windows the developer environment's
# MSVC tools and SDK; on Linux clang-cl's and lld-link's own versions and the CRT and SDK of the xwin splat.
$toolchain = if ($IsWindows) {
	if (!$env:VCToolsVersion -or !$env:WindowsSDKVersion) { throw 'The developer environment names no MSVC tools or Windows SDK version' }
	[ordered]@{ host = 'windows'; compiler = "MSVC $env:VCToolsVersion"; linker = "MSVC link $env:VCToolsVersion"; crt = "MSVC $env:VCToolsVersion"
		sdk = "Windows SDK $($env:WindowsSDKVersion.TrimEnd('\'))" }
} else {
	$clang = "$(& (Join-Path $env:SF4E_LLVM_BIN 'clang-cl') --version | Select-Object -First 1)".Trim()
	$lld = "$(& (Join-Path $env:SF4E_LLVM_BIN 'lld-link') --version | Select-Object -First 1)".Trim()
	$xwinVersion = (Get-Content -LiteralPath (Join-Path $env:SF4E_XWIN '.xwin-version') -Raw).Trim()
	$crtHeader = Get-Content -LiteralPath (Join-Path $env:SF4E_XWIN 'crt/include/crtversion.h') -Raw
	$crtVersion = @('MAJOR', 'MINOR', 'BUILD') | ForEach-Object {
		if ($crtHeader -cnotmatch "#define _VC_CRT_$($_)_VERSION ([0-9]+)") { throw "The splat's crtversion.h names no _VC_CRT_$($_)_VERSION" }
		$Matches[1]
	}
	$sdkVersions = @(Get-ChildItem -LiteralPath (Join-Path $env:SF4E_XWIN 'sdk/Include') -Directory | Where-Object { $_.Name -cmatch '^10\.[0-9.]+$' } | ForEach-Object Name)
	if ($sdkVersions.Count -ne 1) { throw "The splat holds $($sdkVersions.Count) Windows SDK versions, not one" }
	if (!$clang.StartsWith('clang version') -or !$lld.StartsWith('LLD')) { throw "Not clang-cl and lld-link: '$clang', '$lld'" }
	[ordered]@{ host = 'linux'; compiler = "clang-cl: $clang"; linker = "lld-link: $lld"; crt = "Microsoft CRT $($crtVersion -join '.') (xwin $xwinVersion)"
		sdk = "Windows SDK $($sdkVersions[0]) (xwin $xwinVersion)" }
}
$receipt = [ordered]@{
	schema = 1
	product = 'SFV Ember'
	# SFVE_VERSION as CMakeLists.txt makes it (above).
	version = $sfveVersion
	sourceRoot = $repo
	sourceRevision = $sourceRevision
	sourceFingerprint = $sourceFingerprint
	emberRoot = $EmberRoot
	emberRevision = $emberRevision
	emberFingerprint = $emberFingerprint
	buildRoot = $BuildDir
	config = $Config
	# The configured build's own answer, as discordCompanion below.
	buildKind = if (Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^SFVE_DEV_BUILD:BOOL=OFF$' -Quiet) { 'player' } else { 'development' }
	testsPassed = !$SkipTests
	tests = @(if (!$SkipTests) { & $ctest --test-dir $BuildDir -N | Select-String '^\s*Test\s+#\d+: (\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value } })
	discordCompanion = $discordCompanion
	# The ccache version the compiles went through; null when built without it.
	compilerCache = $ccacheVersion
	# Which compiler, linker, C runtime and Windows SDK made the binaries (below).
	toolchain = $toolchain
	binaries = @(foreach ($name in $binaryNames) {
		[ordered]@{ path = $name; sha256 = (Get-FileHash -LiteralPath (Join-Path $BuildDir $name) -Algorithm SHA256).Hash }
	})
	builtAt = (Get-Date).ToUniversalTime().ToString('o')
}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $BuildDir 'build-provenance.json') -Encoding UTF8
Write-Output "build receipt: $(Join-Path $BuildDir 'build-provenance.json') (tests $(if ($SkipTests) { 'skipped' } else { 'passed' }))"
