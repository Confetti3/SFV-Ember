#pragma once

// Which battle a battle frame update belongs to.
//
// A battle frame's address does not name its battle: the game frees the
// frame when the battle ends (the scene state's teardown, rva 0x4EFBE0) and
// allocates the next battle's with FMemory::Malloc (rva 0x4F4788), which can
// hand back the same block. A battle is the construction of its frame (rva
// 0x4C5C40, once per battle: sfv::kBattleFrameCtor): an update is a new
// battle when its frame is not the last update's, or when the frame was
// constructed again since. Training resets, round changes and the game's
// rematch keep the frame and construct nothing, so they stay one battle.
//
// Without a construction seen (the hook missing) the address is all there is.
// Game thread only.

#include <stdint.h>

namespace sfve::mod {

class BattleIdentity {
public:
	// The game constructed a battle frame at `frame`.
	void Constructed(const void* frame)
	{
		constructedAt_ = frame;
		constructions_++;
	}

	// A battle frame update. True when it starts a battle (the first update
	// of a frame constructed since the last update, or of another frame);
	// Battle() then numbers the new battle.
	bool See(const void* frame)
	{
		const bool rebuilt = frame == constructedAt_ && constructions_ != construction_;
		if (frame == frame_ && !rebuilt) return false;
		frame_ = frame;
		construction_ = frame == constructedAt_ ? constructions_ : 0;
		battles_++;
		return true;
	}

	// The battle the last update belongs to: 1 for the first, 0 before any.
	uint64_t Battle() const { return battles_; }
	// The constructions seen (all battle frames, whether updated or not).
	uint64_t Constructions() const { return constructions_; }

private:
	const void* constructedAt_ = nullptr; // the frame last constructed
	uint64_t constructions_ = 0;
	const void* frame_ = nullptr;         // the last update's frame
	uint64_t construction_ = 0;           // its construction (0: none seen)
	uint64_t battles_ = 0;
};

} // namespace sfve::mod
