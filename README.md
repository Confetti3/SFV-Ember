# SFV Ember

Rollback netplay for Street Fighter V, built on SF4 Ember's netcode. Early
work: the framework and rollback core are built and tested against a mock
game; nothing runs in the real game yet.

The game never talks to Capcom: the mod blocks every connection but loopback
and the session's own peers, starting before any game code runs.

- Design and plan: [docs/design/SFV_PORT.md](docs/design/SFV_PORT.md)
- Built on the Ember library: [docs/design/EMBER_LIBRARY.md](docs/design/EMBER_LIBRARY.md)

## Build

Needs Visual Studio 18 Build Tools (C++ x64), the vcpkg checkout next to
this folder (`..\vcpkg`) and a checkout of the Ember library (SF4 Ember's
`feat/ember-library` branch), named with `-EmberRoot` or `SFVE_EMBER_ROOT`.

```powershell
.\scripts\build.ps1 -EmberRoot D:\sf4-wt\ember-library
```

Builds into `D:\sfv-ember-build` and runs the tests. Outputs:
`sfv_ember.dll` (the mod), `xinput1_3.dll` (the proxy that loads it) and
`sfve_tests.exe`.
