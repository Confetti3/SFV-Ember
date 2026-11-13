#pragma once

// Skipping the fighters' intros in a linked battle, on both copies on the
// same frame (EMBER_LIBRARY.md section 50).
//
// Offline, the intro (flow state IntroDemo) ends early when the battle's
// input for a tick carries the game's skip bit (sfv::intro, SfvBattleLayouts).
// Linked, a frame outside the fight gets neutral inputs (the game ignores
// them, so no rollback starts there), so no press ever reached the intro.
//
// Now the skip bit rides in the exchanged input: each copy sends it from its
// player's first new press in the intro (the skip button, or any attack, as
// SF4 Ember's intros take any button) to the intro's end. The game reads it
// only on skip frames, one session frame in kEvery while the flow says
// IntroDemo, and those frames are stepped only once their inputs are
// confirmed (GameAdapter::ConfirmedBits). Both fighters, the watchers and a
// replay's playback then give the game the same bits on the same frame, so
// the intro ends on the same tick everywhere, whichever player pressed. The
// cost: at most one frame in kEvery waits for the other copy's input, and a
// skip comes within kEvery frames of the press reaching both copies.
//
// The scenes after a decided match (the last KO's finish, the winner's demo,
// the way to the results) take the same press on the same skip frames
// (section 52). Unlike the intro, the game itself does not end them on the
// bit: of those flow states only the winner's demo asks for it, behind a gate
// a room's battle keeps shut (docs/design/beta8/skip.md, SfvResultFlow.hxx).
// So the adapter ends them itself (SfvAdapter::Advance): on a frame whose
// given inputs carry the bit, before the frame's update, it moves the current
// state on the way the game's own phases do (ScenesStepFor). The frame is
// confirmed and the move a function of the state at its start and of those
// inputs, so the fighters, the watchers and a replay make it on the same
// frame; the flow is in the rollback state. The winner's demo then waits for
// SFV's results screens (the win quote, then the results card), whose pages
// turn on any button or after 600 ticks each: a question for any button hears
// the request as the press it stands for (PadsHear), inside the tick, from
// the same confirmed inputs. Either player's press skips for both.
//
// The scenes between two rounds (the KO's finish, RoundOver, the round's
// demo; RoundScenes) take the same press, guard and rule: the frame ends
// Finish as at the match's end, and the round demos end on the confirmed
// bit by themselves (they read the battle's input before the decision). The
// next round's call (RoundStart, Ready) is not skipped.
//
// A copy asks only for a new press made at least kScenesGuardFrames after it
// saw the scenes start, so a button still down from the last hit, or mashed
// through the KO, skips nothing.
//
// A copy never asks on its own: no room rule, public room or private, makes
// the scenes end without a press (docs/design/beta9/skip.md; the beta.8
// Skip winner scenes rule is gone). Watchers and a replay's playback do not
// ask at all: they follow the fighters' confirmed skip frames, so a replay
// recorded under that rule plays the skips it recorded.
//
// Once the match is decided the game asks the pads themselves for the skip
// bit (sfv::kPadsAny), so each copy heard only its own player; while a
// session's inputs are injected that question is answered from the battle's
// input (GameHooks).

#include "core/GameAdapter.hxx"
#include "mod/GameInput.hxx"
#include "mod/SfvBattleLayouts.hxx"
#include "mod/SfvResultFlow.hxx"

#include <stdint.h>

