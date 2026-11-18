#include "mod/ScenesSkip.hxx"

#include "core/GameAdapter.hxx"
#include "mod/GameMemory.hxx"
#include "mod/IntroSkip.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <stdio.h>

namespace sfve::mod {

void ScenesSkip::Bind(uintptr_t base)
{
	imageBase_ = base;
	flowRequest_ = reinterpret_cast<decltype(flowRequest_)>(base + sfv::kFlowRequest.rva);
	setFlowPhase_ = reinterpret_cast<decltype(setFlowPhase_)>(base + sfv::kSetFlowPhase.rva);
	markResultSkipped_ = reinterpret_cast<decltype(markResultSkipped_)>(base + sfv::kMarkResultSkipped.rva);
}

void ScenesSkip::Forget()
{
	scenesSkip_ = nullptr;
	scenesIdleState_ = scenesIdlePhase_ = -1;
}

void ScenesSkip::Apply(uint8_t* flow, uint8_t* battleFrame, const FighterUnit* units, int count)
{
	namespace rf = sfv::resultflow;
	using intro_skip::ScenesAction;
	uint8_t* state = flow ? Field<GameSharedPtr>(flow, sfv::flowunit::kCurrent).object : nullptr;
	if (!state || !flowRequest_ || !setFlowPhase_ || !markResultSkipped_) return;
	const int32_t id = Field<int32_t>(state, rf::kStateId);
	const uint32_t vtable = (uint32_t)(Field<uintptr_t>(state, 0) - imageBase_);
	const int32_t phase = Field<int32_t>(state, sfv::flowstate::kPhase);
	// The KO's slow motion (battle frame state word, bit 0x20) holds Finish: a
	// round that ends in a KO is not cut while the game still plays it
	// (docs/design/beta9/skip.md section 2). The word is in the rollback state,
	// so both copies read the same one on the same frame.
	const uint32_t stateWord = battleFrame ? Field<uint32_t>(battleFrame, sfv::battleframe::kState) : 0;
	const bool koStop = battleFrame && (stateWord & intro_skip::kKoStopBit) != 0;
	// Ticks the phase has run: the sequencer's elapsed (in the flow record).
	const int phaseTicks = (int)Field<float>(state, sfv::flowstate::kElapsed);
	const intro_skip::ScenesStep step = intro_skip::ScenesStepFor(id, vtable, phase, koStop, phaseTicks);
	// Finish's last phase asks for the state the constructor gave it; the
	// skip asks for that one only when it is RoundOver, as the versus flow's.
	const bool unexpectedNext = step.action == ScenesAction::RequestNext && Field<int32_t>(state, rf::kNextState) != rf::kRoundOver;
	if (step.action == ScenesAction::None || unexpectedNext) {
		// Logged once per state and phase: the skip frames come every 15.
		if (id != scenesIdleState_ || phase != scenesIdlePhase_) {
			scenesIdleState_ = id;
			scenesIdlePhase_ = phase;
			if (unexpectedNext) {
				Log("scene skip: Finish asks for flow state %d next, not RoundOver (%d); it plays on",
					Field<int32_t>(state, rf::kNextState), rf::kRoundOver);
			} else {
				// MatchResultDemo's phase 6 waits for the results screens, whose
				// pages hear the bit as a press (intro_skip::PadsHear).
				Log("scene skip: the skip bit in flow state %d (class rva 0x%X) phase %d moves no phase here; the "
					"game's own questions for a press hear it; battle frame state word %08x", id, vtable, phase, stateWord);
			}
		}
		return;
	}
	scenesIdleState_ = scenesIdlePhase_ = -1;
	bool done = false;
	switch (step.action) {
	case ScenesAction::RequestNext:
		// What Finish's last phase does (rva 0x40EBE0): the flow switches to
		// RoundOver on this frame's update, which asks for the demo on its
		// first tick.
		done = flowRequest_(flow, Field<int32_t>(state, rf::kNextState), (uint8_t)Field<int32_t>(state, rf::kNextFlags),
				   Field<int32_t>(state, sfv::flowstate::kArg)) != 0;
		snprintf(scenesSkipText_, sizeof(scenesSkipText_), "Finish ends in its phase %d: the flow %s RoundOver", phase,
			done ? "asked for" : "refused");
		// The cut skips Finish's phase 12, whose last act is rva 0x3074D0
		// (fighters, -1): each fighter unit's dword +2376 set to -1 (skip.md,
		// "Settled in IDA"). What `fighters` is (the state's list at
		// +272..+280) was not read to its element type, so the writes are
		// made through the field helpers to the two fighters' units. A
		// confirmed frame on every copy, in the rollback state.
		if (done) {
			for (int i = 0; i < count && i < kPlayers; i++) {
				if (units[i].chara) Field<int32_t>(units[i].chara, sfv::fighter::kFinishMask) = sfv::fighter::kFinishMaskValue;
			}
		}
		break;
	case ScenesAction::ResultSkip: {
		// What phase 3 (rva 0x409930) does on the skip bit when its gate is
		// open, here whatever the gate says: the skip's mark, skipped, phase 4,
		// which runs on at once to the results' event and the wait for them.
		const int32_t gate = Field<int32_t>(state, rf::kResultSkipGate);
		markResultSkipped_(flow + sfv::unit::kContext);
		Field<int32_t>(state, rf::kResultSkipped) = 1;
		done = setFlowPhase_(state, step.phase) != 0;
		snprintf(scenesSkipText_, sizeof(scenesSkipText_),
			"the winner's demo skipped from phase %d to %d (its own gate %d)%s, now in phase %d", phase, step.phase, gate,
			done ? "" : ": the phase was refused", Field<int32_t>(state, sfv::flowstate::kPhase));
		break;
	}
	case ScenesAction::SetPhase:
		done = setFlowPhase_(state, step.phase) != 0;
		snprintf(scenesSkipText_, sizeof(scenesSkipText_), "flow state %d moved from phase %d to %d%s, now in phase %d",
			id, phase, step.phase, done ? "" : ": the phase was refused", Field<int32_t>(state, sfv::flowstate::kPhase));
		break;
	case ScenesAction::None: break;
	}
	Log("scene skip: %s, battle frame state word %08x", scenesSkipText_, stateWord);
	scenesSkip_ = scenesSkipText_;
}

} // namespace sfve::mod
