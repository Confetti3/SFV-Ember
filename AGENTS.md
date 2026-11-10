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
  GGPO port plus `ignored-inputs.patch`.
- No entitlement or character unlocks beyond Ember's own ownership gate,
  which is off by default while Capcom's entitlement server is down
  (`kOwnershipGateByDefault`, EMBER_LIBRARY.md section 54; Kate, 2026-11-10).
  Turn it back on when the server answers again.
