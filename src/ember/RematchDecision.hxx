#pragma once

// What the two fighters of an SFV room game answer over its WIN, and what
// both copies do with it (EMBER_LIBRARY.md section 19).
//
// SF4's WIN is the game's own wait, which the answers hold or cut short
// (common/RematchInput.hxx). SFV's results are skipped (MatchEnd), so the
// wait is the mod's: the battle stands still and the rollback session goes
// on in held frames (RollbackSession::StepHeld), each stepped only once its
// inputs are confirmed. Each fighter's answer rides in those inputs, above
// the bits a pad makes (GameInput's kInputBits), so both copies read the
// same answers on the same frame and decide alike: Rematch, the game's own
// restart in this battle and session; or Leave, back to the room.
//
// The bits are the library's (rematch::ChoiceBit, AskingBit, set from the
// room's parked Rematch and the prompt's time by the runtime's
// ReadRuntimeMatchInput) plus SFV's own DeclineBit: this fighter answered
// Back to room or Character select, or its copy offers no rematch here (the
// table's Instant rematch is off, the set is over), so both copies leave
// at once even when their rooms do not yet agree on the offer.

#include "common/RematchInput.hxx"

#include <cstdint>

namespace sfve::adapter::rematch_in_place {

constexpr std::uint32_t kDeclineBit = 0x10000000;
constexpr std::uint32_t kBits = sf4e::rematch::Bits | kDeclineBit;

// Held frames before "nobody is asked" ends the wait: the prompt opens on the
// first held frame, and its first answers arrive an input delay later.
constexpr int kOpenFrames = 60;
// Held frames after which the wait ends whatever the answers: past the
// prompt's own time (rematch::AskingMs, 11 s) at the game's 60 frames a second.
constexpr int kMostFrames = 15 * 60;

enum class Answer { Wait, Rematch, Leave };

// One held frame's confirmed inputs, `held` frames after the WIN began.
inline Answer Decide(std::uint32_t p1, std::uint32_t p2, int held)
{
	namespace rematch = sf4e::rematch;
	if ((p1 | p2) & kDeclineBit) return Answer::Leave;
	if ((p1 & rematch::ChoiceBit) && (p2 & rematch::ChoiceBit)) return Answer::Rematch;
	if (held >= kMostFrames) return Answer::Leave;
	if (held < kOpenFrames) return Answer::Wait;
	// A fighter still asked holds the wait; with nobody asked and not both
	// chosen, the prompt's time is up.
	return (p1 | p2) & rematch::AskingBit ? Answer::Wait : Answer::Leave;
}

// This copy's bits for a held frame: the runtime's (`runtime`, what
// ReadRuntimeMatchInput added), or a decline when this copy offers no
// rematch or its fighter declined.
inline std::uint32_t LocalBits(bool offered, bool declined, std::uint32_t runtime)
{
	if (!offered || declined) return kDeclineBit;
	return runtime & sf4e::rematch::Bits;
}

// Each side whose input chose Rematch, as the library's RematchChoices reads it.
inline unsigned Choices(std::uint32_t p1, std::uint32_t p2)
{
	return (p1 & sf4e::rematch::ChoiceBit ? 1u : 0u) | (p2 & sf4e::rematch::ChoiceBit ? 2u : 0u);
}

// Each side whose input declined, as the library's RematchDeclines reads it.
inline unsigned Declines(std::uint32_t p1, std::uint32_t p2)
{
	return (p1 & kDeclineBit ? 1u : 0u) | (p2 & kDeclineBit ? 2u : 0u);
}

} // namespace sfve::adapter::rematch_in_place
