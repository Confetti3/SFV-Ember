#pragma once

// A fighter unit of the bound battle, partners included, as SfvAdapter's bind
// finds them in the battle's order: where their anchored socket reads start,
// whose meshes are kept at LOD 0, whose command queues are noted and whose
// finish mask a skipped Finish writes.

#include <stdint.h>

namespace sfve::mod {

struct FighterUnit {
	uint8_t* chara = nullptr;
	int32_t side = -1;
	uint8_t* action = nullptr; // CharaActionPart, for the facing
	uint8_t* visual = nullptr; // CharaVisualPart
};

} // namespace sfve::mod
