# Dot-sourced by build.ps1 and build-helper.ps1: the one place that enforces the
# rule "never build on C:" (the build PC's C: must stay above 100 GB free), and
# on a Linux build PC "never build in the home folder".

# The full path of a build output or cache location ($what names it in the
# message), refused when it is not on a lettered drive other than C:. Run it on
# every such path before anything creates a directory there. Relative paths are
# taken from the current location, '..' is collapsed, and a folder (or any
# folder above it) that is a junction or symbolic link counts where it leads, so
# D:\build -> C:\somewhere is refused as well. A path that does not exist yet is
# fine.
function Resolve-BuildPath([string]$path, [string]$what) {
	if (!$path) { throw "$what is empty: name a folder on D: (never on C:)" }
	$full = [System.IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($path))
	# A Linux build PC (docs/release/PUBLISHING.md, "The build PC") has no drive letters. Its rule is another:
	# nothing in the home folder, whose name (the user's) the compiled paths would carry into the binaries.
	if (!$IsWindows) {
		if ($full.Length -gt 1) { $full = $full.TrimEnd('/') }
		Assert-OutsideHome $full $what
		return $full
	}
	if ($full.Length -gt 3) { $full = $full.TrimEnd('\') }
	$where = $full
	# A drive's root is never a link (ResolveLinkTarget throws on one), so the walk stops above it.
	for ($folder = $full; $folder -and $folder.Length -gt 3; $folder = [System.IO.Path]::GetDirectoryName($folder)) {
		if (Test-Path -LiteralPath $folder) {
			$target = (Get-Item -LiteralPath $folder -Force).ResolveLinkTarget($true)
			if ($target) { $where = $target.FullName; break }
		}
	}
	if ($where -notmatch '^[A-Za-z]:\\') { throw "$what '$path' is $where, not a path on a lettered drive: name a folder on D: (never on C:)" }
	if ($where -match '^[Cc]:') {
		$through = if ($where -ne $full) { " (it is $full, which leads there)" } else { '' }
		throw "$what '$path' is on C:$through. Never build on C: - use a folder on D: (the default already is)"
	}
	$full
}

# On a Linux build PC: $path (a checkout, build tree or cache) is not in the home folder, nor holds it. The
# compiler under Wine sees these paths (z:/home/<user>/...) and puts source paths into the binaries.
function Assert-OutsideHome([string]$path, [string]$what) {
	if ($IsWindows) { return }
	$homeDir = [System.IO.Path]::GetFullPath($HOME).TrimEnd('/')
	$full = [System.IO.Path]::GetFullPath($path).TrimEnd('/')
	if ($full -eq $homeDir -or $full.StartsWith("$homeDir/") -or $homeDir.StartsWith("$full/")) {
		throw "$what '$path' is in the home folder, whose name the binaries would carry: use a folder outside it"
	}
}
