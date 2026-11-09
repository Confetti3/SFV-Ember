#pragma once

// The controller layer's thread (docs/design/EMBER_LIBRARY.md section 48):
// SDL3 reads the pads the game cannot read through XInput, and each one
// gets an XInput slot no real XInput pad holds (mod/PadLayer), where the
// game's XInput reads find it (mod/VirtualPads' hooks).
//
// SDL lives on this thread alone: its start, its reads, opening and closing
// pads, and their motors. The game's main thread is apartment-threaded for
// COM (UE4 calls OleInitialize there) and SDL's Windows backends start COM
// on whichever thread starts them, so SDL never touches the game's (SF4
// Ember's layer started SDL on the game's thread and stopped the game
// starting until it joined the game's apartment first, f0a837a1). The hooks
// read only what the thread publishes.
//
// SDL is linked statically: no SDL3.dll comes with the mod.

namespace sfve::mod {

// Starts the layer once, when the pad hooks are in: `enabled` is the
// player's saved setting (settings.json's controllerLayer, on unless set
// false). Off, it logs that the game reads the pads itself and starts
// nothing. Any thread; returns at once.
void StartSdlPads(bool enabled);

// The pipe's "sdlPads" status: the layer's state, SDL's pads and the slot
// each holds.
void RegisterSdlPadStatus();

} // namespace sfve::mod
