# SFV Ember

- x64 only. Build and test with `scripts/build.ps1`; the build tree is
  `D:\sfv-ember-build`. Never build on C: and stop if C: is under 120 GB free.
- SFV must stay offline from Capcom. Never launch the game without the maintainer's OK,
  never without the network guard, and never use its online modes.
- `sfvrproject/sfv-rollback` has no licence: read it as a map only. Copy no
  code, offsets, signatures or data from it; derive every address from the
  installed exe yourself.
- Ember's code comes from the Ember library checkout the build names
  (`scripts/build.ps1 -EmberRoot`, docs/design/EMBER_LIBRARY.md). Change it in
  the library, never by copying it here. `vcpkg-overlays/ports/ggpo` is Ember's
  GGPO port plus `ignored-inputs.patch`, `confirmed-input-frame.patch` and
  `pose-hold.patch` (docs/design/beta9/pose-hold.md).
- SFV grants every player everything in the game itself (every fighter,
  costume, colour and stage installed, in all of SFV's modes; `EntitlementGrant`,
  EMBER_LIBRARY.md section 57; the maintainer, 2026-11-10). Ownership, when it
  applies, is Ember's gate alone (`kOwnershipGateByDefault`, off while Capcom's
  entitlement server is down, section 54), judged by the account's own
  entitlement cache, never by the game's grant. When the server answers again,
  turn the gate on; the grant stays. `-sfve_grant_all=0` is a development flag.
