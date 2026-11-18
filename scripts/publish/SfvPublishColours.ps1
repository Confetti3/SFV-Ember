#Requires -Version 7
# The colour pack in the publish pipeline (docs/design/COLOUR_PACK.md): with package.ps1 -ColourPack, every colour of
# every costume but the first, one archive beside the zip that the launcher downloads on first run; by default the zip
# carries every colour and there is no pack. The build stage checks the pack the build PC made against the descriptor
# and receipt inside the zip; promote's colours phase puts it on the public repository's colour-packs release, once
# per pack: a release whose pack is unchanged names an asset that is there already, and nothing is uploaded. Packs of
# earlier releases stay, for the installs that still want them. A release that names a pack is never published before
# its colours phase is done. Dot-sourced by SfvPublish.ps1.

# The release that hosts every pack (scripts/ColourPack.ps1's $ColourPackTag): a pre-release that is no version, so
# the updater and this pipeline's version checks pass over it, and never the repository's latest.
$PubColourPackTag = 'colour-packs'
$PubColourPackName = '^sfve-colours-[0-9a-f]{16}\.zip$'

# The pack the build PC's job made, by the name its result.json records; $null when it made none.
function Get-PubColourPackName([object]$Result) {
    if ($null -eq $Result.colourPack) { return $null }
    $name = [string]$Result.colourPack.name
    if ($name -cnotmatch $PubColourPackName) { throw "The laptop job names no colour pack ('$name')." }
    $name
}

# The pack in $Folder against what the zip says of it: the descriptor (colour-pack.json: id, size, SHA-256, and the
# URL on this pipeline's public repository's colour-packs release) and the receipt's renders.colourPack; every entry
# scanned for the forbidden strings, as the package's are; and the zip's Launcher.exe, which refuses a descriptor of
# any other repository than its own (SfvProduct.cxx's stableRepo), names that repository. Returns {name, sha256,
# size, url}, or $null when the job, the descriptor and the receipt all name no pack (the zip carries every colour).
function Assert-PubColourPack([object]$Ctx, [string]$Folder, [object]$Result, [byte[]]$DescriptorBytes, [object]$Packaged, [byte[]]$LauncherBytes, [object[]]$Rules) {
    $name = Get-PubColourPackName $Result
    $named = @(@($name, $DescriptorBytes, $Packaged.renders.colourPack) | Where-Object { $null -ne $_ }).Count
    if (!$named) { return $null }
    if ($named -ne 3) { throw "The laptop job, the zip's colour-pack.json and its receipt do not agree on a colour pack (job: '$name')." }
    if (![Text.Encoding]::Latin1.GetString($LauncherBytes).Contains("$($Ctx.Config.publicRepo)")) {
        throw "The zip's Launcher.exe does not name $($Ctx.Config.publicRepo), so it would refuse the colour pack's URL there."
    }
    $path = Join-Path $Folder $name
    $sha = Get-PubSha256 $path
    $size = (Get-Item -LiteralPath $path).Length
    if ([string]$Result.colourPack.sha256 -cne $sha) { throw 'The colour pack that came back is not the one the laptop made.' }
    if ($name -cne "sfve-colours-$($sha.Substring(0, 16)).zip") { throw "The colour pack $name is not named by its SHA-256 ($sha)." }
    $url = "https://github.com/$($Ctx.Config.publicRepo)/releases/download/$PubColourPackTag/$name"
    $descriptor = [Text.UTF8Encoding]::new($false).GetString($DescriptorBytes).TrimStart([char]0xFEFF) | ConvertFrom-Json
    if ($descriptor.format -ne 1 -or [string]$descriptor.id -cne $sha.Substring(0, 16) -or [string]$descriptor.sha256 -cne $sha -or
        [long]$descriptor.bytes -ne $size -or [string]$descriptor.url -cne $url) {
        throw "The zip's colour-pack.json does not name the colour pack $name ($size bytes, SHA-256 $sha) at $url."
    }
    $receipt = $Packaged.renders.colourPack
    if ([string]$receipt.name -cne $name -or [string]$receipt.sha256 -cne $sha) { throw "The zip's receipt names another colour pack ($($receipt.name))." }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($path)
    try { $scan = Find-PubArchiveHits @($archive.Entries) $Rules 'colour pack' } finally { $archive.Dispose() }
    Assert-PubNoHits $scan.hits 'The colour pack'
    @{name = $name; sha256 = $sha; size = $size; url = $url}
}

