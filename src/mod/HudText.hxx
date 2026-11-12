#pragma once

// The battle HUD's own text. SFV's HUD is a Scaleform movie, and the game
// names each side's player through it (sfv::kHudInvoke), with the side as
// the number: the plate on the life bar ("PLAYER 1", or an online player's
// name) is the movie's SetPilotName, called as the battle's HUD is set up and
// again when the side's pilot changes, and the marker over the fighter at a
// round's start is its SetCursorText, with the same label. The mod hooks
// those calls, so a name it is given takes the label's place: the game draws
// it in its own font, at its own size and place, on every resolution and
// aspect the HUD supports.
//
// The fighter's name under the bar (SetPlayerName) is left as the game shows
// it.
//
// Status field "hudText": each side's last plate and marker as the game asked
// for them and as shown, and how many labels were replaced.

#include <stdint.h>

#include <functional>
#include <string>

namespace sfve::mod {

// The name a side's plate shows instead of the game's label, or false for
// the game's own. Asked on the game thread, as the HUD names the plate.
using PlateName = std::function<bool(int side, std::wstring& name)>;

bool InstallHudText(uintptr_t imageBase);

// Sets where the plates' names come from (none: the game's labels). Once,
// before a battle.
void SetPlateNames(PlateName source);

// The photo booth's HUD probe (development builds, -sfve_mode=booth;
// docs/design/beta8/booth-protocol.md `booth hud` and `booth hudlog`): the
// booth looks for the HUD movie's call that hides the HUD, which overlaps the
// fighters' feet.
//
// Game thread only. Calls the HUD movie's method `method` with `number` and,
// when `text` is not empty, `text`, the way the game's own invoke does
// (sfv::kHudInvoke, on the movie the hook last saw; the call is made through
// the original, not the hook, so nothing is replaced). False when no HUD
// movie has been seen yet or the method name is empty; true means the call was
// made, not that the HUD changed.
bool HudCall(const std::wstring& method, float number, const std::wstring& text = {});

// While on, every HUD invoke the hook sees is logged: the method, the number
// and the text. Any thread.
void SetHudCallLog(bool on);

// The movie HudCall calls is the one that last named a side's plate or
// marker (SetPilotName, SetCursorText): the HUD's own, named again at each
// round's start. A battle left, or a new one (BoothMode), forgets it, so no
// call reaches the last battle's movie. Any thread.
void ForgetHudMovie();
// A movie is known for HudCall. Any thread.
bool HudMovieKnown();

} // namespace sfve::mod
