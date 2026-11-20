# Packages SFV Ember from a verified build, locally: a folder and a zip under
# -OutDir (D:\sfv-ember-dist by default, never the C: drive), with the zip's
# .sha256 beside it. Nothing is published.
#
#   scripts\package.ps1 -RenderStore <dir> [-BuildDir D:\sfv-ember-build-player] [-OutDir D:\sfv-ember-dist] [-VersionLabel <label>]
#       [-MinColourCoverage 0] [-ColourPack [-ColourPackRepo Confetti3/SFV-Ember]] [-OldPackageCheck <sfve_package_check.exe of an earlier beta>] [-NoRenders]
#
# The build must be scripts\build.ps1's player build (-Player), with its tests
# passed, and the helper scripts\build-helper.ps1's from the same Ember
# library checkout. A development build, whose launcher and mod take the test
# switches (src/mod/BuildKind.hxx), is refused. Before
# staging anything this checks, with the Ember library's own provenance
# functions (its scripts\BuildProvenance.ps1 and PackageFiles.ps1, which SF4
# Ember's packaging uses):
#   - the build tree was configured from this checkout (Assert-CMakeSource) and
#     from the library checkout its receipt names;
#   - both checkouts are still at the receipt's commits and fingerprints
#     (Get-SourceFingerprint, fresh): nothing changed since the build;
#   - the built binaries still have the receipt's hashes;
#   - the helper was built from that library checkout, at its fingerprint, and
#     is the binary its record names.
# Then it stages the inventory (src\launcher\PackageInventory.inc), the
# dependency notices (the library's scripts\collect-notices.py), START_HERE.md,
# BUILD_INFO.txt, build-provenance.json (the build's receipt with the
# helper's record and the package's own facts) and MANIFEST.txt; validates the
# folder with the updater's own rules (sfve_package_check.exe --validate, built
# with SFV Ember's inventory); zips it; and runs the library's updater download
# checks (scripts\test-updater-download-validation.ps1) on the zip.
#
# The render store (-RenderStore, beta.8): the captured cutouts of every
# fighter look and the loadout pictures, which the capture tools leave under
# <store>\pack with a manifest.json (docs\design\beta8\pack.md is the format).
# A player package needs it. Before anything is staged every file is checked
# against the manifest's size and SHA-256 and against the pick space, every
# offered costume's first colour must be there (or a listed exception; a
# costume the pick space marks unshipped is never offered, needs no picture
# and may have none) and the other colours must reach -MinColourCoverage
# (default 0: beta.8 to 0.1.0 shipped each costume's first colour only; the
# release pipeline passes 1, every colour, from 0.1.1; the coverage is always
# reported). Every loadout
# picture (a V-Skill and V-Trigger of every fighter) is required too, or a
# listed exception. The zip ships every picture of the store (docs\design\COLOUR_PACK.md):
# they are staged under assets\selection\, a manifest of them beside them, and
# the package's PackageInventory.inc lists each one (an installed updater of an
# earlier beta refuses files it does not know unless the package's own
# inventory does). -ColourPack splits them instead (ColourPack.ps1): the zip
# keeps each costume's first colour and the loadout pictures, every other
# colour goes into the colour pack, written beside the zip under -OutDir as
# sfve-colours-<id>.zip, which the launcher downloads from -ColourPackRepo's
# colour-packs release; colour-pack.json in the package names it. The
# provenance receipt gets a "renders" record: the store manifest's
# hash, counts, bytes, coverage, pipeline and the capture's game build and mod
# revision, what the zip holds, and the pack. -OldPackageCheck <path> also validates the staged folder with a
# kept copy of an earlier beta's sfve_package_check.exe (beta.7's), which
# applies that release's compiled rules. -NoRenders makes a package without
# renders (a development or test package), with a warning, and the receipt
# says so.
#
# On a Linux build PC (docs/release/PUBLISHING.md, "The build PC") it packages
# the same files as on Windows: the checkouts there are made with Windows line
# endings (core.autocrlf), the files this script writes get CRLF lines and
# MANIFEST.txt its '\' paths, the notices come from python3, and the Windows
# programs (sfve_package_check.exe, -OldPackageCheck) run through
# SFVE_WINE_EMULATOR (Linux paths given as Z: paths). The zip's entries are
# the same; the archive records Unix as their origin.

