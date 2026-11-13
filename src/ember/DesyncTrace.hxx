#pragma once

// What a desync's report needs of the frames before it, which no part of the
// session keeps for long: both players' inputs for the last kInputFrames
// frames, and the session's rollbacks by frame. Kept in small rings from the
// first frame of a battle; read once, as the mismatch is found
// (DesyncReporter's Gather).
//
// * Inputs: the session reports each frame it plays (RollbackSession::Config
//   ::onFrame), a rollback's replay of a frame again, so a ring keeps the last
//   report of each frame, as the replay recorder does. The recorder is no
//   source: it runs only for a battle that is recorded (a watched one only
//   with Settings' "Save matches you watch", never a Practice room's). The
//   netplay mode hands each reported frame to NoteFrame too, recorded or not
//   (the one hook, in src/mod/NetplayMode.cxx where onFrame is set: call
//   sfve::adapter::Trace().NoteFrame(frame, played) for every room battle).
//   Until it does, a report says its inputs were not recorded.
// * Rollbacks: SfvVerifier::Step calls NoteStep once a step with the
//   session's counters; the difference to the last call is that step's
//   rollbacks and re-simulated frames. The longest rollback of a window is the
//   most frames any one step re-simulated (two rollbacks in one step count as
//   one deeper one).
//
// Game thread only, like the session. Header-only so the test target needs
// nothing more.

#include "core/GameAdapter.hxx"
#include "core/ReplayFrames.hxx"
#include "ember/DesyncReport.hxx"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace sfve::adapter {

class DesyncTrace {
public:
	// How many frames of each kind are kept: more than a report reads, for the
	// distance between a checkpoint and the frame its mismatch is found at.
	static constexpr int kKeptFrames = 512;

	// A frame the session played (or played again after a rollback): the last
	// report of a frame stands. A frame far behind the newest is a new battle's
	// (a rollback goes back a few frames, never this far): everything before it
	// goes.
	void NoteFrame(int frame, const RecordedFrame& played)
	{
		if (frame < 0) return;
		if (frame + kKeptFrames / 2 < newest_) ClearFrames();
		Slot& slot = frames_[static_cast<std::size_t>(frame) % kKeptFrames];
		slot.frame = frame;
		slot.inputs = played.inputs;
		slot.flags = played.flags;
		newest_ = (std::max)(newest_, frame);
	}

	// One step of the session: its frame now and its running counters. A
	// counter that went back is a new session's: the rings start over.
	void NoteStep(int frame, std::uint64_t rollbacks, std::uint64_t resimulated)
	{
		if (rollbacks < lastRollbacks_ || resimulated < lastResimulated_ || frame + kKeptFrames / 2 < lastStepFrame_) {
			ClearSteps();
		}
		Step& step = steps_[next_++ % kKeptSteps];
		step.frame = frame;
		step.rollbacks = rollbacks - lastRollbacks_;
		step.resimulated = resimulated - lastResimulated_;
		lastRollbacks_ = rollbacks;
		lastResimulated_ = resimulated;
		lastStepFrame_ = frame;
	}

	// The rollbacks of the frames (to - frames, to], as the steps that played
	// them saw. Known only when the ring reaches back that far (the battle is at
	// least that old, or the ring holds `frames` of its frames).
	desync::RollbackFacts Recent(int to, int frames) const
	{
		desync::RollbackFacts out;
		out.from = (std::max)(0, to - frames);
		out.to = to;
		out.known = next_ > 0;
		const std::size_t held = (std::min<std::size_t>)(next_, kKeptSteps);
		for (std::size_t i = 0; i < held; ++i) {
			const Step& step = steps_[(next_ - 1 - i) % kKeptSteps];
			if (step.frame > to || step.frame <= to - frames) continue;
			out.count += step.rollbacks;
			if (step.rollbacks) out.longest = (std::max)(out.longest, static_cast<int>(step.resimulated));
		}
		return out;
	}

	// Both players' inputs for the frames [from, end) in runs of equal inputs;
	// unknown when no frame of them was noted. Frames the ring lost (older than
	// kKeptFrames behind the newest) are left out and `first` moves up to the
	// first frame held.
	desync::InputFacts Inputs(int from, int end) const
	{
		desync::InputFacts out;
		from = (std::max)(from, 0);
		for (int frame = from; frame < end; ++frame) {
			const Slot& slot = frames_[static_cast<std::size_t>(frame) % kKeptFrames];
			if (slot.frame != frame) {
				// A gap: a frame never played (before the battle) or lost.
				if (out.runs.empty()) continue;
				break;
			}
			if (out.runs.empty()) out.first = frame;
			out.end = frame + 1;
			if (!out.runs.empty()) {
				desync::InputRun& last = out.runs.back();
				if (last.first + last.frames == frame && last.p1 == slot.inputs.p[0] && last.p2 == slot.inputs.p[1] &&
					last.flags == slot.flags) {
					last.frames++;
					continue;
				}
			}
			out.runs.push_back({frame, 1, slot.inputs.p[0], slot.inputs.p[1], slot.flags});
		}
		out.known = !out.runs.empty();
		return out;
	}

	void Reset()
	{
		ClearFrames();
		ClearSteps();
	}

private:
	static constexpr std::size_t kKeptSteps = 1024;
	struct Slot {
		int frame = -1;
		FrameInputs inputs;
		std::uint8_t flags = 0;
	};
	struct Step {
		int frame = -1;
		std::uint64_t rollbacks = 0;
		std::uint64_t resimulated = 0;
	};
	void ClearFrames()
	{
		for (Slot& slot : frames_) slot = Slot{};
		newest_ = -1;
	}
	void ClearSteps()
	{
		for (Step& step : steps_) step = Step{};
		next_ = 0;
		lastRollbacks_ = lastResimulated_ = 0;
		lastStepFrame_ = -1;
	}

	std::array<Slot, kKeptFrames> frames_{};
	int newest_ = -1;
	std::array<Step, kKeptSteps> steps_{};
	std::size_t next_ = 0;
	std::uint64_t lastRollbacks_ = 0;
	std::uint64_t lastResimulated_ = 0;
	int lastStepFrame_ = -1;
};

// The one trace of the battle in play (the game thread's).
inline DesyncTrace& Trace()
{
	static DesyncTrace trace;
	return trace;
}

} // namespace sfve::adapter
