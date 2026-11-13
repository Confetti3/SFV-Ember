# The packaging script's render store checks (scripts\RenderStore.ps1's
# Read-RenderStore) on small stores made here: a costume the pick space marks
# "unshipped" needs no picture and no manifest exception (one listed is
# accepted), a picture of it is refused, and an offered costume without its
# first colour still fails.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\RenderStoreTests.ps1
#
# Prints PASS or FAIL and exits 0 or 1. Writes only under the temporary folder.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Off
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\RenderStore.ps1')

$root = Join-Path ([IO.Path]::GetTempPath()) ('sfve-render-store-tests-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$failures = [Collections.Generic.List[string]]::new()

# One fighter, AAA: costume 0 (colours 0 and 1) offered, costume 3 unshipped;
# one V-Skill and one V-Trigger.
$spacePath = Join-Path $root 'pick-space.json'
@{
	build = 'sfv-ember/13173604/0.6/0000000000000000/layout-0000000000000000'
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
