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

SFV reads only Xbox-style (XInput) controllers, and SFV Ember starts it
outside Steam, so Steam Input does not convert other pads. SFV Ember's
controller layer does instead: a PlayStation pad (DualShock 4, DualSense), a
Switch Pro pad or an arcade stick in its PlayStation or DirectInput mode is
read with SDL3 and reaches the game as an XInput controller, in the first slot
no Xbox-style controller holds. An Xbox pad, or a stick in its XInput mode,
reads exactly as before.

SFV Ember's menus and its battles follow the controller in the first slot. To
play with a PlayStation pad, unplug any Xbox-style pad before starting SFV
Ember: a pad keeps the slot it was given, and an Xbox pad plugged in while the
game runs takes the first slot.

To turn the layer off, set `"controllerLayer": false` in
`%APPDATA%\SFVEmber\settings.json` and start SFV Ember again. The keyboard
cannot move around SFV Ember's menus yet (F10 still opens and closes them).

If Steam is running, its desktop configuration may also turn a PlayStation pad
into a keyboard and mouse while SFV is in front, so SFV's own menus may take a
press twice. Turning off Steam Input for PlayStation controllers in Steam's
controller settings stops that.

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
