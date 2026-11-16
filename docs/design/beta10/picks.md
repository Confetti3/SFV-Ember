# beta.10: each fighter's own look, kept between runs (2026-11-15)

**The reports (beta.9 field log).** Costume, colour, V-Skill and V-Trigger looked like one choice
shared by the whole cast (every launch of the run: `costume 2 colour 13 vskill 1 vtrigger 1` for
Z42, Z29, Z33), and the fighter the player had picked was not there at the next start.

## Why

- `SfvPick::LookFor(pick, fighter, remembered)` gave a fighter its remembered look only where
  `SetPick` had stored that fighter this run; any other fighter took the look the player was in
  (`look = pick`), and the look page's Fighter row (`SfvSelectionScreen::Apply` / `Previewed`)
  never asked the memory at all, it carried the look over. So a look chosen once travelled to
  every fighter the player moved to.
- Nothing saved the pick: `SfvPick` stated "not saved between runs", and `settings.json` holds
  no pick. The `profile.mainFighter` the library writes is 0 for SFV by design (Branding's
  `roomFacesArePicks`, `ember::PlayersHaveMain()` false: no Main character row, so a main is never
  set); the fighter a player means by "my main" is the fighter they last chose.

## What it does

- **`SfvPickMemory`** (`picks::PickMemory`, no game in it): each fighter's last stored look and the
  fighter last chosen; `ToJson` / `FromJson` against the pick space. The file names fighters by
  code (`"Z42"`), not by number (positions in the pick space, which move with the roster), and a
  look is kept only where `ValidPick` holds it: unknown code, a costume or colour the fighter
  lacks, an unshipped costume, a V-Skill or V-Trigger it does not have, a number that is no
  integer: left out.
- **`SfvPick`** owns one `PickMemory`. `SetPick` remembers the look under its fighter (not Eleven's)
  and the fighter as chosen (not Random's roll). `LookFor` with `remembered` is that fighter's
  look: the remembered one, or `picks::UnchosenLook` fitted (first costume the player can wear, its
  first colour, V-Skill I, V-Trigger I) for a fighter not stored yet. A remembered look this
  player cannot wear (the catalog's `FitLookWithin` would move it: not owned, not installed) is
  not nudged to the nearest look: the fighter is in its default look. The model size travels with
  the pick (Tiny is a room rule, not a fighter's look). The Training dummy keeps no memory
  (`remembered` false) and carries its look as before.
- **`SfvSelectionScreen`**: the board already used `LookFor`; `Apply` and `Previewed` now do for
  the look page's Fighter row, with `remembered` false on the dummy's screen.
- **`SfvPickStore`**: per game frame, once the catalog is usable, reads the overlay section's
  `"sfvPicks"` key (`SettingsStore::LoadOverlay`; the library's SF4 keeps its per-fighter picks in
  that section too) and `RestorePickMemory`s it, unless a pick was already stored; writes it
  (`SaveOverlay`) 1.5 s after the memory last changed and once more at the game's exit
  (`StopAtExit`). A settings file that cannot be read is retried (5 times, 2 s apart) and never
  written over meanwhile. A test copy (instance above 0) reads, never writes.

```json
"overlay": { "sfvPicks": { "version": 1, "fighter": "Z42",
  "looks": { "Z42": { "costume": 2, "colour": 13, "vSkill": 0, "vTrigger": 1 } } } }
```

V-Skill and V-Trigger are the game's choice numbers (I is 0, II is 1). The costume code, the
model size and Random / Eleven are not saved (Random and Eleven are chosen again each run).
