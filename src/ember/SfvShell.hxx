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
//     the game's menu never sees a press meant for Ember.
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

} // namespace sfve::adapter
