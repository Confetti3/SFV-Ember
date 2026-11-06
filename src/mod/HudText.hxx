#pragma once

// The battle HUD's own text. SFV's HUD is a Scaleform movie, and the game
// names each side's plate on the life bar through it: the label above the
// bar ("PLAYER 1", or an online player's name) is the movie's SetPilotName,
// called with the side as the battle's HUD is set up and again when the
// side's pilot changes (sfv::kHudInvoke). The mod hooks that call, so a name
// it is given takes the label's place: the game draws it in its own font, at
// its own size and place, on every resolution and aspect the HUD supports.
//
// The fighter's name under the bar (SetPlayerName) is left as the game shows
// it.
//
// Status field "hudText": each side's last label as the game asked for it and
// as shown, and how many labels were replaced.

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

} // namespace sfve::mod
