#pragma once

// Reading and writing the game's objects: typed fields, the std::shared_ptr
// the game hands units around in, and which words are addresses.

#include <intrin.h>
#include <stdint.h>

namespace sfve::mod {

template <typename T>
T& Field(uint8_t* object, uint32_t offset)
{
	return *reinterpret_cast<T*>(object + offset);
}

// MSVC std::shared_ptr, as the game stores it. The control block is
// {vtable, long uses, long weaks}.
struct GameSharedPtr {
	uint8_t* object;
	uint8_t* control;
};
static_assert(sizeof(GameSharedPtr) == 16);

constexpr uint32_t kControlUses = 8;

inline void AddRef(uint8_t* control)
{
	_InterlockedIncrement(reinterpret_cast<volatile long*>(control + kControlUses));
}

// Gives back one use. The last use is never given back: dropping it here
// would leave the object alive with no owner instead of going through the
// game's own release, so it is kept (one use leaked) and false returned.
inline bool ReleaseKeepingAlive(uint8_t* control)
{
	volatile long* uses = reinterpret_cast<volatile long*>(control + kControlUses);
	for (long seen = *uses; seen > 1; seen = *uses) {
		if (_InterlockedCompareExchange(uses, seen - 1, seen) == seen) return true;
	}
	return false;
}

// Heap and code addresses of a Windows x64 process (1 TB up to the end of
// user space), 4-byte aligned. They differ between processes even when the
// state is the same, so hashes read them as zero.
inline bool LooksLikeAddress(uint64_t value)
{
	return value >= (1ull << 40) && value <= 0x00007FFFFFFFFFFFull && (value & 3) == 0;
}

} // namespace sfve::mod
