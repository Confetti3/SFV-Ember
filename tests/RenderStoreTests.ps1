# The packaging script's render store checks (scripts\RenderStore.ps1's
# Read-RenderStore) on small stores made here: a costume the pick space marks
# "unshipped" needs no picture and no manifest exception (one listed is
# accepted), a picture of it is refused, and an offered costume without its
# first colour still fails. Then the split between the zip and the colour pack
# (scripts\ColourPack.ps1): by default the zip keeps every picture; with
# -ColourPack the first colours and loadout pictures stay in the zip, the
# other colours make one archive whose bytes depend on its pictures alone, and
# the descriptor names it.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\RenderStoreTests.ps1
#
# Prints PASS or FAIL and exits 0 or 1. Writes only under the temporary folder.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Off
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\RenderStore.ps1')
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\ColourPack.ps1')

$root = Join-Path ([IO.Path]::GetTempPath()) ('sfve-render-store-tests-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$failures = [Collections.Generic.List[string]]::new()

# One fighter, AAA: costume 0 (colours 0 and 1) offered, costume 3 unshipped;
# one V-Skill and one V-Trigger.
$spacePath = Join-Path $root 'pick-space.json'
@{
	build = 'sfv-ember/13173604/0.8/0000000000000000/layout-0000000000000000'
	fighters = @(@{
		code = 'AAA'
		costumes = @(@{ id = 0; colours = @(0, 1) }, @{ id = 3; colours = @(0); unshipped = $true })
		v_skills = 1
		v_triggers = 1
	})
	stages = @('S01')
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $spacePath -Encoding ASCII

# A store under $root\<name>\pack: each picture's file and manifest entry, and
# the manifest's exceptions.
function New-Store([string]$Name, [object[]]$Pictures, [object[]]$Exceptions) {
	$pack = Join-Path (Join-Path $root $Name) 'pack'
	$entries = @()
	foreach ($picture in $Pictures) {
		$native = Join-Path $pack ($picture.Path -replace '/', '\')
		$null = New-Item -ItemType Directory -Path (Split-Path -Parent $native) -Force
		[IO.File]::WriteAllBytes($native, [byte[]](1, 2, 3, 4))
		$entries += [ordered]@{
			path = $picture.Path
			sha256 = (Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash.ToLowerInvariant()
			bytes = 4
			look = $picture.Look
			frame = 0
			stage = 'TRN'
		}
	}
	$null = New-Item -ItemType Directory -Path $pack -Force
	[ordered]@{
		format = 1; gameBuild = '13173604'; modRevision = 'abcdef1'; pipeline = 'test'
		files = $entries
		exceptions = @($Exceptions)
	} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $pack 'manifest.json') -Encoding ASCII
	Join-Path $root $Name
}

$cutout = @{ Path = 'assets/selection/AAA/costume-0/color-0-cutout.png'; Look = @{ fighter = 'AAA'; costume = 0; colour = 0 } }
$skill = @{ Path = 'assets/selection/AAA/vs1.png'; Look = @{ fighter = 'AAA'; part = 'vskill'; choice = 1 } }
$trigger = @{ Path = 'assets/selection/AAA/vt1.png'; Look = @{ fighter = 'AAA'; part = 'vtrigger'; choice = 1 } }
$grey = @{ Path = 'assets/selection/AAA/costume-3/color-0-cutout.png'; Look = @{ fighter = 'AAA'; costume = 3; colour = 0 } }
$second = @{ Path = 'assets/selection/AAA/costume-0/color-1-cutout.png'; Look = @{ fighter = 'AAA'; costume = 0; colour = 1 } }

function Expect([bool]$Condition, [string]$Failure) { if (!$Condition) { $failures.Add($Failure) } }
# The paths a manifest's bytes list.
function Get-ListedPaths([byte[]]$Bytes) { @(([Text.Encoding]::UTF8.GetString($Bytes) | ConvertFrom-Json).files | ForEach-Object { $_.path }) }

# The checks' answers are the functions' only output; their lines go to the host.
function Expect-Pass([string]$Case, [scriptblock]$Check) {
	try {
		$result = & $Check
		Write-Host "ok   $Case"
		$result
	} catch {
		$failures.Add("$Case threw: $($_.Exception.Message)")
		Write-Host "FAIL $Case"
	}
}

function Expect-Throw([string]$Case, [string]$Pattern, [scriptblock]$Check) {
	try {
		$null = & $Check
		$failures.Add("$Case did not throw")
		Write-Host "FAIL $Case"
	} catch {
		if ($_.Exception.Message -match $Pattern) { Write-Host "ok   $Case" }
		else { $failures.Add("$Case threw something else: $($_.Exception.Message)"); Write-Host "FAIL $Case" }
	}
}

try {
	# The unshipped costume needs neither a picture nor an exception.
	$store = New-Store 'bare' @($cutout, $skill, $trigger) @()
	$read = Expect-Pass 'an unshipped costume needs no picture and no exception' { Read-RenderStore $store 0.0 $spacePath }
	if ($read) {
		if ($read.Unshipped -ne 1) { $failures.Add("Unshipped is $($read.Unshipped), not 1") }
		if ($read.Cutouts -ne 1) { $failures.Add("Cutouts is $($read.Cutouts), not 1") }
		# The offered costume's two colours are the looks; the unshipped one's are not.
		if ($read.Looks -ne 2) { $failures.Add("Looks is $($read.Looks), not 2") }
	}

	# An exception left over for it is accepted, and counted.
	$store = New-Store 'excepted' @($cutout, $skill, $trigger) @(@{ fighter = 'AAA'; costume = 3; reason = 'drawn without textures' })
	$read = Expect-Pass 'an exception for an unshipped costume is accepted' { Read-RenderStore $store 0.0 $spacePath }
	if ($read -and $read.UnshippedExcepted -ne 1) { $failures.Add("UnshippedExcepted is $($read.UnshippedExcepted), not 1") }

	# A picture of it is refused.
	$store = New-Store 'grey' @($cutout, $grey, $skill, $trigger) @()
	Expect-Throw 'a picture of an unshipped costume is refused' 'does not ship' { Read-RenderStore $store 0.0 $spacePath }

	# An offered costume without its first colour still fails.
	$store = New-Store 'missing' @($skill, $trigger) @()
	Expect-Throw 'an offered costume without its first colour fails' 'lack the first colour of 1 offered costume' { Read-RenderStore $store 0.0 $spacePath }

	# Unless it is a listed exception.
	$store = New-Store 'missing-excepted' @($skill, $trigger) @(@{ fighter = 'AAA'; costume = 0; reason = 'not captured' })
	$null = Expect-Pass 'an offered costume listed as an exception passes' { Read-RenderStore $store 0.0 $spacePath }

	# A store of first colours only leaves nothing for a colour pack.
	$read = Read-RenderStore (New-Store 'first-only' @($cutout, $skill, $trigger) @()) 0.0 $spacePath
	$split = Split-RenderFiles $read -ColourPack
	Expect ($split.Zip.Count -eq 3 -and $split.Pack.Count -eq 0) "a first-colour store splits $($split.Zip.Count)/$($split.Pack.Count), not 3/0"

	$read = Expect-Pass 'a store of every colour reaches full coverage' { Read-RenderStore (New-Store 'every' @($cutout, $second, $skill, $trigger) @()) 1.0 $spacePath }
	if ($read) {
		# By default the zip keeps every picture and there is no pack.
		$split = Split-RenderFiles $read
		$zipPaths = @($split.Zip | ForEach-Object { $_.Path } | Sort-Object)
		Expect (($zipPaths -join ',') -ceq (@($cutout.Path, $second.Path, $skill.Path, $trigger.Path | Sort-Object) -join ',') -and $split.Pack.Count -eq 0) "by default the zip holds $($zipPaths -join ', ')"
		# -ColourPack: the zip keeps the first colour and the loadout pictures, the pack takes the other colour.
		$split = Split-RenderFiles $read -ColourPack
		$zipPaths = @($split.Zip | ForEach-Object { $_.Path } | Sort-Object)
		Expect (($zipPaths -join ',') -ceq (@($cutout.Path, $skill.Path, $trigger.Path | Sort-Object) -join ',')) "the zip holds $($zipPaths -join ', ')"
		Expect ($split.Pack.Count -eq 1 -and $split.Pack[0].Path -ceq $second.Path) 'the pack holds the second colour alone'
		# Each carries the store's manifest with its own files listed.
		$listed = Get-ListedPaths (Get-RenderManifestBytes $read.Manifest $split.Zip)
		Expect ((@($listed | Sort-Object) -join ',') -ceq ($zipPaths -join ',')) "the zip's manifest lists $($listed -join ', ')"

		$first = New-Item -ItemType Directory -Path (Join-Path $root 'out-1')
		$again = New-Item -ItemType Directory -Path (Join-Path $root 'out-2')
		$pack = Expect-Pass 'the colour pack is written' { New-ColourPack $read $split.Pack $first.FullName 'Owner/Repo' }
		$other = New-ColourPack $read $split.Pack $again.FullName 'Owner/Repo'
		if ($pack) {
			$sha = (Get-FileHash -LiteralPath $pack.Path -Algorithm SHA256).Hash.ToLowerInvariant()
			Expect ($pack.Sha256 -ceq $sha -and $pack.Id -ceq $sha.Substring(0, 16)) 'the pack id is the start of its SHA-256'
			Expect ($other.Sha256 -ceq $pack.Sha256) 'the same pictures make the same pack'
			Expect ($pack.Name -ceq "sfve-colours-$($pack.Id).zip" -and (Split-Path -Leaf $pack.Path) -ceq $pack.Name) "the pack is named $($pack.Name)"
			Expect (!$pack.Name.StartsWith('sfv-ember-')) 'the pack never has the package''s prefix'
			Expect ($pack.Url -ceq "https://github.com/Owner/Repo/releases/download/colour-packs/$($pack.Name)") "the pack's URL is $($pack.Url)"
			Expect ($pack.Bytes -eq (Get-Item -LiteralPath $pack.Path).Length -and $pack.Cutouts -eq 1) 'the pack''s size and count'
			Expect (@(Get-ChildItem -LiteralPath $first.FullName).Count -eq 1) 'nothing but the pack is left in the folder'
			Add-Type -AssemblyName System.IO.Compression.FileSystem
			$archive = [IO.Compression.ZipFile]::OpenRead($pack.Path)
			try {
				$names = @($archive.Entries | ForEach-Object { $_.FullName })
				Expect (($names -join ',') -ceq "manifest.json,$($second.Path)") "the pack's entries are $($names -join ', ')"
				Expect (@($archive.Entries | Where-Object { $_.CompressedLength -ne $_.Length }).Count -eq 0) 'the pack''s entries are stored'
				Expect (@($archive.Entries | Where-Object { $_.LastWriteTime.Year -ne 1980 }).Count -eq 0) 'the pack''s entries carry no time of their own'
				$stream = $archive.GetEntry('manifest.json').Open(); $memory = [IO.MemoryStream]::new()
				try { $stream.CopyTo($memory) } finally { $stream.Dispose() }
				$listed = Get-ListedPaths $memory.ToArray()
				Expect (($listed -join ',') -ceq $second.Path) "the pack's manifest lists $($listed -join ', ')"
			} finally { $archive.Dispose() }
			# The descriptor the launcher and the game read.
			$descriptorPath = Join-Path $root 'colour-pack.json'
			Write-ColourPackDescriptor $pack $descriptorPath
			$descriptor = Get-Content -LiteralPath $descriptorPath -Raw | ConvertFrom-Json
			Expect ($descriptor.format -eq 1 -and $descriptor.id -ceq $pack.Id -and $descriptor.bytes -eq $pack.Bytes -and
				$descriptor.sha256 -ceq $pack.Sha256 -and $descriptor.url -ceq $pack.Url) 'the descriptor names the pack'
		}
		Expect-Throw 'a pack of nothing is refused' 'at least one picture' { New-ColourPack $read @() $first.FullName 'Owner/Repo' }
		Expect-Throw 'a pack for no repository is refused' 'Not a GitHub repository' { New-ColourPack $read $split.Pack $first.FullName 'not a repo' }
	}
} finally {
	Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	$failures | ForEach-Object { Write-Output "  $_" }
	Write-Output 'FAIL'
	exit 1
}
Write-Output 'PASS'
exit 0
