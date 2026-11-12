# Packages SFV Ember from a verified build, locally: a folder and a zip under
# -OutDir (D:\sfv-ember-dist by default, never the C: drive), with the zip's
# .sha256 beside it. Nothing is published.
#
#   scripts\package.ps1 -RenderStore <dir> [-BuildDir D:\sfv-ember-build-player] [-OutDir D:\sfv-ember-dist] [-VersionLabel <label>]
#       [-MinColourCoverage 0] [-OldPackageCheck <sfve_package_check.exe of an earlier beta>] [-NoRenders]
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
# costume's first colour must be there (or a listed exception) and the other
# colours must reach -MinColourCoverage (default 0: beta.8 ships each
# costume's first colour only; the coverage is still reported). Every loadout
# picture (a V-Skill and V-Trigger of every fighter) is required too, or a
# listed exception. The files
# are staged under assets\selection\, the manifest beside them, the package's
# PackageInventory.inc lists each one (an installed updater of an earlier
# beta refuses files it does not know unless the package's own inventory
# does), and the provenance receipt gets a "renders" record: the manifest's
# hash, counts, bytes, coverage, pipeline and the capture's game build and mod
# revision. -OldPackageCheck <path> also validates the staged folder with a
# kept copy of an earlier beta's sfve_package_check.exe (beta.7's), which
# applies that release's compiled rules. -NoRenders makes a package without
# renders (a development or test package), with a warning, and the receipt
# says so.

