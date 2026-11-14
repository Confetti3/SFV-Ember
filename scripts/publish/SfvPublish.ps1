#Requires -Version 7
# SFV Ember's publish pipeline: the stages and every check (docs/release/PUBLISHING.md). Dot-source it; nothing
# runs on import. scripts/publish.ps1 is the entry, SfvPublishOperations.ps1 holds the real commands (git, gh, ssh,
# scp) and tests/PublishTests.ps1 replaces them with fakes.
#
# The rules every stage keeps:
#   - plan.json is made once and read by every stage; it pins the label, the source commit and tree, the library
#     commit and tree, the room build, the notes and the rules files by SHA-256.
#   - A stage writes only into its own folder and leaves one evidence file there, written once and made read-only.
#     Its SHA-256 goes into ledger.jsonl (append-only) as it is written, and every later stage's evidence names the
#     SHA-256 of the plan and of each evidence it relied on. Every stage verifies the whole chain again before it
#     starts: a file replaced after its stage finished no longer matches the ledger or its successor.
#   - Doing a stage again (-Redo) moves that stage and every stage after it to superseded\<time>\ and says so in the
#     ledger; nothing is ever overwritten in place.
#   - Outward steps (VPS staging, the draft, pushes, publishing) go through Invoke-PubOutward: each is named, can be
#     skipped (-Skip), is logged in outward.log, and does nothing in a dry run.

$ErrorActionPreference = 'Stop'

$PubPlanSchema = 'sfv-publish-plan/1'
$PubEvidenceSchema = 'sfv-publish-evidence/1'
$PubStages = @('plan', 'build', 'rooms', 'draft', 'promote')
# What each stage relies on (its evidence names these by SHA-256).
$PubInputs = @{ build = @(); rooms = @(); draft = @('build', 'rooms'); promote = @('build', 'rooms', 'draft') }
$PubPromotePhases = @('verify', 'server', 'push-library', 'push-origin', 'push-public', 'publish', 'confirm')
$PubOutwardPromote = @('push-library', 'push-origin', 'push-public', 'publish')
$PubRoomBuildPattern = '^(sfv-ember/[0-9]+/[0-9]+\.[0-9]+)/([0-9a-f]{16})/layout-([0-9a-f]{16})$'

# ---------------------------------------------------------------------------------------------------------------
# Small helpers

