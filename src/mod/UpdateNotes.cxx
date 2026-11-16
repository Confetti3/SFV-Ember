#include "mod/UpdateNotes.hxx"

#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

namespace sfve::mod {

void UpdateNotes::Forget()
{
	if (queuesCarried_) Log("actions: the battle ends: %llu time(s) a fighter unit's command queue was not empty between updates",
		(unsigned long long)queuesCarried_);
	queuesCarried_ = 0;
	queueLines_ = 0;
	decidedTicks_ = -1;
	pausedBits_ = 0;
}

void UpdateNotes::CommandQueues(const FighterUnit* units, int count, int inputFrame, bool replaying)
{
	namespace f = sfv::fighter;
	for (int i = 0; i < count; i++) {
		const uint8_t* action = units[i].action;
		uintptr_t begin = 0, end = 0;
		if (!action || !TryRead(action + f::kCommandQueueBegin, &begin) || !TryRead(action + f::kCommandQueueEnd, &end) || end <= begin) {
			continue;
		}
		queuesCarried_++;
		if (queueLines_++ < kMostQueueLines) {
			Log("actions: frame %d%s: CharaUnit[%d]'s command queue holds %llu command(s) after the update (outside the snapshot: a "
				"restore would keep them)",
				inputFrame, replaying ? " replayed" : "", i,
				(unsigned long long)((end - begin) / sizeof(void*)));
		}
	}
}

void UpdateNotes::DecidedTicks(bool decided, uint32_t ticksInLastUpdate, int flowState)
{
	if (!decided) {
		decidedTicks_ = -1;
		return;
	}
	if (decidedTicks_ == (int64_t)ticksInLastUpdate) return;
	Log("adapter: %u input tick(s) in this update with the match decided (flow state %d; before, %lld)", ticksInLastUpdate,
		flowState, (long long)decidedTicks_);
	decidedTicks_ = ticksInLastUpdate;
}

void UpdateNotes::Pause(uint8_t* battleFrame, int flowState)
{
	// SFV's own pause never runs in a session's battle (GameHooks keeps the
	// PauseUnit's task from it, sfv::pause); a pause bit here would be one
	// copy's alone. Logged on each change.
	// The mask takes the KO's bits too (0x30: the KO's slow motion, 0x20, and
	// its neighbour) so a round's ending shows in the log (skip.md section 2).
	const uint32_t bits = Field<uint32_t>(battleFrame, sfv::battleframe::kState) & (sfv::pause::kBits | 0x30);
	if (bits == pausedBits_) return;
	Log("adapter: the game's pause bits are %x in a session's battle (were %x; flow state %d)", bits, pausedBits_,
		flowState);
	pausedBits_ = bits;
}

} // namespace sfve::mod
