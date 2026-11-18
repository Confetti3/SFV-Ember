# The colour pack (docs/design/COLOUR_PACK.md) for scripts\package.ps1 (dot-sourced), in a file of its own so
# that tests\ColourPackTests.ps1 runs it on small stores. Takes Read-RenderStore's result (RenderStore.ps1).
#
# The zip ships each offered costume's first colour and every loadout picture; every other colour goes into one
# archive the launcher downloads on first run. The archive is content-addressed: stored entries in ordinal order,
# every time stamp 1980-01-01, so the same pictures make the same bytes, and it is named by its SHA-256. The
# public repository's release $ColourPackTag hosts every pack; the descriptor (colour-pack.json, at the
# package's top) names the one this package wants.

# The release that hosts the packs. The asset name must not start with the package's "sfv-ember-": the updater
# takes any asset with that prefix for the package.
$ColourPackTag = 'colour-packs'
function Get-ColourPackName([string]$Id) { "sfve-colours-$Id.zip" }
function Get-ColourPackUrl([string]$Repo, [string]$Name) { "https://github.com/$Repo/releases/download/$ColourPackTag/$Name" }

# The store's files split between the zip (first colours and loadout pictures) and the pack (the other colours).
function Split-RenderFiles([object]$Renders) {
	[pscustomobject]@{
		Zip = @($Renders.Files | Where-Object { $_.Kind -cne 'colour' })
		Pack = @($Renders.Files | Where-Object { $_.Kind -ceq 'colour' })
	}
}

# The store's manifest listing only $Files (the zip's or the pack's), as UTF-8 bytes: the zip and the pack each
# carry the manifest of what they hold.
function Get-RenderManifestBytes([object]$Manifest, [object[]]$Files) {
	$keep = [Collections.Generic.HashSet[string]]::new([string[]]@($Files | ForEach-Object { $_.Path }), [StringComparer]::Ordinal)
	$copy = [ordered]@{}
	foreach ($property in $Manifest.PSObject.Properties) { $copy[$property.Name] = $property.Value }
	$copy.files = @($Manifest.files | Where-Object { $keep.Contains([string]$_.path) })
	[Text.UTF8Encoding]::new($false).GetBytes(($copy | ConvertTo-Json -Depth 10) + "`n")
}

# Writes the pack of $Files under $OutDir: manifest.json, then each file at its store path. Returns what the
# descriptor and the receipt name: Id, Name, Path, Bytes, Sha256, Url (on $Repo's $ColourPackTag release), Cutouts.
function New-ColourPack([object]$Renders, [object[]]$Files, [string]$OutDir, [string]$Repo) {
	if (!$Files.Count) { throw 'A colour pack needs at least one picture.' }
	if ($Repo -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw "Not a GitHub repository (owner/name): $Repo" }
	Add-Type -AssemblyName System.IO.Compression
	$bySource = @{}
	foreach ($file in $Files) { $bySource[$file.Path] = $file.Native }
	$names = [string[]]@($bySource.Keys)
	[Array]::Sort($names, [StringComparer]::Ordinal)
	$stamp = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
	$temp = Join-Path $OutDir ('colour-pack-' + [Guid]::NewGuid().ToString('N') + '.zip')
	$stream = [IO.File]::Open($temp, [IO.FileMode]::CreateNew)
	try {
		$zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
		try {
			$add = {
				param([string]$Name, [byte[]]$Bytes)
				$entry = $zip.CreateEntry($Name, [IO.Compression.CompressionLevel]::NoCompression)
				$entry.LastWriteTime = $stamp
				$out = $entry.Open()
				try { $out.Write($Bytes, 0, $Bytes.Length) } finally { $out.Dispose() }
			}
			& $add 'manifest.json' (Get-RenderManifestBytes $Renders.Manifest $Files)
			foreach ($name in $names) { & $add $name ([IO.File]::ReadAllBytes($bySource[$name])) }
		} finally { $zip.Dispose() }
	} finally { $stream.Dispose() }
	$sha = (Get-FileHash -LiteralPath $temp -Algorithm SHA256).Hash.ToLowerInvariant()
	$id = $sha.Substring(0, 16)
	$name = Get-ColourPackName $id
	$path = Join-Path $OutDir $name
	# The same pictures made the same bytes: an earlier package's copy of this pack is this file.
	Move-Item -LiteralPath $temp -Destination $path -Force
	[pscustomobject]@{
		Id = $id; Name = $name; Path = $path; Bytes = (Get-Item -LiteralPath $path).Length; Sha256 = $sha
		Url = (Get-ColourPackUrl $Repo $name); Cutouts = $Files.Count
	}
}

# The descriptor the launcher and the game read (src/ember/SfvColourPack.hxx).
function Write-ColourPackDescriptor([object]$Pack, [string]$Path) {
	$text = ([ordered]@{ format = 1; id = $Pack.Id; bytes = $Pack.Bytes; sha256 = $Pack.Sha256; url = $Pack.Url } | ConvertTo-Json) + "`n"
	[IO.File]::WriteAllText($Path, $text, [Text.UTF8Encoding]::new($false))
}
