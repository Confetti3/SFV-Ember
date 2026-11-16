#pragma once

// Training's gauge settings written into a bound battle (sfv::practice), in
// the battle data's tables and each fighter's copy, as Training's menu writes
// them: once, before the session's first frame, on every copy alike. Each is
// false (logged) when the battle is not bound (`battleData` null), a fighter
// is missing or the game refused a value.

#include <stddef.h>
#include <stdint.h>

#include <utility>

namespace sfve::mod::practice_gauges {

// A Practice room's game: both players' vital, EX and V-gauges refill and
// nobody is knocked out.
bool SetPractice(uint8_t* rollbackUnit, const uint8_t* battleData);
// Only the EX (Critical Art) gauge, full at every round's start
// (sfv::practice::kExtraFullAtRoundStart); vital and the V-gauge stay a match's.
bool SetStartFullExtra(uint8_t* rollbackUnit, const uint8_t* battleData);
// Writes each (param, value) of `gauges` into both players' tables; `what`
// names them in the log.
bool SetParams(uint8_t* rollbackUnit, const uint8_t* battleData, const std::pair<int, int>* gauges, size_t count,
	const char* what);

} // namespace sfve::mod::practice_gauges
