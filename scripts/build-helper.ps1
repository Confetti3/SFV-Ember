# Builds Ember's network helper (sf4-net.exe) from an Ember checkout for SFV
# Ember. The helper is Ember's own, unchanged: SFV Ember runs it the way the
# SF4 launcher does and talks to it over its named pipe.
#
# Output: $BuildDir\sf4-net.exe (tools/sfve.py deploy copies it next to the mod;
# scripts/package.ps1 packages it) and $BuildDir\sf4-net.provenance.json: the
# checkout, its commit and source fingerprint, its Cargo.lock, the neutral names
# the build machine's paths were remapped to (pathRemap) and the binary's
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
	[string]$Features = '',
	# Linux: rustc and the C compiles go through sccache unless this is given (scripts/build.ps1's cache).
	[switch]$NoCompilerCache
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

if ($IsWindows) {
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
} else {
	# A Linux build PC (docs/release/PUBLISHING.md, "The build PC"): the x64 Windows target cross-compiled as
	# scripts/build.ps1 does (cmake/cross/clang-cl-x64.cmake): lld-link links it against the xwin splat's CRT and
	# SDK (SF4E_XWIN), the cc crate compiles with clang-cl, and the build script's rc.exe is llvm-rc
	# (SF4E_LLVM_BIN). Wrappers for them go in the build tree.
	Assert-OutsideHome $EmberRepo '-EmberRepo'
	$x = $env:SF4E_XWIN; $l = $env:SF4E_LLVM_BIN
	foreach ($folder in $x, $l) { if (!$folder -or !(Test-Path -LiteralPath $folder -PathType Container)) { throw 'SF4E_XWIN and SF4E_LLVM_BIN must name the xwin splat and the LLVM tools' } }
	if ("$x$l".Contains("'") -or "$x$l".Contains(' ')) { throw 'The splat and LLVM paths may hold no quote or space' }
	$tools = Join-Path $BuildDir 'cross-bin'
	New-Item -ItemType Directory -Force $tools | Out-Null
	$inc = "/imsvc $x/crt/include /imsvc $x/sdk/include/ucrt /imsvc $x/sdk/include/um /imsvc $x/sdk/include/shared /imsvc $x/sdk/include/winrt"
	# The compiler cache scripts/build.ps1 uses (its folder and server port from the caller): rustc through
	# RUSTC_WRAPPER, which the cc crate puts before its C compiler (the clang-cl wrapper below) too.
	$sccache = if ($NoCompilerCache) { $null } else { (Get-Command sccache -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source }
	if ($sccache) {
		if (!$env:SCCACHE_DIR -or !$env:SCCACHE_SERVER_PORT) { throw 'sccache needs SCCACHE_DIR and SCCACHE_SERVER_PORT here (a cache and a server of this build''s own), or -NoCompilerCache' }
		Assert-OutsideHome $env:SCCACHE_DIR 'SCCACHE_DIR'
		if ($sccache.Contains("'")) { throw "A path with a quote: $sccache" }
		$env:RUSTC_WRAPPER = $sccache
		& $sccache --zero-stats | Out-Null
	} else { Remove-Item Env:\RUSTC_WRAPPER -ErrorAction SilentlyContinue }
	$wrappers = [ordered]@{
		'clang-cl-x64' = "exec '$l/clang-cl' --target=x86_64-pc-windows-msvc $inc `"`$@`""
		'lld-link-x64' = "export LIB='$x/crt/lib/x86_64;$x/sdk/lib/ucrt/x86_64;$x/sdk/lib/um/x86_64'`nexec '$l/lld-link' `"`$@`""
		'rc.exe' = "args=(); for a in `"`$@`"; do case `"`${a,,}`" in /nologo|-nologo) ;; *) args+=(`"`$a`") ;; esac; done`nexec '$l/llvm-rc' /I $x/sdk/include/um /I $x/sdk/include/shared /I $x/sdk/include/ucrt /I $x/crt/include `"`${args[@]}`""
		'ml64.exe' = "exec '$l/llvm-ml' -m64 `"`$@`""
	}
	foreach ($name in $wrappers.Keys) { [IO.File]::WriteAllText((Join-Path $tools $name), "#!/usr/bin/env bash`n$($wrappers[$name])`n") }
	& chmod 0755 @($wrappers.Keys | ForEach-Object { Join-Path $tools $_ })
	if ($LASTEXITCODE) { throw 'chmod failed' }
	$env:PATH = "$tools$([IO.Path]::PathSeparator)$env:PATH"
	$env:CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER = Join-Path $tools 'lld-link-x64'
	$env:CC_x86_64_pc_windows_msvc = Join-Path $tools 'clang-cl-x64'
	$env:CXX_x86_64_pc_windows_msvc = Join-Path $tools 'clang-cl-x64'
	$env:AR_x86_64_pc_windows_msvc = Join-Path $l 'llvm-lib'
}

$env:CARGO_TARGET_DIR = Join-Path $BuildDir 'sf4-net-target'

# No build-machine path ships in sf4-net.exe: rustc puts source paths into panic
# locations and assertions (the registry crates', the standard library's, this
# crate's) and the C compiler's __FILE__ into the code it builds (ring, sqlite,
# blake3 through the cc crate). Each prefix is derived here from the environment
# and mapped to a short neutral name; none is written down. Later --remap-path-prefix
# rules win in rustc, so the broad one (the user profile) comes first. Flags the
# caller already set (CARGO_ENCODED_RUSTFLAGS, RUSTFLAGS, CFLAGS) are kept and ours
# are appended; rustflags in a cargo config file would be replaced by the
# environment's, so one there stops the build rather than being lost.
$userProfile = if ($IsWindows) { $env:USERPROFILE } else { $HOME }
$cargoHome = if ($env:CARGO_HOME) { $env:CARGO_HOME } else { Join-Path $userProfile '.cargo' }
$rustupHome = if ($env:RUSTUP_HOME) { $env:RUSTUP_HOME } else { Join-Path $userProfile '.rustup' }
$remaps = [ordered]@{ '/user' = $userProfile; '/cargo' = $cargoHome; '/rustup' = $rustupHome; '/src' = $EmberRepo; '/sfv' = $repo; '/target' = $env:CARGO_TARGET_DIR }
$rustFlags = New-Object System.Collections.Generic.List[string]
if ($env:CARGO_ENCODED_RUSTFLAGS) { $rustFlags.AddRange([string[]]($env:CARGO_ENCODED_RUSTFLAGS -split [char]0x1f)) }
elseif ($env:RUSTFLAGS) { $rustFlags.AddRange([string[]]($env:RUSTFLAGS -split '\s+' | Where-Object { $_ })) }
# Cargo takes the first of CARGO_ENCODED_RUSTFLAGS, RUSTFLAGS and the target's rustflags (the library's CMake sets
# CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS): ours would hide the last, so it is kept here.
elseif ($env:CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS) { $rustFlags.AddRange([string[]]($env:CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS -split '\s+' | Where-Object { $_ })) }
$trims = @()
foreach ($name in $remaps.Keys) {
	$from = $remaps[$name]
	if (!$from) { continue }
	$from = [IO.Path]::GetFullPath($from).TrimEnd([IO.Path]::DirectorySeparatorChar)
	$rustFlags.Add("--remap-path-prefix=$from=$name")
	# clang-cl (Linux) maps the prefix as clang does.
	$trims += if ($IsWindows) { "/d1trimfile:$from\" } else { "/clang:-ffile-prefix-map=$from=$name" }
}
foreach ($config in @((Join-Path (Split-Path -Parent $manifest) '.cargo\config.toml'), (Join-Path $EmberRepo '.cargo\config.toml'), (Join-Path $cargoHome 'config.toml'))) {
	if ((Test-Path -LiteralPath $config) -and (Select-String -LiteralPath $config -Pattern 'rustflags' -Quiet)) {
		throw "$config sets rustflags, which the path remapping would replace: merge them into this script"
	}
}
$env:CARGO_ENCODED_RUSTFLAGS = $rustFlags -join [string][char]0x1f
Remove-Item Env:\RUSTFLAGS -ErrorAction SilentlyContinue
# cl.exe (the cc crate reads CFLAGS): /d1trimfile cuts the prefix from __FILE__. On Linux the flags are the
# Windows target's alone (CFLAGS would reach the host compiler of build scripts too).
if ($IsWindows) { $env:CFLAGS = ((@($env:CFLAGS) + $trims) | Where-Object { $_ }) -join ' ' }
else { $env:CFLAGS_x86_64_pc_windows_msvc = ((@($env:CFLAGS_x86_64_pc_windows_msvc) + $trims) | Where-Object { $_ }) -join ' ' }
$featureArgs = if ($Features) { @('--features', $Features) } else { @() }
# On Linux the Windows target is named, and its output goes to a folder of that name.
$targetArgs = if ($IsWindows) { @() } else { @('--target', 'x86_64-pc-windows-msvc') }
cargo build --release --locked --manifest-path $manifest -j $Jobs @targetArgs @featureArgs
if ($LASTEXITCODE) { throw "cargo build failed ($LASTEXITCODE)" }
if (!$IsWindows -and $sccache) {
	# What the cache served this build (the publish job reads these lines from the log).
	Write-Output '== sccache statistics'
	& $sccache --show-stats
}
# The cache rustc went through (RUSTC_WRAPPER), kind and version; null when none.
$compilerCache = $null
if ($env:RUSTC_WRAPPER) {
	$wrapperVersion = [regex]::Match("$(& $env:RUSTC_WRAPPER --version | Select-Object -First 1)", '^(\S+) ([0-9]+(\.[0-9]+)+)')
	$compilerCache = if ($wrapperVersion.Success) { "$([IO.Path]::GetFileNameWithoutExtension($wrapperVersion.Groups[1].Value)) $($wrapperVersion.Groups[2].Value)" } else { "$(Split-Path -Leaf $env:RUSTC_WRAPPER) (no version)" }
}
$helper = Join-Path $BuildDir 'sf4-net.exe'
Copy-Item (Join-Path $env:CARGO_TARGET_DIR $(if ($IsWindows) { 'release\sf4-net.exe' } else { 'x86_64-pc-windows-msvc/release/sf4-net.exe' })) $helper -Force

. (Join-Path $EmberRepo 'scripts\BuildProvenance.ps1')
[ordered]@{
	schema = 1
	emberRoot = $EmberRepo
	emberRevision = (& git -C $EmberRepo rev-parse HEAD)
	emberFingerprint = Get-SourceFingerprint $EmberRepo
	cargoLockSha256 = (Get-FileHash -LiteralPath (Join-Path $EmberRepo 'rust\sf4-net\Cargo.lock') -Algorithm SHA256).Hash
	sha256 = (Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash
	pathRemap = @($remaps.Keys)
	# The Rust compiler, and the linker and C compiler of the Windows target (build.ps1's toolchain record).
	toolchain = [ordered]@{
		# Asked in the crate's folder: its rust-toolchain.toml picks the compiler.
		rustc = "$(Push-Location (Split-Path -Parent $manifest); try { & rustc --version } finally { Pop-Location })".Trim()
		linker = if ($IsWindows) { "MSVC link $env:VCToolsVersion" } else { "lld-link: $("$(& (Join-Path $env:SF4E_LLVM_BIN 'lld-link') --version | Select-Object -First 1)".Trim())" }
		cc = if ($IsWindows) { "MSVC $env:VCToolsVersion" } else { "clang-cl: $("$(& (Join-Path $env:SF4E_LLVM_BIN 'clang-cl') --version | Select-Object -First 1)".Trim())" }
	}
	# The cache rustc's compiles went through, kind and version (above); null when none.
	compilerCache = $compilerCache
	builtAt = (Get-Date).ToUniversalTime().ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BuildDir 'sf4-net.provenance.json') -Encoding UTF8
$commit = git -C $EmberRepo rev-parse --short HEAD
Write-Output "built sf4-net.exe from $EmberRepo at $commit$(if ($Features) { " with $Features" })"
