#pragma once

// SFV Ember 0.1.0-beta.1 saved "SFV 1" as every player's name (the copy's
// instance number, hard-coded), so two of its players could never share a
// room: rooms refuse a second member with a name already in them. Nobody
// chose those names, so the launcher treats exactly "SFV 1" to "SFV 4" as
// unset and gives the player a unique one (EnsureUniqueDisplayName). Any
// other saved name is the player's and is kept (EMBER_LIBRARY.md section 42).

#include <cstring>

namespace sfve::launcher {

inline bool IsOldDefaultName(const char* name)
{
	return name && std::strlen(name) == 5 && std::strncmp(name, "SFV ", 4) == 0 && name[4] >= '1' && name[4] <= '4';
}

} // namespace sfve::launcher
