# The render store's checks for scripts\package.ps1 (dot-sourced), in a file
# of their own so that tests\RenderStoreTests.ps1 runs them on small stores.
# Needs nothing but its arguments.

# The render store's pack (beta.8): the captured cutouts and loadout pictures
# the mod draws on its pick screen (docs/design/beta8/pack.md holds the
# manifest's format). Everything is checked before anything is staged: the
# manifest's own fields, every file against its size and SHA-256, every path
# against the pick space (`$SpacePath`, the checkout's data\pick-space.json:
# the costumes and colours the fighter has, the loadout choices it has; the
# same rules as src\ember\SfvSelectionAssetPath.cxx, which the validator
# applies again), no file in the pack the manifest does not list, and the
# coverage the package must reach. A costume the pick space marks
# "unshipped" (the slot the game lists but ships nothing for, which the pick
# screens never offer) is left out: no picture of it is wanted or taken, and
# a manifest exception naming it is accepted and counted, no longer needed.
# Returns what staging and the receipt need.
function Read-RenderStore([string]$Store, [double]$MinCoverage, [string]$SpacePath) {
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
	$space = Get-Content -LiteralPath $SpacePath -Raw | ConvertFrom-Json
	$spaceBuild = ($space.build -split '/')[1]
	if ($manifest.gameBuild -ne $spaceBuild) { throw "The renders were captured on game build $($manifest.gameBuild); this checkout's pick space is build $spaceBuild" }
	# Every path a pick space allows, and every offered costume's first colour.
	$allowed = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
	$firstColours = [Collections.Generic.List[object]]::new()
	$unshipped = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$lookCount = 0
	foreach ($fighter in $space.fighters) {
		foreach ($costume in $fighter.costumes) {
			if ($costume.unshipped -eq $true) {
				$null = $unshipped.Add("$($fighter.code) $($costume.id)")
				continue
			}
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
		if (!$allowed.TryGetValue($path, [ref]$meaning)) {
			if ($path -match '^assets/selection/([^/]+)/costume-(\d+)/' -and $unshipped.Contains("$($Matches[1]) $($Matches[2])")) {
				throw "Render manifest lists a picture of a costume the game does not ship (the pick space marks it unshipped): $path"
			}
			throw "Render manifest lists a path the pick space has no picture for: $path"
		}
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
	$unshippedExcepted = 0
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
		# An unshipped costume needs no exception any more; one listed is accepted.
		if ($unshipped.Contains("$($exception.fighter) $($exception.costume)")) { $unshippedExcepted++; continue }
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
		throw "The renders lack the first colour of $($missing.Count) offered costume(s) (list them as manifest exceptions or capture them): $((@($missing | Select-Object -First 20)) -join '; ')$(if ($missing.Count -gt 20) { '; ...' })"
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
		Unshipped = $unshipped.Count; UnshippedExcepted = $unshippedExcepted
		Coverage = $coverage; Bytes = $totalBytes
	}
}