function Get-PubSha256([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "No file at $Path" }
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Get-PubBytesSha256([byte[]]$Bytes) { [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant() }
function Get-PubTextSha256([string]$Text) { Get-PubBytesSha256 ([Text.UTF8Encoding]::new($false).GetBytes($Text)) }
function ConvertTo-PubJsonText([object]$Value) { (($Value | ConvertTo-Json -Depth 30) -replace "`r`n", "`n") + "`n" }
function Read-PubJson([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "No file at $Path" }
    [IO.File]::ReadAllText($Path) | ConvertFrom-Json
}
function Write-PubText([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Get-PubNow([object]$Ctx) { (& $Ctx.Ops.Now).ToUniversalTime().ToString('o') }
function Write-PubLog([object]$Ctx, [string]$Text) {
    Write-Host $Text
    if ($Ctx.Root -and (Test-Path -LiteralPath $Ctx.Root)) {
        [IO.File]::AppendAllText((Join-Path $Ctx.Root 'publish.log'), "$(Get-PubNow $Ctx) $Text`n", [Text.UTF8Encoding]::new($false))
    }
}
function ConvertTo-PubText([object]$Result) { [Text.UTF8Encoding]::new($false).GetString([byte[]]$Result.bytes) }

# The label: 0.1.0-beta.9 and what follows from it.
function ConvertFrom-PubLabel([string]$Label) {
    if ($Label -cnotmatch '^([0-9]{1,4})\.([0-9]{1,4})\.([0-9]{1,4})-beta\.([1-9][0-9]{0,3})$') { throw "Not a beta label (0.1.0-beta.9): $Label" }
    $version = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
    [pscustomobject]@{
        label = $Label; tag = "v$Label"; version = $version; beta = [int]$Matches[4]
        releaseBranch = "release/beta.$($Matches[4])"; title = "SFV Ember $version Beta $($Matches[4])"
        zip = "sfv-ember-$Label.zip"; folder = "sfv-ember-$Label"
    }
}
# Orders release tags (v0.1.0-beta.8, v0.1.0): version, then a beta before the final. Returns -1, 0 or 1.
function Compare-PubTag([string]$Left, [string]$Right) {
    $key = {
        param($Tag)
        if ($Tag -cnotmatch '^v([0-9]+)\.([0-9]+)\.([0-9]+)(?:-beta\.([0-9]+))?$') { return $null }
        @([long]$Matches[1], [long]$Matches[2], [long]$Matches[3], $(if ($Matches[4]) { [long]$Matches[4] } else { [long]::MaxValue }))
    }
    $a = & $key $Left; $b = & $key $Right
    if ($null -eq $a -or $null -eq $b) { throw "Not a release tag: $Left / $Right" }
    for ($i = 0; $i -lt 4; $i++) { if ($a[$i] -ne $b[$i]) { return $(if ($a[$i] -lt $b[$i]) { -1 } else { 1 }) } }
    0
}

# ---------------------------------------------------------------------------------------------------------------
# Git through the operations (real git in the field and in the tests' fixture repositories; pushes are separate).

function Invoke-PubGit([object]$Ctx, [string]$Repo, [string[]]$Arguments, [byte[]]$Stdin = $null, [switch]$AllowFail) {
    $result = & $Ctx.Ops.Git $Repo $Arguments $Stdin
    if ($result.exitCode -and !$AllowFail) { throw "git $($Arguments -join ' ') failed in ${Repo}: $($result.stderr)" }
    $result
}
function Get-PubGitText([object]$Ctx, [string]$Repo, [string[]]$Arguments) { (ConvertTo-PubText (Invoke-PubGit $Ctx $Repo $Arguments)).Trim() }
function Resolve-PubCommit([object]$Ctx, [string]$Repo, [string]$Ref) {
    $sha = Get-PubGitText $Ctx $Repo @('rev-parse', '--verify', '--quiet', "$Ref^{commit}")
    if ($sha -cnotmatch '^[0-9a-f]{40}$') { throw "$Ref is not a commit in $Repo" }
    $sha
}
function Get-PubTree([object]$Ctx, [string]$Repo, [string]$Commit) { Get-PubGitText $Ctx $Repo @('rev-parse', "$Commit^{tree}") }
function Test-PubAncestor([object]$Ctx, [string]$Repo, [string]$Ancestor, [string]$Descendant) {
    (Invoke-PubGit $Ctx $Repo @('merge-base', '--is-ancestor', $Ancestor, $Descendant) -AllowFail).exitCode -eq 0
}
function Test-PubObject([object]$Ctx, [string]$Repo, [string]$Sha) { (Invoke-PubGit $Ctx $Repo @('cat-file', '-e', "$Sha^{commit}") -AllowFail).exitCode -eq 0 }
function Get-PubShow([object]$Ctx, [string]$Repo, [string]$Commit, [string]$Path) {
    ConvertTo-PubText (Invoke-PubGit $Ctx $Repo @('show', "${Commit}:$Path"))
}
# A remote ref's commit (ls-remote, read-only), peeled for tags; '' when the remote has no such ref.
function Get-PubRemoteRef([object]$Ctx, [string]$Repo, [string]$Remote, [string]$Ref) {
    $text = ConvertTo-PubText (Invoke-PubGit $Ctx $Repo @('ls-remote', $Remote, $Ref, "$Ref^{}"))
    $lines = @($text -split "`n" | Where-Object { $_.Trim() })
    $peeled = @($lines | Where-Object { $_ -cmatch "\s$([regex]::Escape($Ref))\^\{\}$" })
    $line = if ($peeled.Count) { $peeled[0] } else { @($lines | Where-Object { $_ -cmatch "\s$([regex]::Escape($Ref))$" })[0] }
    if (!$line) { return '' }
    ($line -split '\s+')[0]
}

# ---------------------------------------------------------------------------------------------------------------
# The forbidden strings: the owner's rules files (git filter-repo's replace-text format: regex:, literal:, glob: or a
# plain literal, then ==> and the replacement, which is ignored here). The patterns never appear in this code or in
# any message: a hit names the rules file and line only.

function Read-PubRules([string[]]$Paths, [string[]]$MachineNames = @()) {
    $rules = [Collections.Generic.List[object]]::new()
    if (!@($Paths | Where-Object { $_ }).Count) { throw 'Pass the rules file (-Rules): the forbidden strings live outside the repository.' }
    foreach ($path in $Paths) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "No rules file at $path" }
        $number = 0
        foreach ($line in [IO.File]::ReadAllLines($path)) {
            $number++
            if (!$line.Trim() -or $line.TrimStart().StartsWith('#')) { continue }
            $arrow = $line.IndexOf('==>')
            $spec = if ($arrow -ge 0) { $line.Substring(0, $arrow) } else { $line }
            $pattern = if ($spec.StartsWith('regex:')) { $spec.Substring(6) }
                elseif ($spec.StartsWith('literal:')) { [regex]::Escape($spec.Substring(8)) }
                elseif ($spec.StartsWith('glob:')) { [regex]::Escape($spec.Substring(5)).Replace('\*', '.*').Replace('\?', '.') }
                else { [regex]::Escape($spec) }
            if (!$pattern) { continue }
            $rules.Add([pscustomobject]@{id = "$(Split-Path $path -Leaf)#$number"; regex = [regex]::new($pattern, 'Compiled, CultureInvariant')})
        }
    }
    if (!$rules.Count) { throw 'The rules files hold no rule.' }
    # The scanning machine's own names (its user, profile folder and computer), read at run time.
    foreach ($name in @($MachineNames | Where-Object { $_ -and $_.Length -ge 3 } | Sort-Object -Unique)) {
        $rules.Add([pscustomobject]@{id = 'machine-name'; regex = [regex]::new([regex]::Escape($name), 'CultureInvariant')})
    }
    $rules
}
function Get-PubMachineNames { @($env:USERNAME, $env:COMPUTERNAME, (Split-Path -Leaf "$env:USERPROFILE")) | Where-Object { $_ } }
function Get-PubRulesFingerprint([string[]]$Paths) { @($Paths | ForEach-Object { Get-PubSha256 $_ }) }

# Hits of the rules in a byte array: its raw bytes (Latin-1, one char per byte) and UTF-16LE text at both byte
# alignments. Returns {rule, where, encoding, offset} per hit.
function Find-PubHits([object[]]$Rules, [byte[]]$Bytes, [string]$Where) {
    $hits = [Collections.Generic.List[object]]::new()
    if (!$Bytes -or !$Bytes.Length) { return @() }
    $views = @(@('raw', [Text.Encoding]::Latin1.GetString($Bytes)), @('utf16', [Text.Encoding]::Unicode.GetString($Bytes)))
    if ($Bytes.Length -gt 2) { $views += , @('utf16+1', [Text.Encoding]::Unicode.GetString($Bytes, 1, $Bytes.Length - 1)) }
    foreach ($view in $views) {
        foreach ($rule in $Rules) {
            $match = $rule.regex.Match($view[1])
            if ($match.Success) { $hits.Add([pscustomobject]@{rule = $rule.id; where = $Where; encoding = $view[0]; offset = $match.Index}) }
        }
    }
    @($hits)
}
function Assert-PubNoHits([object[]]$Hits, [string]$What, [string[]]$Allowed = @()) {
    $left = @($Hits | Where-Object { $_ -and "$($_.where)|$($_.rule)" -cnotin $Allowed })
    if ($left.Count) {
        $list = @($left | Select-Object -First 20 | ForEach-Object { "  $($_.rule) in $($_.where) ($($_.encoding) at $($_.offset))" }) -join "`n"
        throw "$What carries forbidden strings ($($left.Count) hit(s)); nothing public may carry them:`n$list"
    }
}

# Every object of a range of commits (each commit, tree and blob reachable from $Commit and not from $Exclude), and
# every blob of $Commit's tree (not with -NoTree: the library's tree is public already, only its new commits are scanned). Returns @{ commits = [{sha, author, committer, message}]; objects = n; hits = [...] }.
function Get-PubRangeScan([object]$Ctx, [string]$Repo, [string]$Commit, [string]$Exclude, [object[]]$Rules, [string]$Name, [switch]$NoTree) {
    $revArgs = @('rev-list', '--objects', $Commit) + @(if ($Exclude) { "^$Exclude" })
    $listed = @((ConvertTo-PubText (Invoke-PubGit $Ctx $Repo $revArgs)) -split "`n" | Where-Object { $_.Trim() })
    $objects = [Collections.Generic.Dictionary[string, string]]::new()
    foreach ($line in $listed) {
        $sha = $line.Substring(0, 40)
        $path = if ($line.Length -gt 41) { $line.Substring(41) } else { '' }
        if (!$objects.ContainsKey($sha)) { $objects[$sha] = $path }
    }
    $tree = if ($NoTree) { '' } else { ConvertTo-PubText (Invoke-PubGit $Ctx $Repo @('ls-tree', '-r', '--full-tree', $Commit)) }
    foreach ($line in @($tree -split "`n" | Where-Object { $_.Trim() })) {
        if ($line -cmatch '^[0-7]+ blob ([0-9a-f]{40})\t(.*)$' -and !$objects.ContainsKey($Matches[1])) { $objects[$Matches[1]] = $Matches[2] }
    }
    $batch = Invoke-PubGit $Ctx $Repo @('cat-file', '--batch') ([Text.Encoding]::ASCII.GetBytes((@($objects.Keys) -join "`n") + "`n"))
    $bytes = [byte[]]$batch.bytes
    $hits = [Collections.Generic.List[object]]::new()
    $commits = [Collections.Generic.List[object]]::new()
    $position = 0
    while ($position -lt $bytes.Length) {
        $end = [Array]::IndexOf($bytes, [byte]10, $position)
        $header = [Text.Encoding]::ASCII.GetString($bytes, $position, $end - $position)
        if ($header -cnotmatch '^([0-9a-f]{40}) ([a-z]+) ([0-9]+)$') { throw "Unexpected git cat-file output: $header" }
        $sha = $Matches[1]; $type = $Matches[2]; $size = [int]$Matches[3]
        $content = [byte[]]::new($size)
        [Array]::Copy($bytes, $end + 1, $content, 0, $size)
        $position = $end + 1 + $size + 1
        $where = "$Name $type $($sha.Substring(0, 12))$(if ($objects[$sha]) { " $($objects[$sha])" })"
        foreach ($hit in (Find-PubHits $Rules $content $where)) { $hits.Add($hit) }
        if ($objects[$sha]) { foreach ($hit in (Find-PubHits $Rules ([Text.UTF8Encoding]::new($false).GetBytes($objects[$sha])) "$Name path of $($sha.Substring(0, 12))")) { $hits.Add($hit) } }
        if ($type -ceq 'commit') {
            $text = [Text.UTF8Encoding]::new($false).GetString($content)
            $split = $text.IndexOf("`n`n")
            $head = $text.Substring(0, [Math]::Max(0, $split))
            $identity = { param($Kind) if ($head -cmatch "(?m)^$Kind (.+ <[^>]*>) [0-9]+ [-+][0-9]{4}$") { $Matches[1] } else { '' } }
            $commits.Add([pscustomobject]@{sha = $sha; author = (& $identity 'author'); committer = (& $identity 'committer')
                message = $(if ($split -ge 0) { $text.Substring($split + 2) } else { '' })})
        }
    }
    @{ commits = @($commits); objects = $objects.Count; hits = @($hits) }
}

# Commit messages may carry no trailer of the "<Something>-by:" kind (authorship and tool lines), and every author
# and committer must be one of the allowed identities.
function Assert-PubCommits([object[]]$Commits, [string[]]$AllowedIdentities, [string]$Name) {
    foreach ($commit in $Commits) {
        foreach ($who in @($commit.author, $commit.committer)) {
            if ($who -cnotin $AllowedIdentities) { throw "$Name commit $($commit.sha.Substring(0, 12)) has an identity that is not the release identity; rewrite it before publishing." }
        }
        if ($commit.message -cmatch '(?m)^[A-Za-z][A-Za-z-]*-[Bb][Yy]:') { throw "$Name commit $($commit.sha.Substring(0, 12)) carries a trailer line (<...>-by:); public commits carry none." }
    }
}

# ---------------------------------------------------------------------------------------------------------------
# State: the label's folder, the ledger and the evidence files.

function Get-PubStageDir([object]$Ctx, [string]$Stage) { Join-Path $Ctx.Root $Stage }
function Get-PubLedgerPath([object]$Ctx) { Join-Path $Ctx.Root 'ledger.jsonl' }
function Get-PubLedger([object]$Ctx) {
    $path = Get-PubLedgerPath $Ctx
    if (!(Test-Path -LiteralPath $path)) { return @() }
    @([IO.File]::ReadAllLines($path) | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
}
function Add-PubLedger([object]$Ctx, [hashtable]$Entry) {
    $Entry.at = Get-PubNow $Ctx
    $line = ($Entry | ConvertTo-Json -Compress -Depth 5) + "`n"
    $stream = [IO.File]::Open((Get-PubLedgerPath $Ctx), 'Append', 'Write', 'Read')
    try { $bytes = [Text.UTF8Encoding]::new($false).GetBytes($line); $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
}
# The SHA-256 the ledger holds for a stage's current evidence ('' when the stage has none, or it was superseded).
function Get-PubLedgerSha([object]$Ctx, [string]$Stage) {
    $last = @(Get-PubLedger $Ctx | Where-Object { $_.stage -ceq $Stage }) | Select-Object -Last 1
    if (!$last -or $last.superseded) { return '' }
    [string]$last.sha256
}
function Get-PubEvidencePath([object]$Ctx, [string]$Stage) {
    if ($Stage -ceq 'plan') { return Join-Path $Ctx.Root 'plan.json' }
    Join-Path (Get-PubStageDir $Ctx $Stage) 'evidence.json'
}
# Writes a stage's evidence once: refuses an existing file, records the SHA-256 in the ledger, makes it read-only.
function Write-PubEvidence([object]$Ctx, [string]$Stage, [object]$Document) {
    $path = Get-PubEvidencePath $Ctx $Stage
    if (Test-Path -LiteralPath $path) { throw "The $Stage evidence exists already ($path); it is never replaced (-Redo supersedes it)." }
    $text = ConvertTo-PubJsonText $Document
    Write-PubText $path $text
    (Get-Item -LiteralPath $path).Attributes = 'ReadOnly'
    $sha = Get-PubSha256 $path
    Add-PubLedger $Ctx @{stage = $Stage; file = (Split-Path $path -Leaf); sha256 = $sha}
    Write-PubLog $Ctx "$Stage evidence written: $sha"
    $sha
}
# The stage and every stage that relies on it (the plan: all of them).
function Get-PubDependents([string]$Stage) {
    if ($Stage -ceq 'plan') { return $PubStages }
    $set = [Collections.Generic.List[string]]::new()
    $set.Add($Stage)
    foreach ($candidate in $PubStages) {
        if ($candidate -cin $set) { continue }
        if (@($PubInputs[$candidate] | Where-Object { $_ -cin $set }).Count) { $set.Add($candidate) }
    }
    @($PubStages | Where-Object { $_ -cin $set })
}
# Moves a stage and every stage relying on it aside (superseded\<time>\) and records it in the ledger.
function Clear-PubStages([object]$Ctx, [string]$FromStage) {
    $stamp = (& $Ctx.Ops.Now).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $target = Join-Path $Ctx.Root "superseded\$stamp"
    foreach ($stage in (Get-PubDependents $FromStage)) {
        $sha = Get-PubLedgerSha $Ctx $stage
        $items = if ($stage -ceq 'plan') { @(Join-Path $Ctx.Root 'plan.json'; Join-Path $Ctx.Root 'notes.md') } else { @(Get-PubStageDir $Ctx $stage) }
        foreach ($item in $items) {
            if (Test-Path -LiteralPath $item) {
                New-Item -ItemType Directory -Path $target -Force | Out-Null
                Get-ChildItem -LiteralPath $item -Recurse -Force -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Attributes = 'Normal' }
                if ((Get-Item -LiteralPath $item) -is [IO.FileInfo]) { (Get-Item -LiteralPath $item).Attributes = 'Normal' }
                Move-Item -LiteralPath $item -Destination (Join-Path $target (Split-Path $item -Leaf))
            }
        }
        if ($sha) { Add-PubLedger $Ctx @{stage = $stage; superseded = $true; sha256 = $sha; movedTo = "superseded\$stamp"} }
    }
    Write-PubLog $Ctx "Superseded $FromStage and later stages (superseded\$stamp)."
}

# ---------------------------------------------------------------------------------------------------------------
# The plan

function Assert-PubPlanShape([object]$Plan) {
    if (!$Plan -or $Plan.schema -cne $PubPlanSchema) { throw "Not a publish plan ($PubPlanSchema)." }
    $label = ConvertFrom-PubLabel $Plan.label
    if ($Plan.tag -cne $label.tag -or $Plan.title -cne $label.title -or $Plan.releaseBranch -cne $label.releaseBranch) { throw 'The plan''s tag, title or branch is not its label''s.' }
    foreach ($sha in @($Plan.source.commit, $Plan.source.tree, $Plan.library.commit, $Plan.library.tree, $Plan.previous.commit)) {
        if ("$sha" -cnotmatch '^[0-9a-f]{40}$') { throw 'The plan names no full commit or tree somewhere.' }
    }
    if ($Plan.roomBuild.id -cnotmatch $PubRoomBuildPattern) { throw 'The plan names no SFV room build.' }
    if ($Plan.notes.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'The plan pins no release notes.' }
    $Plan
}
# plan.json as the ledger has it: its SHA-256 is the ledger's, and it is the label's plan.
function Read-PubPlan([object]$Ctx) {
    $path = Get-PubEvidencePath $Ctx 'plan'
    if (!(Test-Path -LiteralPath $path)) { throw "No plan for $($Ctx.Label): run -Stage plan first." }
    $sha = Get-PubSha256 $path
    $ledger = Get-PubLedgerSha $Ctx 'plan'
    if (!$ledger -or $ledger -cne $sha) { throw 'plan.json is not the file the plan stage wrote (the ledger names another SHA-256).' }
    $plan = Assert-PubPlanShape (Read-PubJson $path)
    if ($plan.label -cne $Ctx.Label) { throw "plan.json is for $($plan.label), not $($Ctx.Label)." }
    $notes = Join-Path $Ctx.Root 'notes.md'
    if (!(Test-Path -LiteralPath $notes) -or (Get-PubSha256 $notes) -cne $plan.notes.sha256) { throw 'notes.md is not the notes file the plan pinned.' }
    [pscustomobject]@{plan = $plan; sha256 = $sha}
}

# Release notes and the Discord draft: present, not empty, none of the words config.json's notesForbidden lists (an
# option public text never mentions), no forbidden string.
function Assert-PubNotesText([string]$Path, [object[]]$Rules, [string]$What, [string[]]$Words = @()) {
    if (!$Path) { throw "Pass the $What file: the pipeline never writes it." }
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "No $What at $Path" }
    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = [Text.UTF8Encoding]::new($false).GetString($bytes)
    if (!$text.Trim()) { throw "The $What is empty." }
    foreach ($word in @($Words | Where-Object { $_ })) {
        if ($text -match "(?i)\b$([regex]::Escape($word))\b") { throw "The $What mentions a word config.json's notesForbidden lists; public text never mentions it." }
    }
    Assert-PubNoHits (Find-PubHits $Rules $bytes $What) "The $What"
    $bytes
}

# Worktrees of $Repo whose HEAD is $Commit or whose branch is $Branch: each must be clean.
function Assert-PubCleanWorktrees([object]$Ctx, [string]$Repo, [string]$Commit, [string]$Branch) {
    $list = ConvertTo-PubText (Invoke-PubGit $Ctx $Repo @('worktree', 'list', '--porcelain'))
    $checked = 0
    foreach ($block in ($list -split "`n`n")) {
        if ($block -cnotmatch '(?m)^worktree (.+)$') { continue }
        $path = $Matches[1].Trim()
        $head = if ($block -cmatch '(?m)^HEAD ([0-9a-f]{40})') { $Matches[1] } else { '' }
        $branchLine = if ($block -cmatch '(?m)^branch refs/heads/(.+)$') { $Matches[1].Trim() } else { '' }
        if ($block -cmatch '(?m)^bare$' -or ($head -cne $Commit -and (!$Branch -or $branchLine -cne $Branch))) { continue }
        if (!(Test-Path -LiteralPath $path)) { continue }
        $status = ConvertTo-PubText (Invoke-PubGit $Ctx $path @('status', '--porcelain', '--untracked-files=normal'))
        if ($status.Trim()) { throw "The worktree $path ($(if ($branchLine) { $branchLine } else { 'detached' })) has uncommitted changes; commit or drop them before planning." }
        $checked++
    }
    $checked
}

# The plan stage. $P: Source, Library, Notes, Rules, DiscordDraft, AllowIdentity, Replan.
function Invoke-PubPlan([object]$Ctx, [hashtable]$P) {
    $label = ConvertFrom-PubLabel $Ctx.Label
    $cfg = $Ctx.Config
    New-Item -ItemType Directory -Path $Ctx.Root -Force | Out-Null
    $rules = Read-PubRules $P.Rules (Get-PubMachineNames)
    $repo = $Ctx.Repo
    if (!$P.Source) { throw 'Pass the source (-Source <branch or commit>).' }
    $commit = Resolve-PubCommit $Ctx $repo $P.Source
    $tree = Get-PubTree $Ctx $repo $commit
    Write-PubLog $Ctx "Plan $($label.label): source $($P.Source) = $commit (tree $tree)"

    # The release before this one: the newest published release of the public repository, by its tag.
    $releases = @(Get-PubReleases $Ctx)
    $published = @($releases | Where-Object { !$_.draft -and $_.tag_name -cmatch '^v[0-9]+\.[0-9]+\.[0-9]+(-beta\.[0-9]+)?$' })
    if (!$published.Count) { throw "$($cfg.publicRepo) has no published release to build on." }
    $previous = $published[0]
    foreach ($release in $published) { if ((Compare-PubTag $release.tag_name $previous.tag_name) -gt 0) { $previous = $release } }
    if ((Compare-PubTag $label.tag $previous.tag_name) -le 0) { throw "$($label.tag) is not newer than the newest published release $($previous.tag_name)." }
    if (@($releases | Where-Object { $_.tag_name -ceq $label.tag }).Count) { throw "$($cfg.publicRepo) already has a release or draft named $($label.tag)." }
    if (Get-PubRemoteRef $Ctx $repo $cfg.publicRemote "refs/tags/$($label.tag)") { throw "$($cfg.publicRemote) already has the tag $($label.tag)." }
    $previousCommit = Get-PubRemoteRef $Ctx $repo $cfg.publicRemote "refs/tags/$($previous.tag_name)"
    if ($previousCommit -cnotmatch '^[0-9a-f]{40}$') { throw "Could not read the tag $($previous.tag_name) from $($cfg.publicRemote)." }
    if (!(Test-PubObject $Ctx $repo $previousCommit)) { throw "The commit of $($previous.tag_name) ($previousCommit) is not in this repository; fetch it." }
    if (!(Test-PubAncestor $Ctx $repo $previousCommit $commit)) { throw "$commit does not descend from the last release $($previous.tag_name) ($previousCommit)." }
    $publicMain = Get-PubRemoteRef $Ctx $repo $cfg.publicRemote 'refs/heads/main'
    if ($publicMain -and (!(Test-PubObject $Ctx $repo $publicMain) -or !(Test-PubAncestor $Ctx $repo $publicMain $commit))) {
        throw "The public main ($publicMain) is not an ancestor of $commit (fetch $($cfg.publicRemote), or the release would not fast-forward it)."
    }
    $originBranch = Get-PubRemoteRef $Ctx $repo $cfg.originRemote "refs/heads/$($label.releaseBranch)"
    if ($originBranch -and $originBranch -cne $commit -and (!(Test-PubObject $Ctx $repo $originBranch) -or !(Test-PubAncestor $Ctx $repo $originBranch $commit))) {
        throw "$($cfg.originRemote) has $($label.releaseBranch) at $originBranch, which $commit does not fast-forward."
    }

    # Clean: the worktrees on this commit or branch, and the pipeline's own checkout (its commit goes in the plan).
    $sourceBranch = if ($P.Source -cnotmatch '^[0-9a-f]{7,40}$') { $P.Source -replace '^refs/heads/', '' } else { '' }
    $worktrees = Assert-PubCleanWorktrees $Ctx $repo $commit $sourceBranch
    $pipelineStatus = ConvertTo-PubText (Invoke-PubGit $Ctx $Ctx.PipelineRoot @('status', '--porcelain', '--untracked-files=normal'))
    if ($pipelineStatus.Trim() -and !$P.AllowDirtyPipeline -and !$Ctx.DryRun) { throw "The pipeline's checkout $($Ctx.PipelineRoot) has uncommitted changes; the plan records the pipeline's commit." }
    $pipelineCommit = Resolve-PubCommit $Ctx $Ctx.PipelineRoot 'HEAD'

    # The version the build will report, the room build and the library the README pins.
    $cmake = Get-PubShow $Ctx $repo $commit 'CMakeLists.txt'
    $projectVersion = if ($cmake -cmatch '(?m)^project\(sfv_ember VERSION ([0-9.]+)') { $Matches[1] } else { '' }
    $suffix = if ($cmake -cmatch '(?m)^set\(SFVE_VERSION "\$\{PROJECT_VERSION\}-([^"]+)"\)') { $Matches[1] } else { '' }
    if ("$projectVersion-$suffix" -cne $label.label) { throw "CMakeLists.txt at $commit makes version $projectVersion-$suffix, not $($label.label)." }
    $space = Get-PubShow $Ctx $repo $commit 'data/pick-space.json' | ConvertFrom-Json
    if ([string]$space.build -cnotmatch $PubRoomBuildPattern) { throw "data/pick-space.json names no SFV room build: $($space.build)" }
    $roomBuild = [pscustomobject]@{id = [string]$space.build; matchLine = $Matches[1]; pickHash = $Matches[2]; layoutId = $Matches[3]}
    $header = Get-PubShow $Ctx $repo $commit 'src/ember/SfvPickSpace.hxx'
    if ($header -cnotmatch 'kMatchBuild = "([^"]+)"' -or $Matches[1] -cne $roomBuild.matchLine) { throw "src/ember/SfvPickSpace.hxx's kMatchBuild is not the pick space's match line $($roomBuild.matchLine)." }
    $previousBuild = [string](Get-PubShow $Ctx $repo $previousCommit 'data/pick-space.json' | ConvertFrom-Json).build
    $roomBuild | Add-Member previousId $previousBuild
    $roomBuild | Add-Member changed ($previousBuild -cne $roomBuild.id)
    $readme = Get-PubShow $Ctx $repo $commit 'README.md'
    if ($readme -cnotmatch 'library commit `([0-9a-f]{7,40})`') { throw 'README.md pins no library commit (library commit `<sha>`).' }
    $pin = $Matches[1]
    $libRepo = $Ctx.LibraryRepo
    $library = Resolve-PubCommit $Ctx $libRepo $(if ($P.Library) { $P.Library } else { $pin })
    if (!$library.StartsWith($pin)) { throw "README.md pins library $pin, but the library to build is $library." }
    $libraryTree = Get-PubTree $Ctx $libRepo $library
    $libraryTip = Get-PubRemoteRef $Ctx $libRepo $cfg.libraryRemote "refs/heads/$($cfg.libraryBranch)"
    if ($libraryTip -cnotmatch '^[0-9a-f]{40}$') { throw "Could not read $($cfg.libraryBranch) from the library's $($cfg.libraryRemote)." }
    if (!(Test-PubObject $Ctx $libRepo $libraryTip)) { throw "The library's public tip $libraryTip is not in $libRepo; fetch it." }
    $libraryPush = $false
    if ($libraryTip -ceq $library -or (Test-PubAncestor $Ctx $libRepo $library $libraryTip)) { $libraryPush = $false }
    elseif (Test-PubAncestor $Ctx $libRepo $libraryTip $library) { $libraryPush = $true }
    else { throw "The library commit $library and $($cfg.libraryBranch) ($libraryTip) have diverged." }

    # The identity public commits carry: the last release's own author, plus any the owner allows.
    $releaseIdentity = Get-PubGitText $Ctx $repo @('log', '-1', '--format=%an <%ae>', $previousCommit)
    $allowed = @($releaseIdentity) + @($P.AllowIdentity | Where-Object { $_ })

    # Notes, the Discord draft, and every commit, tree, blob and path being released, in both repositories.
    $notesBytes = Assert-PubNotesText $P.Notes $rules 'release notes' @($cfg.notesForbidden)
    $notesSha = Get-PubBytesSha256 $notesBytes
    $discordSha = ''
    if ($P.DiscordDraft) { $discordSha = Get-PubBytesSha256 (Assert-PubNotesText $P.DiscordDraft $rules 'Discord draft' @($cfg.notesForbidden)) }
    $scan = Get-PubRangeScan $Ctx $repo $commit $previousCommit $rules 'sfv-ember'
    Assert-PubNoHits $scan.hits 'The source being released'
    $releasedCommits = @($scan.commits)
    if (!$releasedCommits.Count) { throw "$commit releases no commit after $($previous.tag_name)." }
    Assert-PubCommits $releasedCommits $allowed 'sfv-ember'
    $libraryScan = @{commits = @(); objects = 0; hits = @()}
    if ($libraryPush) {
        $libraryScan = Get-PubRangeScan $Ctx $libRepo $library $libraryTip $rules 'library' -NoTree
        Assert-PubNoHits $libraryScan.hits 'The library commits being pushed'
        Assert-PubCommits $libraryScan.commits $allowed 'library'
    }

    $plan = [ordered]@{
        schema = $PubPlanSchema; planId = [Guid]::NewGuid().ToString('N'); label = $label.label; tag = $label.tag; title = $label.title
        releaseBranch = $label.releaseBranch; zip = $label.zip; publicRepo = $cfg.publicRepo
        source = [ordered]@{ref = $P.Source; commit = $commit; tree = $tree; commits = $releasedCommits.Count; cleanWorktrees = $worktrees}
        previous = [ordered]@{tag = $previous.tag_name; commit = $previousCommit; publicMain = $publicMain; originBranch = $originBranch}
        library = [ordered]@{commit = $library; tree = $libraryTree; readmePin = $pin; branch = $cfg.libraryBranch; publicTip = $libraryTip; needsPush = $libraryPush; commits = @($libraryScan.commits).Count}
        roomBuild = $roomBuild; version = $label.label
        notes = [ordered]@{sha256 = $notesSha; source = $P.Notes}; discordDraft = [ordered]@{sha256 = $discordSha}
        rules = [ordered]@{sha256 = @(Get-PubRulesFingerprint $P.Rules); machineNames = $true}
        identities = @($allowed)
        scan = [ordered]@{sourceObjects = $scan.objects; libraryObjects = $libraryScan.objects; hits = 0}
        pipeline = [ordered]@{commit = $pipelineCommit; dirty = [bool]$pipelineStatus.Trim()}
        createdAt = Get-PubNow $Ctx
    }

    # Made once: the same inputs find the plan there already; other inputs need -Replan.
    $planPath = Get-PubEvidencePath $Ctx 'plan'
    if (Test-Path -LiteralPath $planPath) {
        $existing = Read-PubPlan $Ctx
        $same = $existing.plan.source.commit -ceq $commit -and $existing.plan.library.commit -ceq $library -and $existing.plan.notes.sha256 -ceq $notesSha -and
            ((@($existing.plan.rules.sha256) -join ',') -ceq (@($plan.rules.sha256) -join ','))
        if ($same -and !$P.Replan) { Write-PubLog $Ctx "The plan stands ($($existing.sha256)): same source, library, notes and rules."; return $existing }
        if (!$P.Replan) { throw "A plan for $($Ctx.Label) exists with other inputs (source $($existing.plan.source.commit)); -Replan supersedes it and every stage after it." }
        Assert-PubNoLiveDraft $Ctx
        if ($Ctx.DryRun) { Write-PubLog $Ctx 'Dry run: would supersede the plan and every stage, then write a new plan.'; return [pscustomobject]@{plan = [pscustomobject]$plan; sha256 = ''} }
        Clear-PubStages $Ctx 'plan'
    }
    if ($Ctx.DryRun) {
        Write-PubLog $Ctx "Dry run: every plan check passed ($($releasedCommits.Count) commits, $($scan.objects) objects scanned); plan.json not written."
        return [pscustomobject]@{plan = [pscustomobject]$plan; sha256 = ''}
    }
    Write-PubText (Join-Path $Ctx.Root 'notes.md') ([Text.UTF8Encoding]::new($false).GetString($notesBytes))
    if ((Get-PubSha256 (Join-Path $Ctx.Root 'notes.md')) -cne $notesSha) { throw 'notes.md did not keep the notes byte for byte.' }
    $sha = Write-PubEvidence $Ctx 'plan' $plan
    Write-PubLog $Ctx "Plan written: $($label.tag) from $commit, library $library, room build $($roomBuild.id)$(if ($roomBuild.changed) { ' (new)' } else { ' (unchanged)' })."
    [pscustomobject]@{plan = (Read-PubJson $planPath); sha256 = $sha}
}

# A draft recorded by this label's draft stage must be gone from GitHub before the plan is replaced.
function Assert-PubNoLiveDraft([object]$Ctx) {
    $path = Get-PubEvidencePath $Ctx 'draft'
    $state = Join-Path (Get-PubStageDir $Ctx 'draft') 'draft-state.json'
    foreach ($file in @($path, $state)) {
        if (Test-Path -LiteralPath $file) {
            $id = [string](Read-PubJson $file).releaseId
            if ($id -and (Get-PubReleaseById $Ctx $id -AllowMissing)) { throw "Draft $id of $($Ctx.Label) still exists on $($Ctx.Config.publicRepo); delete it there before planning again." }
        }
    }
}

# ---------------------------------------------------------------------------------------------------------------
# Evidence: shape, chain and files

function New-PubEvidence([object]$Ctx, [string]$Stage, [object]$PlanRead, [object[]]$Files, [hashtable]$Facts) {
    $inputs = [ordered]@{}
    foreach ($name in $PubInputs[$Stage]) { $inputs[$name] = Get-PubLedgerSha $Ctx $name }
    [ordered]@{
        schema = $PubEvidenceSchema; stage = $Stage; label = $Ctx.Label
        plan = [ordered]@{sha256 = $PlanRead.sha256; planId = $PlanRead.plan.planId}
        source = [ordered]@{commit = $PlanRead.plan.source.commit; tree = $PlanRead.plan.source.tree}
        library = [ordered]@{commit = $PlanRead.plan.library.commit; tree = $PlanRead.plan.library.tree}
        inputs = $inputs; files = @($Files); facts = $Facts; createdAt = Get-PubNow $Ctx
    }
}
function Get-PubFileRecord([string]$Root, [string]$Relative) {
    $path = Join-Path $Root $Relative
    [ordered]@{name = $Relative.Replace('\', '/'); sha256 = (Get-PubSha256 $path); size = (Get-Item -LiteralPath $path).Length}
}
# A stage's evidence, checked: the ledger's file, for this plan, every file it names unchanged on disk, and its
# inputs the current evidence of the stages it relied on (each checked the same way). Returns the evidence.
function Read-PubEvidence([object]$Ctx, [string]$Stage, [object]$PlanRead, [hashtable]$Seen = @{}) {
    if ($Seen.ContainsKey($Stage)) { return $Seen[$Stage] }
    $path = Get-PubEvidencePath $Ctx $Stage
    if (!(Test-Path -LiteralPath $path)) { throw "No $Stage evidence for $($Ctx.Label): run -Stage $Stage first." }
    $sha = Get-PubSha256 $path
    $ledger = Get-PubLedgerSha $Ctx $Stage
    if (!$ledger -or $ledger -cne $sha) { throw "The $Stage evidence is not the file the $Stage stage wrote (the ledger names $(if ($ledger) { $ledger } else { 'none' }), the file is $sha)." }
    $evidence = Read-PubJson $path
    if ($evidence.schema -cne $PubEvidenceSchema -or $evidence.stage -cne $Stage -or $evidence.label -cne $Ctx.Label) { throw "The $Stage evidence is not $($Ctx.Label)'s $Stage evidence." }
    if ($evidence.plan.sha256 -cne $PlanRead.sha256 -or $evidence.plan.planId -cne $PlanRead.plan.planId) { throw "The $Stage evidence was made for another plan." }
    if ($evidence.source.commit -cne $PlanRead.plan.source.commit -or $evidence.source.tree -cne $PlanRead.plan.source.tree -or
        $evidence.library.commit -cne $PlanRead.plan.library.commit -or $evidence.library.tree -cne $PlanRead.plan.library.tree) { throw "The $Stage evidence is for another source or library commit." }
    $dir = Get-PubStageDir $Ctx $Stage
    foreach ($file in @($evidence.files)) {
        $local = Join-Path $dir ($file.name.Replace('/', '\'))
        if (!(Test-Path -LiteralPath $local -PathType Leaf)) { throw "The $Stage evidence names $($file.name), which is gone." }
        if ((Get-PubSha256 $local) -cne $file.sha256) { throw "$Stage file $($file.name) changed since its evidence was written." }
    }
    foreach ($name in $PubInputs[$Stage]) {
        $want = [string]$evidence.inputs.$name
        $inputEvidence = Read-PubEvidence $Ctx $name $PlanRead $Seen
        if ($want -cne (Get-PubLedgerSha $Ctx $name)) { throw "The $Stage evidence relied on another $name evidence ($want) than the current one; do $Stage again (-Redo)." }
        $null = $inputEvidence
    }
    $evidence | Add-Member -NotePropertyName sha256 -NotePropertyValue $sha -Force
    $Seen[$Stage] = $evidence
    $evidence
}
# A stage that has evidence already: checked, and the stage is done (idempotent re-runs). With -Redo the stage and
# every later one are superseded first. Returns $true when the stage still has to run.
function Test-PubStageToDo([object]$Ctx, [string]$Stage, [object]$PlanRead, [switch]$Redo) {
    $path = Get-PubEvidencePath $Ctx $Stage
    if ((Test-Path -LiteralPath $path) -and !$Redo) {
        $evidence = Read-PubEvidence $Ctx $Stage $PlanRead
        Write-PubLog $Ctx "$Stage is done already (evidence $($evidence.sha256), checked again)."
        return $false
    }
    if ($Redo -and (Get-ChildItem -LiteralPath (Get-PubStageDir $Ctx $Stage) -ErrorAction SilentlyContinue)) {
        if ($Stage -cin @('build', 'rooms', 'draft')) { Assert-PubNoLiveDraft $Ctx }
        if ($Ctx.DryRun) { Write-PubLog $Ctx "Dry run: would supersede $Stage and every later stage."; return $true }
        Clear-PubStages $Ctx $Stage
    }
    foreach ($name in $PubInputs[$Stage]) { $null = Read-PubEvidence $Ctx $name $PlanRead }
    $true
}

# ---------------------------------------------------------------------------------------------------------------
# Outward steps

function Invoke-PubOutward([object]$Ctx, [string]$Step, [string]$What, [scriptblock]$Action) {
    $log = Join-Path $Ctx.Root 'outward.log'
    $line = { param($Status) [IO.File]::AppendAllText($log, "$(Get-PubNow $Ctx) $Step $Status $What`n", [Text.UTF8Encoding]::new($false)) }
    if ($Step -cin @($Ctx.Skip)) { & $line 'SKIPPED'; Write-PubLog $Ctx "SKIPPED ($Step): $What"; return [pscustomobject]@{done = $false; skipped = $true} }
    if ($Ctx.DryRun) { Write-PubLog $Ctx "Dry run, would ($Step): $What"; return [pscustomobject]@{done = $false; skipped = $false; dryRun = $true} }
    & $line 'START'
    try { $result = & $Action } catch { & $line "FAILED $($_.Exception.Message -replace '\s+', ' ')"; throw }
    & $line 'DONE'
    [pscustomobject]@{done = $true; skipped = $false; result = $result}
}

# ---------------------------------------------------------------------------------------------------------------
# GitHub, read through gh (read-only unless inside Invoke-PubOutward)

function Invoke-PubGh([object]$Ctx, [string[]]$Arguments, [switch]$AllowFail) {
    $result = & $Ctx.Ops.Gh $Arguments
    if ($result.exitCode -and !$AllowFail) { throw "gh $($Arguments[0..1] -join ' ') failed: $($result.stderr)" }
    $result
}
function Get-PubReleases([object]$Ctx) {
    $text = ConvertTo-PubText (Invoke-PubGh $Ctx @('api', "repos/$($Ctx.Config.publicRepo)/releases", '--paginate', '--jq', '.[] | {id, tag_name, name, draft, prerelease, target_commitish, created_at, published_at}'))
    @($text -split "`n" | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
}
function Get-PubReleaseById([object]$Ctx, [string]$Id, [switch]$AllowMissing) {
    if ($Id -cnotmatch '^[1-9][0-9]*$') { throw "Not a release id: $Id" }
    $result = Invoke-PubGh $Ctx @('api', "repos/$($Ctx.Config.publicRepo)/releases/$Id") -AllowFail
    if ($result.exitCode) {
        if ($AllowMissing -and "$($result.stderr)$(ConvertTo-PubText $result)" -match '404|Not Found') { return $null }
        throw "Could not read release $Id of $($Ctx.Config.publicRepo): $($result.stderr)"
    }
    ConvertTo-PubText $result | ConvertFrom-Json
}

. (Join-Path $PSScriptRoot 'SfvPublishStages.ps1')
