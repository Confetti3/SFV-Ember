# Files copied from SF4 Ember

Copied byte for byte from `sf4-current` (branch `release`) at commit
`0fc37b325a820737c2255a4a824dece9e8397fb8` on 2026-11-04. Keep them identical
so Ember fixes can be diffed and brought across; do not rename the `sf4e`
namespaces.

| Here | Ember source |
| --- | --- |
| `src/shared/sf4e__GgpoGate.hxx`, `.cxx` | `src/common/` |
| `src/shared/sf4e__GgpoAbortLatch.hxx` | `src/common/` |
| `src/shared/sf4e__StateHash.hxx` | `src/common/` |
| `src/shared/sf4e__PacingController.hxx` | `src/common/` |
| `src/shared/GgpoDisconnectTolerance.hxx` | `src/common/` |
| `vcpkg-overlays/ports/ggpo/` (port-version 18) | `vcpkg-overlays/ports/ggpo/` |
| `src/shared/platform/HelperProcess.*`, `HelperClient.*`, `HelperFrame.hxx` | `src/platform/` (at `831c2078`) |
| `src/shared/common/TickCount.hxx`, `WipeText.hxx` | `src/common/` (at `831c2078`) |
| `src/shared/netplay/BoundedMailbox.hxx` | `src/netplay/` (at `831c2078`) |
| `sf4-net.exe` (built by `scripts/build-helper.ps1`, not copied) | `rust/sf4-net` |

Check for drift:

```powershell
foreach ($f in Get-ChildItem src\shared) { git diff --no-index --stat "..\sf4-current\src\common\$($f.Name)" $f.FullName }
```
