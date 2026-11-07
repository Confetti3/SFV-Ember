#pragma once

// Ember's match HUD over SFV's battle (EMBER_LIBRARY.md section 18).
//
// The players' names go where SFV itself names a player: the plate on each
// life bar ("PLAYER 1", or an online player's name). The game writes them
// there in its own font, at its own size (mod/HudText); Ember gives it the
// room's names while a room's game is fought. Ember's overlay draws the rest
// of its match HUD, laid out around SFV's battle HUD as SFV Ember's product
// describes it (SfvProduct.cxx, ember::BattleHud): each name's link and class
// marks just above its plate, and the telemetry (score, ping, rollback,
// delay) where the player's HUD settings put it, clear of SFV's timer, bars,
// gauges and portraits.

#include "sf4e/sf4e__NetplayFacade.hxx"

namespace sfve::adapter {

// Gives the HUD's plates the room's names. Once, from InstallEmberShell.
void InstallSfvHud();

// Render thread, inside ImGui's frame, with the shell shown or not: the
// match HUD while a room's game is played, or only its notices while the
// shell is open over it.
void DrawSfvMatchHud(const sf4e::NetplayFacade::PresentationSnapshot& frame, bool shellShown);

// Render thread, inside ImGui's frame, over SFV's Training battle
// (EMBER_LIBRARY.md section 27): what SF4 Ember draws over its own while a
// room member waits there (ui/TrainingInRoom.hxx), in SFV Ember's look. A
// muted line under the game's timer says where they stand in their room
// (hidden while the shell or the game's pause menu is up), and while the
// room's call back runs its countdown a band across the screen says the
// match is ready, with who sat down and go now's prompt while it can be
// pressed. `unread`: the room chat's unread count, as the room screen shows
// it; `focused`: the game's window has the focus.
void DrawSfvTraining(const sf4e::NetplayFacade::PresentationSnapshot& frame, bool shellShown, unsigned unread,
	bool focused);

} // namespace sfve::adapter