# The colour-packs release of the public repository, or $null when it has none yet.
function Get-PubColourStore([object]$Ctx) {
    $stores = @(Get-PubReleases $Ctx | Where-Object { $_.tag_name -ceq $PubColourPackTag })
    if ($stores.Count -gt 1) { throw "$($Ctx.Config.publicRepo) has $($stores.Count) releases named $PubColourPackTag; keep the published one." }
    if (!$stores.Count) { return $null }
    if ($stores[0].draft -ne $false) { throw "$($Ctx.Config.publicRepo)'s $PubColourPackTag release $($stores[0].id) is a draft, whose assets nobody can download; publish or delete it." }
    $stores[0]
}
# The asset $Name of release $Id, or $null.
function Get-PubColourAsset([object]$Ctx, [string]$Id, [string]$Name) {
    @((Get-PubReleaseById $Ctx $Id).assets | Where-Object { $_.name -ceq $Name }) | Select-Object -First 1
}
function Assert-PubColourAsset([object]$Asset, [object]$Pack) {
    if (!$Asset) { throw "The $PubColourPackTag release has no $($Pack.name)." }
    if (($Asset.state -and $Asset.state -cne 'uploaded') -or [long]$Asset.size -ne [long]$Pack.size -or [string]$Asset.digest -cne "sha256:$($Pack.sha256)") {
        throw "The $PubColourPackTag release's $($Pack.name) is not the checked pack ($($Asset.size) bytes, digest $($Asset.digest), $($Asset.state)); delete that asset on GitHub and promote again."
    }
}

# Promote's colours phase: the colour-packs release, made when the repository has none, with the pack, uploaded
# unless an asset of its name is there; the hosted asset is checked by size and digest either way. Returns 'there',
# 'uploaded', 'skipped' or 'dry-run'.
function Publish-PubColourPack([object]$Ctx, [string]$Path, [object]$Pack) {
    $repo = $Ctx.Config.publicRepo
    if ((Get-PubSha256 $Path) -cne $Pack.sha256) { throw "$Path is not the checked colour pack." }
    $store = Get-PubColourStore $Ctx
    if ($store) {
        $there = Get-PubColourAsset $Ctx ([string]$store.id) $Pack.name
        if ($there) {
            Assert-PubColourAsset $there $Pack
            Write-PubLog $Ctx "colours: $($Pack.name) is on the $PubColourPackTag release already; nothing uploaded."
            return 'there'
        }
    }
    $outcome = Invoke-PubOutward $Ctx 'colours' "upload $($Pack.name) to $repo's $PubColourPackTag release$(if (!$store) { ', made now as a pre-release' })" {
        $id = if ($store) { [string]$store.id } else {
            $request = Join-Path (Get-PubStageDir $Ctx 'promote') 'colour-store-request.json'
            Write-PubText $request (ConvertTo-PubJsonText ([ordered]@{tag_name = $PubColourPackTag; target_commitish = 'main'; name = 'Colour packs'
                body = 'The outfit colours SFV Ember''s launcher downloads on its first start. Not a release of SFV Ember: install from the releases with a version.'
                draft = $false; prerelease = $true; make_latest = 'false'}))
            $created = ConvertTo-PubText (Invoke-PubGh $Ctx @('api', '-X', 'POST', "repos/$repo/releases", '--input', $request)) | ConvertFrom-Json
            if ([string]$created.id -cnotmatch '^[1-9][0-9]*$' -or $created.draft -ne $false) { throw "GitHub did not return a published $PubColourPackTag release with an id." }
            Write-PubLog $Ctx "colours: made the $PubColourPackTag release $($created.id)."
            [string]$created.id
        }
        Invoke-PubGh $Ctx @('api', '-X', 'POST', "https://uploads.github.com/repos/$repo/releases/$id/assets?name=$([uri]::EscapeDataString($Pack.name))",
            '-H', 'Content-Type: application/zip', '--input', $Path) | Out-Null
        Write-PubLog $Ctx "colours: uploaded $($Pack.name) to release $id."
        $id
    }
    if ($outcome.skipped) { return 'skipped' }
    if (!$outcome.done) { return 'dry-run' }
    Assert-PubColourAsset (Get-PubColourAsset $Ctx $outcome.result $Pack.name) $Pack
    'uploaded'
}

# Promote's confirm: the pack downloaded from the colour-packs release into $Download, as GitHub serves it.
function Confirm-PubColourPack([object]$Ctx, [string]$Download, [object]$Pack) {
    Invoke-PubGh $Ctx @('release', 'download', $PubColourPackTag, '--repo', $Ctx.Config.publicRepo, '--dir', $Download, '--pattern', $Pack.name) | Out-Null
    $file = Join-Path $Download $Pack.name
    if (!(Test-Path -LiteralPath $file) -or (Get-PubSha256 $file) -cne $Pack.sha256) { throw "The $PubColourPackTag release does not serve the checked $($Pack.name)." }
}
