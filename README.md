# SFV Ember

Rollback netplay for Street Fighter V on Steam, built on
[Ember](https://github.com/Confetti3/SF4-Ember-Netplay), the netcode of SF4
Ember. Unofficial and not affiliated with Capcom.

The game never talks to Capcom: the mod blocks every connection except
loopback and the room's own players, before any game code runs. SFV's own
online modes stay closed.

## Play

1. Download the newest `sfv-ember-<version>.zip` from
   [Releases](https://github.com/Confetti3/SFV-Ember/releases).
2. Extract it anywhere outside the game's folder.
3. Keep Steam running and double-click **Launcher.exe**. Press **F10** or
   **Start** in the game to open SFV Ember.

`START_HERE.md` in the package has the details. Both players need the same SFV
Ember release.

## What it has

- Private rooms with rollback netplay, spectators and rematches.
- Players with different DLC can play: each player picks from what their
  account owns, the opponent's picks always play, and stages one player lacks
  are left out of the stage choice. Random stage picks from stages both have.
- Fighter select with costumes and colours, V-Skill and V-Trigger, and
  previews of each pick and stage.
- Ember's frame meter in SFV's Training.
- Replays and the Discord companion from Ember.
- In-app updates from this repository's releases.

This is a beta. Known limits: Ember's menus and its battles follow the pad in
the game's first slot; the frame meter is in Training only; non-English
translations are drafts.

PlayStation, Switch and DirectInput pads work without Steam Input: SFV Ember's
controller layer reads them with SDL3 and gives them to the game as XInput
pads, in slots no Xbox-style pad holds. `"controllerLayer": false` in
`%APPDATA%\SFVEmber\settings.json` turns it off.

## Build

Needs Visual Studio 18 Build Tools (C++ x64), a vcpkg checkout next to this
folder (`..\vcpkg`), and the Ember library: the `feat/ember-library` branch of
[SF4-Ember-Netplay](https://github.com/Confetti3/SF4-Ember-Netplay). This
release builds against library commit `ffafff701`.

```powershell
git clone -b feat/ember-library https://github.com/Confetti3/SF4-Ember-Netplay ember-library
git -C ember-library checkout ffafff701
.\scripts\build.ps1 -EmberRoot ..\ember-library
```

`scripts/build.ps1` builds and runs the tests; `-Player` makes a player build,
which `scripts/package.ps1` packages. Design notes are in
[docs/design/EMBER_LIBRARY.md](docs/design/EMBER_LIBRARY.md) and
[docs/design/SFV_PORT.md](docs/design/SFV_PORT.md).

## Licence

MIT (`LICENSE`). Ember's own licence and the third-party notices ship in each
package's `notices` folder. Street Fighter V is Capcom's; this project ships
none of its files.