namespace sfve::mod::intro_skip {

// The game's own skip bit: what the intro reads, and what is sent.
constexpr PackedInput kSkipBit = sfv::intro::kSkipBit;
static_assert((kSkipBit & input::kInputBits) == 0, "the skip bit is not one a fight frame reads");

// A press asks for the skip: the game's skip button or any attack.
constexpr PackedInput kPressBits =
	kSkipBit | input::kLP | input::kMP | input::kLK | input::kMK | input::kHP | input::kHK;

// One session frame in this many reads the skip bit while in the intro.
constexpr int kEvery = 15;

// Session frames of a decided match's scenes a copy shows before its
// player's press there asks for the skip: about a second of the KO.
constexpr int kScenesGuardFrames = 60;

// The scenes between two rounds: Finish, RoundOver and the round's demo
// (sfv::resultflow), from the KO (or time-over) that ends a round to the
// next round's call, which they are not. Only a round's end reaches these
// states, and with the match decided they are its scenes anyway.
constexpr bool RoundScenes(int flowState)
{
	namespace rf = sfv::resultflow;
	return flowState == rf::kFinish || flowState == rf::kRoundOver || flowState == rf::kRoundResultDemo ||
		flowState == rf::kRoundDoubleKODemo;
}

// A press skips what flow state `flowState` (the one the flow last
// announced) shows: the intro, the scenes between two rounds, or with the
// match decided (sfv::battledata::kDecided) anything but the fight. Both are
// part of the rollback state, so every copy answers alike at a frame's start.
constexpr bool Skippable(int flowState, bool decided)
{
	return flowState == sfv::flow::kIntroDemo || RoundScenes(flowState) || (decided && flowState != sfv::flow::kFight);
}

// The scenes a press (after the guard) skips: a decided match's (anything
// but the fight once it is decided) and those between two rounds. Not the
// intro, which takes a press at once.
constexpr bool Scenes(int flowState, bool decided)
{
	return (decided && flowState != sfv::flow::kFight) || RoundScenes(flowState);
}

// Session frame `frame`, starting from that state, reads the skip bit.
constexpr bool SkipFrame(int flowState, int frame, bool decided = false)
{
	return Skippable(flowState, decided) && frame >= 0 && frame % kEvery == 0;
}

// The bits that frame reads confirmed (GameAdapter::ConfirmedBits).
constexpr PackedInput ConfirmedBits(int flowState, int frame, bool decided = false)
{
	return SkipFrame(flowState, frame, decided) ? kSkipBit : 0;
}

// Every attack: what the game asks for when it waits for any button. SFV's
// results screens (the ResultUnit's sequence, which MatchResultDemo's phase 6
// waits for) turn a page on that (rva 0x49FB00, and 0x4920A0, 0x492900,
// 0x492EF0, 0x49F970, 0x4A1230, 0x4B7D60 in other modes ask the same, mask
// 0xFFFFBFF0, which holds every attack but not the skip bit).
constexpr PackedInput kAttackBits = kPressBits & ~kSkipBit;

// What a question to the pads (sfv::kPadsAny) hears while a session's inputs
// play the tick: both players' bits of `mask` (`given`, the frame's given
// inputs ORed), and, once the match is decided, the skip request as the press
// it stands for when the question is for any button. The request is the skip
// bit, which reaches the game only on a decided match's confirmed skip frames
// (GivenInputs), so every copy hears the same on the same tick.
constexpr bool PadsHear(PackedInput given, uint32_t mask, bool decided)
{
	if ((given & mask) != 0) return true;
	return decided && (given & kSkipBit) != 0 && (mask & kAttackBits) == kAttackBits;
}

// The session frame a copy first saw the decided match's scenes, from the
// frames it steps (or watches); -1 outside them. A rollback that takes the
// deciding hit back shows the fight again, which starts the count over.
class ScenesClock {
public:
	void Note(bool scenes, int frame)
	{
		if (!scenes) from_ = -1;
		else if (from_ < 0) from_ = frame;
	}
	int From() const { return from_; }
	// Session frames shown of them by `frame`; 0 outside them.
	int Shown(int frame) const { return from_ < 0 ? 0 : frame - from_; }

private:
	int from_ = -1;
};

// This copy's skip request: what it sends for each frame it steps.
class Request {
public:
	// `local` (the player's input for session frame `frame`, the game's value
	// of the pad or a test's script) with the skip bit set from the first new
	// press in the intro (or in a decided match's scenes, once they have
	// shown kScenesGuardFrames) to its end, and clear otherwise. A button
	// already down as it starts (the one that landed the last hit, say) is not
	// a press until it is released; a press in the scenes' first frames asks
	// nothing, and is not taken up later.
	PackedInput Next(int flowState, PackedInput local, bool decided = false, int frame = 0)
	{
		const bool scenes = Scenes(flowState, decided);
		clock_.Note(scenes, frame);
		const bool guarded = scenes && clock_.Shown(frame) < kScenesGuardFrames;
		const PackedInput down = local & kPressBits;
		// Only a new press asks, and never inside the guard: the scenes do not
		// end by themselves however long they run.
		const bool pressed = (down & ~down_) != 0 && !guarded;
		requested_ = Skippable(flowState, decided) && (requested_ || pressed);
		down_ = down;
		return (local & ~kSkipBit) | (requested_ ? kSkipBit : 0);
	}
	bool Requested() const { return requested_; }
	// The frame this copy first saw the decided match's scenes; -1 outside them.
	int ScenesFrom() const { return clock_.From(); }

private:
	bool requested_ = false;
	PackedInput down_ = kPressBits; // as if all down: nothing held before the first call is a press
	ScenesClock clock_;
};

// A test's scripted skip press (the pipe's "skipafter <n>", runroom's
// -skipAfter): the frames from `after` frames into the scenes, for
// kScriptedPressFrames, press an attack. `after` < 0: never.
constexpr int kScriptedPressFrames = 6;
constexpr bool ScriptedPress(int scenesFrom, int frame, int after)
{
	return after >= 0 && scenesFrom >= 0 && frame >= scenesFrom + after && frame < scenesFrom + after + kScriptedPressFrames;
}

// --- Ending the decided match's scenes (SfvAdapter::Advance).

// What a frame carrying the skip bit does to the flow state it starts in.
enum class ScenesAction {
	None,        // nothing of this state is ended yet (a later skip frame may)
	RequestNext, // Finish: ask the flow for the state its last phase asks for (RoundOver)
	ResultSkip,  // MatchResultDemo's phase 3: what its own skip does, then `phase`
	SetPhase,    // move the state to `phase`, as its own phases do
};

struct ScenesStep {
	ScenesAction action = ScenesAction::None;
	int phase = -1;
	bool operator==(const ScenesStep&) const = default;
};

// The battle frame's state bit the KO's slow motion sets (+0x190 bit 0x20,
// sfv::battleframe::kState; the pause menu's detector reads it too, rva
// 0x46E110). Finish is not cut while it is set (ScenesStepFor's `koStop`).
constexpr uint32_t kKoStopBit = 0x20;

// The step for the current state: its id (resultflow::kStateId), its class
// (its vtable's rva) and its phase (flowstate::kPhase). Only the classes the
// versus flow builds for these ids are moved; anything else plays on.
// - Finish: whatever it waits on (the KO's presentation), on to RoundOver,
//   which asks for the winner's or the draw's demo on its first tick. Not
//   while `koStop` (the KO's slow motion, kKoStopBit): a round that ends in a
//   KO is cut only once the game has cleared it itself, since the step Finish
//   makes on its way out (docs/design/beta9/skip.md section 2) is not known
//   to run on a cut. A time-over has no KO presentation, so nothing waits.
//   SfvAdapter::EndScenes passes it from the battle frame (beta.9 patch).
// - MatchResultDemo: its 90-tick wait before the demo's setup ends at once
//   (phase 2 is the same setup without the wait); in phase 3, the skip the
//   game makes when its gate is open (phase 4, then the results' event and
//   the wait for the results' unit, as unskipped). Phases 0 and 2 run at
//   once, and from phase 4 on it is ending already.
// - RoundResultDemo, RoundDoubleKODemo: nothing; they end on the skip bit by
//   themselves (sfv::resultflow), on the same confirmed frame.
// - MatchDrawDemo: the wait for its demo ends (phase 3, which goes to 4 at
//   once), and its 300-tick wait (phase 5, the results' event).
constexpr ScenesStep ScenesStepFor(int stateId, uint32_t vtableRva, int phase, bool koStop = false)
{
	namespace rf = sfv::resultflow;
	if (stateId == rf::kFinish && vtableRva == rf::kFinishVtable) {
		return koStop ? ScenesStep{} : ScenesStep{ScenesAction::RequestNext, -1};
	}
	if (stateId == rf::kMatchResultDemo && vtableRva == rf::kMatchResultDemoVtable) {
		if (phase == rf::kResultSetupWaitPhase) return {ScenesAction::SetPhase, rf::kResultSetupPhase};
		if (phase == rf::kResultSkipPhase) return {ScenesAction::ResultSkip, rf::kResultSkippedPhase};
	}
	if (stateId == rf::kMatchDrawDemo && vtableRva == rf::kMatchDrawDemoVtable) {
		if (phase == rf::kDrawDemoPhase) return {ScenesAction::SetPhase, rf::kDrawAfterDemoPhase};
		if (phase == rf::kDrawWaitPhase) return {ScenesAction::SetPhase, rf::kDrawResultPhase};
	}
	return {};
}

} // namespace sfve::mod::intro_skip
