#pragma once

// Ember's shell (the library's ApplicationShell, through its ShellHost) drawn
// over SFV by the mod's overlay (src/overlay/Overlay.hxx), as SF4 Ember's
// sf4e__Overlay draws it over SF4. This is SFV's part of it:
//   - the game's side of the shell's frame (ember::ShellGame): the player's
//     pick (SfvPick) and what the shell says of it, and SFV's pick screen
//     (SfvSelectionScreen);
//   - when the shell is shown (ui::OverlayPresentation): it opens with Start
//     on the gameplay pad or with F10 at the main menu, hides while a match
//     is prepared or played, and comes back after it;
//   - who has the pads (ui::MenuPads): the shell while it is shown, and
//     while a room is joined at the main menu even when it is hidden (the
//     game's own menu takes no input in a room: input::NativeMenuLockFor), so
//     the game's menu never sees a press meant for Ember;
//   - who has the keyboard: the shell while it is shown with the focus (the
//     overlay's capture), and otherwise the same as the pads
//     (KeepsKeyFromGame).
//
// The pads reach the shell the way they reach SF4's: the runtime samples the
// gameplay pad (SfvInput::ReadController, from VirtualPads' record of each
// slot's last state) and publishes it in its snapshot, and the shell's
// navigation reads it from there (ui::ControllerNavigation). A virtual pad
// (VirtualPads' `pad` command) therefore drives the shell exactly as a
// physical one does.
//
// The shell runs once Ember's runtime runs (EmberHost); before that the
// overlay draws nothing.
//
// Status field "shell": shown or not, why, the shell's screen and focused row.

namespace sfve::adapter {

// Plugs the shell into the overlay and registers its status. Once, from
// SfveStart, with rooms on (-sfve_room=1).
void InstallEmberShell();

// The shell holds the pads now, or takes them where Start opens it
// (Input::MenuTakesPads). Any thread.
bool ShellTakesPads();

// Who keeps the keyboard from the game while the shell is hidden.
struct KeyboardHolders {
	bool playback = false;   // a replay plays: its controls are Ember's
	bool menuLocked = false; // SFV's own main menu while a room is joined (input::NativeMenuLockFor)
};

// Whether a key message (WM_KEYDOWN, WM_KEYUP) is kept from the game, as the
// pads are: a replay's controls (F1 to F4, Esc and the seeks' keys), pressed
// and released, while it plays; and every key press at a locked main menu, so
// a keyboard player can no more start SFV's own modes from a room than a pad
// player can. A release there still reaches the game: its key went down for it.
bool KeepsKeyFromGame(const KeyboardHolders& holders, unsigned message, unsigned key);

} // namespace sfve::adapter
