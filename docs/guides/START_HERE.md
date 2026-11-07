# SFV Ember

SFV Ember is an unofficial rollback netplay mod for Street Fighter V on Steam,
built on Ember. It plays private rooms between players who run the same SFV
Ember build with the same installed fighters and stages. It never connects the
game to Capcom's servers: the game runs offline, and its online modes stay
closed.

## Start

1. Install Street Fighter V from Steam, and keep Steam running.
2. Extract this folder anywhere on your PC (not inside the game's folder).
3. Double-click **Launcher.exe**.

The launcher finds the game through Steam, starts it with SFV Ember inside it
and waits for it to close. Nothing is copied into the game's folder. Press
**F10** or **Start** in the game to open SFV Ember.

If the launcher cannot find the game, it opens its recovery window: choose the
game's `StreetFighterV\Binaries\Win64` folder (the one that holds
`StreetFighterV.exe`) and retry.

## Files

- Settings and logs: `%APPDATA%\SFVEmber` (`logs\sfv_ember.log` is the game's,
  `logs\launcher.log` the launcher's).
- Room links from a browser open SFV Ember through `ember-sfv://` links, which
  the launcher registers for your Windows account when it first runs.

## Updates

SFV Ember has no public releases yet, so it checks for no updates.