param(
	[string]$BuildDir = 'D:\sfv-ember-build-player',
	[string]$OutDir = 'D:\sfv-ember-dist',
	[string]$VersionLabel = '',
	[string]$RenderStore = '',
	[ValidateRange(0.0, 1.0)][double]$MinColourCoverage = 0.0,
	# Every colour but each costume's first in a colour pack beside the zip, not in the zip.
	[switch]$ColourPack,
	# With -ColourPack: the public repository whose colour-packs release hosts the pack: the product's own
	# (SfvProduct.cxx's stableRepo), or the launcher refuses the descriptor.
	[string]$ColourPackRepo = 'Confetti3/SFV-Ember',
	[string]$OldPackageCheck = '',
	[switch]$NoRenders
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Off
$repo = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
# Read-RenderStore: the render store's checks (tests\RenderStoreTests.ps1 runs them on small stores).
. (Join-Path $PSScriptRoot 'RenderStore.ps1')
# The split between the zip and the colour pack, and the pack itself.
. (Join-Path $PSScriptRoot 'ColourPack.ps1')
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

if ($IsWindows) {
	$freeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
	if ($freeGb -lt 120) { throw "C: has $freeGb GB free; stopping (floor 120 GB)" }
}
if ([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($OutDir)) -ieq 'C:\') { throw "Package output stays off the C: drive; got $OutDir" }

$receiptPath = Join-Path $BuildDir 'build-provenance.json'
if (!(Test-Path -LiteralPath $receiptPath)) { throw "No build receipt in $BuildDir; build with scripts\build.ps1" }
$receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
$emberRoot = $receipt.emberRoot
. (Join-Path $emberRoot 'scripts\BuildProvenance.ps1')
. (Join-Path $emberRoot 'scripts\PackageFiles.ps1')
if (!$IsWindows) {
	# The library's writers of MANIFEST.txt and the .sha256 sidecar, with the bytes they write on Windows: CRLF
	# lines, and MANIFEST.txt's paths relative with '\' in the ordinal order of the Windows paths.
	function Write-PackageLines([string]$Path, [string[]]$Lines, [Text.Encoding]$Encoding) {
		[IO.File]::WriteAllText((Resolve-PackagePath $Path), (-join @($Lines | ForEach-Object { "$_`r`n" })), $Encoding)
	}
	function Write-HashManifest([string]$Directory, [string]$Name = 'MANIFEST.txt') {
		$Directory = $Directory.TrimEnd('\', '/')
		$files = @{}
		foreach ($file in Get-ChildItem -LiteralPath $Directory -File -Recurse) { $files[$file.FullName.Substring($Directory.Length + 1).Replace('/', '\')] = $file.FullName }
		$relative = [string[]]@($files.Keys)
		[Array]::Sort($relative, [StringComparer]::Ordinal)
		$lines = [string[]]@(foreach ($path in $relative) { '{0}  {1}' -f (Get-FileSha256 $files[$path]), $path })
		Write-PackageLines (Join-Path $Directory $Name) $lines ([Text.UTF8Encoding]::new($false))
	}
	# A text file this script (or python3) wrote with LF lines, given the CRLF lines Windows writes.
	function ConvertTo-WindowsLines([string]$Path) {
		$text = [IO.File]::ReadAllText($Path)
		[IO.File]::WriteAllText($Path, ($text -replace "(?<!`r)`n", "`r`n"), [Text.UTF8Encoding]::new($false))
	}
	# A Windows program run through Wine: a wrapper that gives it its Linux path arguments as Z: paths.
	$wine = $env:SFVE_WINE_EMULATOR
	if (!$wine -or !(Get-Command $wine -ErrorAction SilentlyContinue)) { throw "Name the program that runs a Windows program (SFVE_WINE_EMULATOR; got '$wine')" }
	$wine = (Get-Command $wine).Source
	$wrappers = Join-Path ([IO.Path]::GetTempPath()) ('sfve-package-wine-' + [Guid]::NewGuid().ToString('N'))
	New-Item -ItemType Directory -Path $wrappers | Out-Null
	function New-WineCommand([string]$Program) {
		if ("$wine$Program".Contains("'")) { throw "A path with a quote: $Program" }
		$wrapper = Join-Path $wrappers ([IO.Path]::GetFileNameWithoutExtension($Program) + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
		# Each '/' of a Linux path becomes '\' after Z: (bash's ${a//\//$bs}, $bs a backslash).
		$body = @'
#!/usr/bin/env bash
bs='\'
args=()
for a in "$@"; do case "$a" in /*) a="Z:${a//\//$bs}" ;; esac; args+=("$a"); done
exec 'WINE' 'PROGRAM' "${args[@]}"
'@
		[IO.File]::WriteAllText($wrapper, $body.Replace("`r`n", "`n").Replace("'WINE'", "'$wine'").Replace("'PROGRAM'", "'$Program'"))
		& chmod 0755 $wrapper
		if ($LASTEXITCODE) { throw "chmod failed for $wrapper" }
		$wrapper
	}
}

# The receipt is this build's, from these checkouts as they are now.
Assert-CMakeSource $repo $BuildDir
$cachedEmber = (Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^SFVE_EMBER_SOURCE_ROOT:[A-Z]+=(.+)$').Matches[0].Groups[1].Value
if ([IO.Path]::GetFullPath($cachedEmber) -ne [IO.Path]::GetFullPath($emberRoot)) { throw "The build tree was configured with $cachedEmber, its receipt names $emberRoot" }
if ($receipt.sourceRoot -ne $repo -or $receipt.buildRoot -ne $BuildDir) { throw 'Build receipt path mismatch' }
if (!$receipt.testsPassed) { throw 'The build has no passing tests; build with scripts\build.ps1 without -SkipTests' }
# A player build, by its receipt and by the configured tree itself.
if ($receipt.buildKind -ne 'player' -or !(Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^SFVE_DEV_BUILD:BOOL=OFF$' -Quiet)) {
	throw 'This is a development build, whose test switches no package may carry: build with scripts\build.ps1 -Player, into a -BuildDir of its own'
}
if ($receipt.sourceRevision -ne (& git -C $repo rev-parse HEAD)) { throw 'This checkout moved since the build; rebuild' }
if ($receipt.emberRevision -ne (& git -C $emberRoot rev-parse HEAD)) { throw 'The library checkout moved since the build; rebuild' }
if ($receipt.sourceFingerprint -ne (Get-SourceFingerprint $repo)) { throw 'SFV Ember source changed since the build; rebuild with scripts\build.ps1' }
$emberFingerprint = Get-SourceFingerprint $emberRoot
if ($receipt.emberFingerprint -ne $emberFingerprint) { throw 'Ember library source changed since the build; rebuild with scripts\build.ps1' }
foreach ($binary in $receipt.binaries) {
	if ((Get-FileHash -LiteralPath (Join-Path $BuildDir $binary.path) -Algorithm SHA256).Hash -ne $binary.sha256) { throw "Built binary changed since the build: $($binary.path)" }
}
$helperPath = Join-Path $BuildDir 'sf4-net.exe'
$helperRecordPath = Join-Path $BuildDir 'sf4-net.provenance.json'
if (!(Test-Path -LiteralPath $helperRecordPath)) { throw "No helper record in $BuildDir; build the helper with scripts\build-helper.ps1" }
$helper = Get-Content -LiteralPath $helperRecordPath -Raw | ConvertFrom-Json
if ($helper.emberRoot -ne $emberRoot -or $helper.emberRevision -ne $receipt.emberRevision -or $helper.emberFingerprint -ne $emberFingerprint) {
	throw 'sf4-net.exe was not built from the library checkout and source this build used; rebuild it with scripts\build-helper.ps1'
}
if ((Get-FileHash -LiteralPath $helperPath -Algorithm SHA256).Hash -ne $helper.sha256) { throw 'sf4-net.exe is not the binary its record names' }
$validator = Join-Path $BuildDir 'sfve_package_check.exe'
if (!(Test-Path -LiteralPath $validator)) { throw "Build sfve_package_check before packaging: $validator" }
if (!$IsWindows) { $validator = New-WineCommand $validator }


$renders = $null
if ($RenderStore) {
	if ($NoRenders) { throw '-RenderStore and -NoRenders contradict each other' }
	$renders = Read-RenderStore $RenderStore $MinColourCoverage (Join-Path $repo 'data\pick-space.json')
	$split = Split-RenderFiles $renders -ColourPack:$ColourPack
	Write-Output ('Renders: {0} cutouts, {1} of {2} loadout pictures, {3:P2} colour coverage, {4:N1} MB, manifest {5}; {6} files in the zip, {7} in the colour pack' -f $renders.Cutouts, $renders.Loadouts, $renders.LoadoutTotal, $renders.Coverage, ($renders.Bytes / 1MB), $renders.ManifestHash, $split.Zip.Count, $split.Pack.Count)
} elseif ($NoRenders) {
	Write-Warning 'Packaging without renders (-NoRenders): the pick screen draws the game''s own pictures only. Not for players.'
} else {
	throw 'A player package needs the render store: pass -RenderStore <dir> (or -NoRenders for a development package without renders)'
}
if ($OldPackageCheck -and !(Test-Path -LiteralPath $OldPackageCheck -PathType Leaf)) { throw "No earlier sfve_package_check at $OldPackageCheck" }
if ($OldPackageCheck -and !$IsWindows) { $OldPackageCheck = New-WineCommand ((Resolve-Path -LiteralPath $OldPackageCheck).Path) }

if (!$VersionLabel) { $VersionLabel = '{0}-{1}' -f $receipt.version, (Get-Date).ToString('yyyyMMdd-HHmmss', [cultureinfo]::InvariantCulture) }
if ($VersionLabel -notmatch '^[a-zA-Z0-9._-]+$') { throw 'Invalid version label' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$destination = Join-Path $OutDir "sfv-ember-$VersionLabel"
if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath "$destination.zip")) { throw "Package destination already exists: $destination" }
New-Item -ItemType Directory -Path $destination | Out-Null

function Stage([string]$Source, [string]$Relative) {
	if (!(Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Package file missing: $Source" }
	$target = Join-Path $destination $Relative
	New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
	Copy-Item -LiteralPath $Source -Destination $target
}
foreach ($name in 'Launcher.exe', 'Updater.exe', 'sfv_ember.dll') { Stage (Join-Path $BuildDir $name) $name }
Stage $helperPath 'sf4-net.exe'
# Discord's companion, its SDK's DLL and what it was built with; a package
# without them is refused. Whether the build made them is the receipt's word
# (build.ps1 reads the configured build), and the receipt's hashes, checked
# with every other binary above, keep out anything an earlier build or a
# later hand left in their place.
$companionFiles = 'ember-discord.exe', 'discord_partner_sdk.dll', 'discord-build.json'
if (!$receipt.discordCompanion -or @($companionFiles | Where-Object { $_ -notin $receipt.binaries.path }).Count) {
	throw 'This build has no Discord companion: build with the Discord SDK archive (scripts\build.ps1 -DiscordSdkArchive, or SF4E_DISCORD_SDK_ARCHIVE)'
}
Stage (Join-Path $BuildDir 'ember-discord.exe') 'ember-discord.exe'
Stage (Join-Path $BuildDir 'discord_partner_sdk.dll') 'discord_partner_sdk.dll'
Stage (Join-Path $BuildDir 'discord-build.json') 'discord-build.json'
Stage (Join-Path $BuildDir 'Discord-SDK-notices.txt') 'notices\Discord-SDK.txt'
# The pictures the shell draws (the build copies them beside the mod): SFV
# Ember's ground, and Ember's button prompts and flags, which the updater's
# rules allow by name pattern (PackageInventory.hxx, IsArtAssetPath). Their
# licence goes to notices\, not beside them.
Stage (Join-Path $BuildDir 'assets\brand\sfv-ember-menu-background.png') 'assets\brand\sfv-ember-menu-background.png'
foreach ($art in @(Get-ChildItem -LiteralPath (Join-Path $BuildDir 'assets\input') -File -Filter '*.png') +
		@(Get-ChildItem -LiteralPath (Join-Path $BuildDir 'assets\flags') -File | Where-Object { $_.Extension -in '.png', '.json' })) {
	Stage $art.FullName ($art.FullName.Substring($BuildDir.Length + 1))
}
# Each art set's licence beside it, as SF4 Ember's package has them, and the
# notices: the fonts, Dear ImGui, the flags and Ember's own licence and
# attribution (SFV Ember is built on Ember's code).
Stage (Join-Path $emberRoot 'assets\input\License.txt') 'assets\input\License.txt'
Stage (Join-Path $emberRoot 'assets\flags\LICENSE') 'assets\flags\LICENSE'
Stage (Join-Path $emberRoot 'src\ui\fonts\OFL.txt') 'notices\Inter-OFL.txt'
Stage (Join-Path $emberRoot 'src\ui\fonts\NotoSansCJK-OFL.txt') 'notices\NotoSansCJK-OFL.txt'
Stage (Join-Path $emberRoot 'src\ui\backends\LICENSE.txt') 'notices\Dear-ImGui-MIT.txt'
Stage (Join-Path $emberRoot 'assets\flags\LICENSE') 'notices\flag-icons-MIT.txt'
Stage (Join-Path $emberRoot 'LICENSE') 'notices\Ember-LICENSE.txt'
Stage (Join-Path $emberRoot 'ATTRIBUTION.md') 'notices\Ember-ATTRIBUTION.md'
Stage (Join-Path $repo 'LICENSE') 'notices\SFV-Ember-LICENSE.txt'
if ($IsWindows) {
& python (Join-Path $emberRoot 'scripts\collect-notices.py') --build-dir $BuildDir --output (Join-Path $destination 'notices\THIRD_PARTY_LICENSES.txt') `
	--product 'SFV Ember' --native cli11 detours fmt ggpo imgui minhook nlohmann-json spdlog xxhash zlib | Out-Host
} else {
	# The script runs CARGO_HOME's bin/cargo.exe, as on Windows: a CARGO_HOME of its own whose cargo.exe is this
	# PC's cargo, with the real one's registry and git checkouts (the crates the helper was built from).
	$cargoHome = if ($env:CARGO_HOME) { $env:CARGO_HOME } else { Join-Path $HOME '.cargo' }
	$cargo = (Get-Command cargo -CommandType Application | Select-Object -First 1).Source
	$noticeHome = Join-Path $wrappers 'cargo-home'
	New-Item -ItemType Directory -Path (Join-Path $noticeHome 'bin') -Force | Out-Null
	foreach ($name in 'registry', 'git') { if (Test-Path -LiteralPath (Join-Path $cargoHome $name)) { New-Item -ItemType SymbolicLink -Path (Join-Path $noticeHome $name) -Target (Join-Path $cargoHome $name) | Out-Null } }
	[IO.File]::WriteAllText((Join-Path $noticeHome 'bin/cargo.exe'), "#!/usr/bin/env bash`nexec '$cargo' `"`$@`"`n")
	& chmod 0755 (Join-Path $noticeHome 'bin/cargo.exe')
	$savedCargoHome = $env:CARGO_HOME
	$env:CARGO_HOME = $noticeHome
	try {
		& python3 (Join-Path $emberRoot 'scripts\collect-notices.py') --build-dir $BuildDir --output (Join-Path $destination 'notices\THIRD_PARTY_LICENSES.txt') `
			--product 'SFV Ember' --native cli11 detours fmt ggpo imgui minhook nlohmann-json spdlog xxhash zlib | Out-Host
	} finally { $env:CARGO_HOME = $savedCargoHome }
}
if ($LASTEXITCODE) { throw 'Dependency notice collection failed' }
# The packaged inventory is the repo's plus one OPTIONAL line per render (and
# the manifest): the installed updater of an earlier beta refuses a file its
# compiled rules do not know unless the package's own inventory lists it
# (PackageInstaller.cxx), and this is where it learns the renders' names. Built
# from the repo's file as it stands, so every REQUIRED and OBSOLETE line stays.
$inventorySource = Join-Path $repo 'src\launcher\PackageInventory.inc'
if ($renders) {
	$lines = [Collections.Generic.List[string]]::new()
	$lines.Add('')
	$lines.Add('// Generated by scripts\package.ps1 from the render store''s manifest (' + $renders.ManifestHash + '):')
	$lines.Add('// the captured selection pictures the zip ships, listed so an installed updater accepts them.')
	$lines.Add('SF4E_PACKAGE_OPTIONAL("assets\\selection\\manifest.json")')
	foreach ($render in @($split.Zip | Sort-Object Path)) {
		$lines.Add('SF4E_PACKAGE_OPTIONAL("' + $render.Relative.Replace('\', '\\') + '")')
	}
	$text = [IO.File]::ReadAllText($inventorySource)
	if (!$text.EndsWith("`n")) { $text += "`r`n" }
	$generated = Join-Path $destination 'PackageInventory.inc'
	[IO.File]::WriteAllText($generated, $text + ($lines -join "`r`n") + "`r`n", [Text.UTF8Encoding]::new($false))
} else {
	Stage $inventorySource 'PackageInventory.inc'
}
$pack = $null
if ($renders) {
	$shippedManifest = Join-Path $destination 'assets\selection\manifest.json'
	New-Item -ItemType Directory -Path (Split-Path -Parent $shippedManifest) -Force | Out-Null
	[IO.File]::WriteAllBytes($shippedManifest, (Get-RenderManifestBytes $renders.Manifest $split.Zip))
	foreach ($render in $split.Zip) { Stage $render.Native $render.Relative }
	# With -ColourPack, every other colour: the pack beside the zip, and the descriptor in the package that names it.
	if ($split.Pack.Count) {
		# The launcher refuses a descriptor of any repository but its own stableRepo, which it carries as text.
		if (![Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes((Join-Path $destination 'Launcher.exe'))).Contains($ColourPackRepo)) {
			throw "Launcher.exe does not name $ColourPackRepo, so it would refuse the colour pack's URL there: pass its own repository as -ColourPackRepo"
		}
		$pack = New-ColourPack $renders $split.Pack $OutDir $ColourPackRepo
		Write-ColourPackDescriptor $pack (Join-Path $destination 'colour-pack.json')
		Write-Output ('Colour pack: {0}, {1} cutouts, {2:N1} MB, {3}' -f $pack.Name, $pack.Cutouts, ($pack.Bytes / 1MB), $pack.Url)
	}
}
Stage (Join-Path $repo 'docs\guides\START_HERE.md') 'START_HERE.md'

# The build's receipt, with the helper's record and this package's facts.
$provenance = [ordered]@{}
foreach ($property in $receipt.PSObject.Properties) { $provenance[$property.Name] = $property.Value }
$provenance.helper = $helper
$provenance.package = [ordered]@{ label = $VersionLabel; packagedAt = (Get-Date).ToUniversalTime().ToString('o') }
$provenance.renders = if ($renders) {
	[ordered]@{
		included = $true
		manifestSha256 = $renders.ManifestHash
		pipeline = $renders.Manifest.pipeline
		captureGameBuild = $renders.Manifest.gameBuild
		captureModRevision = $renders.Manifest.modRevision
		files = $renders.Files.Count
		cutouts = $renders.Cutouts
		loadoutPictures = $renders.Loadouts
		loadoutPicturesPossible = $renders.LoadoutTotal
		looks = $renders.Looks
		looksExcepted = $renders.Excepted
		unshippedCostumes = $renders.Unshipped
		loadoutPicturesExcepted = $renders.LoadoutExcepted
		bytes = $renders.Bytes
		colourCoverage = [math]::Round($renders.Coverage, 6)
		minColourCoverage = $MinColourCoverage
		# What the zip holds of the store, and the pack that holds the rest (null when nothing is left over).
		shipped = [ordered]@{
			files = $split.Zip.Count
			cutouts = @($split.Zip | Where-Object { $_.Kind -cne 'loadout' }).Count
			loadoutPictures = @($split.Zip | Where-Object { $_.Kind -ceq 'loadout' }).Count
		}
		colourPack = if ($pack) {
			[ordered]@{ id = $pack.Id; name = $pack.Name; bytes = $pack.Bytes; sha256 = $pack.Sha256; url = $pack.Url; cutouts = $pack.Cutouts }
		} else { $null }
	}
} else {
	[ordered]@{ included = $false }
}
$provenance | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'build-provenance.json') -Encoding UTF8
Set-Content -LiteralPath (Join-Path $destination 'BUILD_INFO.txt') -Encoding UTF8 -Value @(
	"SFV Ember $($receipt.version)", "Release: $VersionLabel",
	"Source: $repo at $($receipt.sourceRevision)", "Source fingerprint: $($receipt.sourceFingerprint)",
	"Ember library: $emberRoot at $($receipt.emberRevision)", "Ember library fingerprint: $($receipt.emberFingerprint)",
	'See build-provenance.json for the binaries, the helper and the tests.')
if ($renders) {
	Add-Content -LiteralPath (Join-Path $destination 'BUILD_INFO.txt') -Encoding UTF8 -Value @(
		"Selection renders: $($renders.Cutouts) cutouts, $($renders.Loadouts) loadout pictures, colour coverage $([math]::Round($renders.Coverage * 100, 2))%",
		"Render manifest sha256: $($renders.ManifestHash)", "Render pipeline: $($renders.Manifest.pipeline)",
		"Captured on game build $($renders.Manifest.gameBuild) with mod revision $($renders.Manifest.modRevision)",
		"In the zip: $($split.Zip.Count) pictures ($(if ($pack) { "each costume's first colour and the loadout pictures" } else { 'every colour and the loadout pictures' }))")
	if ($pack) {
		Add-Content -LiteralPath (Join-Path $destination 'BUILD_INFO.txt') -Encoding UTF8 -Value @(
			"Colour pack: $($pack.Name), $($pack.Cutouts) cutouts, $($pack.Bytes) bytes, sha256 $($pack.Sha256)",
			"Colour pack URL: $($pack.Url)")
	}
}
if (!$IsWindows) { foreach ($name in 'build-provenance.json', 'BUILD_INFO.txt', 'notices\THIRD_PARTY_LICENSES.txt') { ConvertTo-WindowsLines (Join-Path $destination $name) } }
Write-HashManifest $destination 'MANIFEST.txt'

# The updater's own rules on the folder, then on the zip as a download.
& $validator --validate $destination | Out-Host
if ($LASTEXITCODE) { throw "The updater's package rules refuse the folder ($LASTEXITCODE)" }
# An earlier beta's own rules (its compiled inventory and selection paths) on the
# same folder: the renders must be listed by this package's inventory for its
# updater to take them.
if ($OldPackageCheck) {
	& $OldPackageCheck --validate $destination | Out-Host
	if ($LASTEXITCODE) { throw "The earlier beta's package rules refuse the folder ($LASTEXITCODE)" }
}
Compress-Archive -LiteralPath $destination -DestinationPath "$destination.zip"
& (Join-Path $emberRoot 'scripts\test-updater-download-validation.ps1') -Validator $validator -PackageZip "$destination.zip" | Out-Host
if ($LASTEXITCODE) { throw "Updater download validation failed ($LASTEXITCODE)" }
$hash = Write-Sha256Sidecar "$destination.zip"
if (!$IsWindows) { Remove-Item -LiteralPath $wrappers -Recurse -Force }
Write-Output "SFV Ember package: $destination.zip ($hash)"
[pscustomobject]@{ zipPath = "$destination.zip"; folderPath = $destination; sha256 = $hash; colourPackPath = $(if ($pack) { $pack.Path } else { '' }) }