param(
	[string]$BuildDir = 'D:\sfv-ember-build-player',
	[string]$OutDir = 'D:\sfv-ember-dist',
	[string]$VersionLabel = '',
	[string]$RenderStore = '',
	[ValidateRange(0.0, 1.0)][double]$MinColourCoverage = 0.0,
	[string]$OldPackageCheck = '',
	[switch]$NoRenders
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

# The render store's pack (beta.8): the captured cutouts and loadout pictures
# the mod draws on its pick screen (docs/design/beta8/pack.md holds the
# manifest's format). Everything is checked before anything is staged: the
# manifest's own fields, every file against its size and SHA-256, every path
# against the pick space (data\pick-space.json: the costumes and colours the
# fighter has, the loadout choices it has; the same rules as
# src\ember\SfvSelectionAssetPath.cxx, which the validator applies again),
# no file in the pack the manifest does not list, and the coverage the
# package must reach. Returns what staging and the receipt need.
function Read-RenderStore([string]$Store, [double]$MinCoverage) {
	$pack = Join-Path (Resolve-Path -LiteralPath $Store).Path 'pack'
	$manifestPath = Join-Path $pack 'manifest.json'
	if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "No render manifest at $manifestPath" }
	$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
	$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
	if ($manifest.format -ne 1) { throw "Render manifest format $($manifest.format) is not 1" }
	foreach ($field in 'gameBuild', 'modRevision', 'pipeline') {
		if ($manifest.$field -isnot [string] -or !$manifest.$field) { throw "Render manifest has no '$field' string" }
	}
	if ($manifest.modRevision -notmatch '^[0-9a-f]{7,40}$') { throw "Render manifest modRevision is not a git revision: $($manifest.modRevision)" }

	# The pick space this checkout embeds, and the game build it was exported from.
	$space = Get-Content -LiteralPath (Join-Path $repo 'data\pick-space.json') -Raw | ConvertFrom-Json
	$spaceBuild = ($space.build -split '/')[1]
	if ($manifest.gameBuild -ne $spaceBuild) { throw "The renders were captured on game build $($manifest.gameBuild); this checkout's pick space is build $spaceBuild" }
	# Every path a pick space allows, and every costume's first colour.
	$allowed = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
	$firstColours = [Collections.Generic.List[object]]::new()
	$lookCount = 0
	foreach ($fighter in $space.fighters) {
		foreach ($costume in $fighter.costumes) {
			foreach ($colour in $costume.colours) {
				$allowed["assets/selection/$($fighter.code)/costume-$($costume.id)/color-$colour-cutout.png"] = "cutout $($fighter.code) $($costume.id) $colour"
				$lookCount++
			}
			$firstColours.Add([pscustomobject]@{ Fighter = $fighter.code; Costume = [int]$costume.id; Colour = [int]$costume.colours[0] })
		}
		for ($choice = 1; $choice -le $fighter.v_skills; $choice++) { $allowed["assets/selection/$($fighter.code)/vs${choice}.png"] = "loadout $($fighter.code) vskill $choice" }
		for ($choice = 1; $choice -le $fighter.v_triggers; $choice++) { $allowed["assets/selection/$($fighter.code)/vt${choice}.png"] = "loadout $($fighter.code) vtrigger $choice" }
	}
	$loadoutTotal = @($allowed.Values | Where-Object { $_.StartsWith('loadout ') }).Count

	# Every file: listed once, a pick-space path, its look as the path says, and its bytes.
	$files = [Collections.Generic.List[object]]::new()
	$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$present = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$loadoutPresent = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	[long]$totalBytes = 0
	$cutouts = 0
	$loadouts = 0
	foreach ($entry in @($manifest.files)) {
		$path = [string]$entry.path
		$meaning = ''
		if (!$allowed.TryGetValue($path, [ref]$meaning)) { throw "Render manifest lists a path the pick space has no picture for: $path" }
		if (!$seen.Add($path)) { throw "Render manifest lists $path twice" }
		if ($entry.sha256 -isnot [string] -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw "Render manifest: no SHA-256 for $path" }
		if ($entry.bytes -isnot [int] -and $entry.bytes -isnot [long]) { throw "Render manifest: no byte count for $path" }
		if ($null -eq $entry.look -or $null -eq $entry.frame -or $entry.stage -isnot [string] -or !$entry.stage) { throw "Render manifest: $path lacks look, frame or stage" }
		$look = $entry.look
		$part = $meaning -split ' '
		if ($part[0] -eq 'cutout') {
			if ($look.fighter -ne $part[1] -or $look.costume -ne [int]$part[2] -or $look.colour -ne [int]$part[3]) { throw "Render manifest: the look of $path says $($look.fighter) $($look.costume) $($look.colour)" }
			$cutouts++
		} else {
			if ($look.fighter -ne $part[1] -or $look.part -ne $part[2] -or $look.choice -ne [int]$part[3]) { throw "Render manifest: the look of $path says $($look.fighter) $($look.part) $($look.choice)" }
			$loadouts++
		}
		$native = Join-Path $pack ($path -replace '/', '\')
		if (!(Test-Path -LiteralPath $native -PathType Leaf)) { throw "Render file missing from the pack: $path" }
		$item = Get-Item -LiteralPath $native
		if ($item.Length -ne [long]$entry.bytes) { throw "Render file is $($item.Length) bytes, its manifest says $($entry.bytes): $path" }
		if ($item.Length -le 0) { throw "Render file is empty: $path" }
		if ((Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash -ne $entry.sha256.ToUpperInvariant()) { throw "Render file does not match its manifest hash: $path" }
		$totalBytes += $item.Length
		if ($part[0] -eq 'cutout') { $null = $present.Add("$($part[1]) $($part[2]) $($part[3])") } else { $null = $loadoutPresent.Add("$($part[1]) $($part[2]) $($part[3])") }
		$files.Add([pscustomobject]@{ Path = $path; Native = $native; Relative = ($path -replace '/', '\') })
	}
	# Nothing in the pack the manifest does not vouch for.
	$root = Join-Path $pack 'assets\selection'
	if (Test-Path -LiteralPath $root) {
		foreach ($stray in Get-ChildItem -LiteralPath $root -File -Recurse) {
			$relative = 'assets/selection/' + ($stray.FullName.Substring($root.Length + 1) -replace '\\', '/')
			if (!$seen.Contains($relative)) { throw "The pack holds a file its manifest does not list: $relative" }
		}
	}

	# Coverage: every costume's first colour, or a listed exception; the other colours by -MinColourCoverage.
	$excepted = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$loadoutExcepted = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($exception in @($manifest.exceptions)) {
		if ($null -eq $exception) { continue }
		if (!$exception.reason -or $exception.reason -isnot [string]) { throw "Render manifest: an exception for $($exception.fighter) has no reason" }
		if ($null -ne $exception.part) {
			# A loadout picture: fighter, part ("vskill" or "vtrigger") and choice (1 or 2).
			$key = "$($exception.fighter) $($exception.part) $($exception.choice)"
			$suffix = if ($exception.part -eq 'vskill') { 'vs' } elseif ($exception.part -eq 'vtrigger') { 'vt' } else { '' }
			if (!$suffix -or !$allowed.ContainsKey("assets/selection/$($exception.fighter)/$suffix$($exception.choice).png")) { throw "Render manifest: an exception names a loadout picture the pick space lacks: $key" }
			if ($loadoutPresent.Contains($key)) { throw "Render manifest: $key is both excepted and rendered" }
			$null = $loadoutExcepted.Add($key)
			continue
		}
		$first = $firstColours | Where-Object { $_.Fighter -eq $exception.fighter -and $_.Costume -eq $exception.costume } | Select-Object -First 1
		if (!$first) { throw "Render manifest: an exception names a costume the pick space lacks: $($exception.fighter) $($exception.costume)" }
		$colour = if ($null -ne $exception.colour) { [int]$exception.colour } else { $first.Colour }
		$key = "$($exception.fighter) $($exception.costume) $colour"
		if (!$allowed.ContainsKey("assets/selection/$($exception.fighter)/costume-$($exception.costume)/color-$colour-cutout.png")) { throw "Render manifest: an exception names a colour the costume lacks: $key" }
		if ($present.Contains($key)) { throw "Render manifest: $key is both excepted and rendered" }
		$null = $excepted.Add($key)
	}
	$missing = @($firstColours | Where-Object { $k = "$($_.Fighter) $($_.Costume) $($_.Colour)"; !$present.Contains($k) -and !$excepted.Contains($k) } |
		ForEach-Object { "$($_.Fighter) costume $($_.Costume) colour $($_.Colour)" })
	if ($missing.Count) {
		throw "The renders lack the first colour of $($missing.Count) costume(s) (list them as manifest exceptions or capture them): $((@($missing | Select-Object -First 20)) -join '; ')$(if ($missing.Count -gt 20) { '; ...' })"
	}
	$wanted = $lookCount - $excepted.Count
	$coverage = if ($wanted -gt 0) { $cutouts / $wanted } else { 1.0 }
	if ($coverage + 1e-9 -lt $MinCoverage) {
		throw ("Colour coverage is {0:P2} ({1} of {2} looks, {3} excepted); -MinColourCoverage asks for {4:P2}" -f $coverage, $cutouts, $wanted, $excepted.Count, $MinCoverage)
	}
	$loadoutMissing = @($allowed.Values | Where-Object { $_.StartsWith('loadout ') } | ForEach-Object { $_.Substring(8) } | Where-Object { !$loadoutPresent.Contains($_) -and !$loadoutExcepted.Contains($_) })
	if ($loadoutMissing.Count) {
		throw "The renders lack $($loadoutMissing.Count) of $loadoutTotal loadout picture(s) (list them as manifest exceptions or capture them): $((@($loadoutMissing | Select-Object -First 20)) -join '; ')$(if ($loadoutMissing.Count -gt 20) { '; ...' })"
	}
	# The updater's zip reader has no zip64 and a 16-bit entry count.
	if ($files.Count + 200 -gt 60000 -or $totalBytes -gt 3GB) { throw "The renders ($($files.Count) files, $totalBytes bytes) do not fit the updater's zip reader (no zip64, 16-bit entry count)" }
	[pscustomobject]@{
		Pack = $pack; ManifestPath = $manifestPath; ManifestHash = $manifestHash; Manifest = $manifest; Files = $files
		Cutouts = $cutouts; Loadouts = $loadouts; LoadoutTotal = $loadoutTotal; Looks = $lookCount; Excepted = $excepted.Count; LoadoutExcepted = $loadoutExcepted.Count
		Coverage = $coverage; Bytes = $totalBytes
	}
}

$renders = $null
if ($RenderStore) {
	if ($NoRenders) { throw '-RenderStore and -NoRenders contradict each other' }
	$renders = Read-RenderStore $RenderStore $MinColourCoverage
	Write-Output ('Renders: {0} cutouts, {1} of {2} loadout pictures, {3:P2} colour coverage, {4:N1} MB, manifest {5}' -f $renders.Cutouts, $renders.Loadouts, $renders.LoadoutTotal, $renders.Coverage, ($renders.Bytes / 1MB), $renders.ManifestHash)
} elseif ($NoRenders) {
	Write-Warning 'Packaging without renders (-NoRenders): the pick screen draws the game''s own pictures only. Not for players.'
} else {
	throw 'A player package needs the render store: pass -RenderStore <dir> (or -NoRenders for a development package without renders)'
}
if ($OldPackageCheck -and !(Test-Path -LiteralPath $OldPackageCheck -PathType Leaf)) { throw "No earlier sfve_package_check at $OldPackageCheck" }

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
& python (Join-Path $emberRoot 'scripts\collect-notices.py') --build-dir $BuildDir --output (Join-Path $destination 'notices\THIRD_PARTY_LICENSES.txt') `
	--product 'SFV Ember' --native cli11 detours fmt ggpo imgui minhook nlohmann-json spdlog xxhash zlib | Out-Host
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
	$lines.Add('// the captured selection pictures, listed so an installed updater accepts them.')
	$lines.Add('SF4E_PACKAGE_OPTIONAL("assets\\selection\\manifest.json")')
	foreach ($render in @($renders.Files | Sort-Object Path)) {
		$lines.Add('SF4E_PACKAGE_OPTIONAL("' + $render.Relative.Replace('\', '\\') + '")')
	}
	$text = [IO.File]::ReadAllText($inventorySource)
	if (!$text.EndsWith("`n")) { $text += "`r`n" }
	$generated = Join-Path $destination 'PackageInventory.inc'
	[IO.File]::WriteAllText($generated, $text + ($lines -join "`r`n") + "`r`n", [Text.UTF8Encoding]::new($false))
} else {
	Stage $inventorySource 'PackageInventory.inc'
}
if ($renders) {
	Stage $renders.ManifestPath 'assets\selection\manifest.json'
	foreach ($render in $renders.Files) { Stage $render.Native $render.Relative }
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
		loadoutPicturesExcepted = $renders.LoadoutExcepted
		bytes = $renders.Bytes
		colourCoverage = [math]::Round($renders.Coverage, 6)
		minColourCoverage = $MinColourCoverage
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
		"Captured on game build $($renders.Manifest.gameBuild) with mod revision $($renders.Manifest.modRevision)")
}
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
Write-Output "SFV Ember package: $destination.zip ($hash)"
[pscustomobject]@{ zipPath = "$destination.zip"; folderPath = $destination; sha256 = $hash }
