# Packages SFV Ember from a verified build, locally: a folder and a zip under
# -OutDir (D:\sfv-ember-dist by default, never the C: drive), with the zip's
# .sha256 beside it. Nothing is published.
#
#   scripts\package.ps1 [-BuildDir D:\sfv-ember-build] [-OutDir D:\sfv-ember-dist] [-VersionLabel <label>]
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

param(
	[string]$BuildDir = 'D:\sfv-ember-build',
	[string]$OutDir = 'D:\sfv-ember-dist',
	[string]$VersionLabel = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Off
$repo = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

$freeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
if ($freeGb -lt 120) { throw "C: has $freeGb GB free; stopping (floor 120 GB)" }
if ([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($OutDir)) -ieq 'C:\') { throw "Package output stays off the C: drive; got $OutDir" }

$receiptPath = Join-Path $BuildDir 'build-provenance.json'
if (!(Test-Path -LiteralPath $receiptPath)) { throw "No build receipt in $BuildDir; build with scripts\build.ps1" }
$receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
$emberRoot = $receipt.emberRoot
. (Join-Path $emberRoot 'scripts\BuildProvenance.ps1')
. (Join-Path $emberRoot 'scripts\PackageFiles.ps1')

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
& python (Join-Path $emberRoot 'scripts\collect-notices.py') --build-dir $BuildDir --output (Join-Path $destination 'notices\THIRD_PARTY_LICENSES.txt') `
	--product 'SFV Ember' --native cli11 detours fmt ggpo imgui minhook nlohmann-json spdlog xxhash zlib | Out-Host
if ($LASTEXITCODE) { throw 'Dependency notice collection failed' }
Stage (Join-Path $repo 'src\launcher\PackageInventory.inc') 'PackageInventory.inc'
Stage (Join-Path $repo 'docs\guides\START_HERE.md') 'START_HERE.md'

# The build's receipt, with the helper's record and this package's facts.
$provenance = [ordered]@{}
foreach ($property in $receipt.PSObject.Properties) { $provenance[$property.Name] = $property.Value }
$provenance.helper = $helper
$provenance.package = [ordered]@{ label = $VersionLabel; packagedAt = (Get-Date).ToUniversalTime().ToString('o') }
$provenance | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'build-provenance.json') -Encoding UTF8
Set-Content -LiteralPath (Join-Path $destination 'BUILD_INFO.txt') -Encoding UTF8 -Value @(
	"SFV Ember $($receipt.version)", "Release: $VersionLabel",
	"Source: $repo at $($receipt.sourceRevision)", "Source fingerprint: $($receipt.sourceFingerprint)",
	"Ember library: $emberRoot at $($receipt.emberRevision)", "Ember library fingerprint: $($receipt.emberFingerprint)",
	'See build-provenance.json for the binaries, the helper and the tests.')
Write-HashManifest $destination 'MANIFEST.txt'

# The updater's own rules on the folder, then on the zip as a download.
& $validator --validate $destination | Out-Host
if ($LASTEXITCODE) { throw "The updater's package rules refuse the folder ($LASTEXITCODE)" }
Compress-Archive -LiteralPath $destination -DestinationPath "$destination.zip"
& (Join-Path $emberRoot 'scripts\test-updater-download-validation.ps1') -Validator $validator -PackageZip "$destination.zip" | Out-Host
if ($LASTEXITCODE) { throw "Updater download validation failed ($LASTEXITCODE)" }
$hash = Write-Sha256Sidecar "$destination.zip"
Write-Output "SFV Ember package: $destination.zip ($hash)"
[pscustomobject]@{ zipPath = "$destination.zip"; folderPath = $destination; sha256 = $hash }
