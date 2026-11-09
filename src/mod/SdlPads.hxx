#pragma once

// The controller layer's thread (docs/design/EMBER_LIBRARY.md section 49):
// SDL3, through the library's SDL pad source (common/SdlPadSource), reads
// the pads the game cannot read through XInput, and each one gets an XInput
// slot no real XInput pad or virtual test pad holds (the library's Slots),
// where the game's XInput reads find it (mod/XInputHooks, mod/PadLayer).
//
// SDL lives on this thread alone: its start, its reads, opening and closing
// pads, their motors and its end. The game's main thread is
// apartment-threaded for COM (UE4 calls OleInitialize there) and SDL's
// Windows backends start COM on whichever thread starts them, so SDL never
// touches the game's (SF4 Ember's layer started SDL on the game's thread and
// stopped the game starting until it joined the game's apartment first,
// f0a837a1). The hooks read only what the thread publishes.
//
// SDL is linked statically: no SDL3.dll comes with the mod.

namespace sfve::mod {

// What the mod's start found for the layer.
enum class PadLayerStart {
	Off,     // the player's setting is off (settings.json's controllerLayer)
	NoHooks, // the XInput hooks the layer answers the game through are not in
	On,
};

// Registers the pipe's "sdlPads" status (the layer's state, SDL's pads and
// the slot each holds) and, when On, starts the layer's thread, which the
// game's exit stops (mod/GameExit). Otherwise it logs why the game reads the
// pads itself. Once, from the mod's start on the game's main thread; returns
// at once.
void StartSdlPads(PadLayerStart start);

} // namespace sfve::mod
