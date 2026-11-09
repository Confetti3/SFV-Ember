# Dot-sourced by build.ps1 and build-helper.ps1: the one place that enforces the
# rule "never build on C:" (the maintainer's C: must stay above 100 GB free).

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
	if ($full.Length -gt 3) { $full = $full.TrimEnd('\') }
	$where = $full
	for ($folder = $full; $folder; $folder = [System.IO.Path]::GetDirectoryName($folder)) {
		if (Test-Path -LiteralPath $folder) {
			$target = (Get-Item -LiteralPath $folder -Force).ResolveLinkTarget($true)
			if ($target) { $where = $target.FullName; break }
		}
	}
	if ($where -notmatch '^[A-Za-z]:\') { throw "$what '$path' is $where, not a path on a lettered drive: name a folder on D: (never on C:)" }
	if ($where -match '^[Cc]:') {
		$through = if ($where -ne $full) { " (it is $full, which leads there)" } else { '' }
		throw "$what '$path' is on C:$through. Never build on C: - use a folder on D: (the default already is)"
	}
	$full
}
