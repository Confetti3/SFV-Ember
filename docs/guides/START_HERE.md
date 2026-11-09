# SFV Ember

SFV Ember is an unofficial rollback netplay mod for Street Fighter V on Steam,
built on Ember. It plays private rooms between players who run the same SFV
Ember build. It never connects the game to Capcom's servers: the game runs
offline, and its online modes stay closed.

Players with different DLC can play each other. The fighters, costumes,
colours and stages you can pick follow what your Steam account owns, and your
opponent's picks always play on your screen. A stage one of you does not have
is left out of the stage choice, and Random stage picks only from stages you
both have.

## Start

1. Install Street Fighter V from Steam, and keep Steam running.
2. Extract this folder anywhere on your PC (not inside the game's folder).
3. Double-click **Launcher.exe**.

The launcher finds the game through Steam, starts it in a window with SFV
Ember inside it and waits for it to close. Nothing is copied into the game's
folder. Press **F10** or **Start** in the game to open SFV Ember.

If the launcher cannot find the game, it opens its recovery window: choose the
game's `StreetFighterV\Binaries\Win64` folder (the one that holds
`StreetFighterV.exe`) and retry.

## Controllers

In this beta, SFV Ember's menus follow an Xbox-style (XInput) controller in
the first slot. PlayStation pads, DirectInput sticks and the keyboard cannot
move around SFV Ember's menus yet (F10 still opens and closes them).

## Beta notes

- This is a beta: you and your opponent need the same SFV Ember release.
- Ember's frame meter is in Training only.
- Translations other than English are drafts.

## Files

- Settings and logs: `%APPDATA%\SFVEmber` (`logs\sfv_ember.log` is the game's,
  `logs\launcher.log` the launcher's).
- Room links from a browser open SFV Ember through `ember-sfv://` links, which
  the launcher registers for your Windows account when it first runs.

## Updates

The launcher checks github.com/Confetti3/SFV-Ember for new releases and offers
them in its Updates window. A beta copy follows the Beta channel, which gets
betas and full releases.
