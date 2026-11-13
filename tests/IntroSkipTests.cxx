// The intro's skip (mod/IntroSkip.hxx): the bit both copies exchange, the
// frames the game reads it on, and each copy's request; and the decided
// match's scenes: the request's guard, and how a skip frame ends each state.

#include <doctest/doctest.h>

#include "core/GameAdapter.hxx"
#include "ember/RematchDecision.hxx"
#include "mod/IntroSkip.hxx"

using namespace sfve;
namespace intro_skip = sfve::mod::intro_skip;
namespace input = sfve::mod::input;
namespace rip = sfve::adapter::rematch_in_place;
using intro_skip::kSkipBit;

namespace {

constexpr int kIntro = sfve::mod::sfv::flow::kIntroDemo;
constexpr int kFight = sfve::mod::sfv::flow::kFight;
constexpr int kMatchStart = 2; // the state before the intro

// A battle that stays in the intro: reads no inputs but the intro's skip
// request on its skip frames, as SfvAdapter does there.
class InIntro : public GameAdapter {
public:
	std::string BuildId() const override { return "intro"; }
	StateLayout& Layout() override { return layout_; }
	void Advance(const FrameInputs&, bool) override {}
	bool InputsMatter() const override { return false; }
	PackedInput InputBits() const override { return input::kInputBits; }
	PackedInput ConfirmedBits(int frame) const override { return intro_skip::ConfirmedBits(kIntro, frame); }

private:
	StateLayout layout_;
};

} // namespace

TEST_CASE("the skip bit is the game's own, and no bit a fight frame or the WIN reads")
{
	CHECK(kSkipBit == 0x4000u);
	CHECK((kSkipBit & input::kInputBits) == 0);
	CHECK((kSkipBit & rip::kBits) == 0);
	CHECK((intro_skip::kPressBits & ~kSkipBit) == (input::kLP | input::kMP | input::kLK | input::kMK | input::kHP | input::kHK));
}

TEST_CASE("the intro reads the skip bit on one frame in 15, and nothing else does")
{
	for (int frame = 0; frame <= 90; frame++) {
		INFO("frame " << frame);
		const bool skip = frame % intro_skip::kEvery == 0;
		CHECK(intro_skip::SkipFrame(kIntro, frame) == skip);
		CHECK(intro_skip::ConfirmedBits(kIntro, frame) == (skip ? kSkipBit : 0u));
		// Nor the next round's call, the fight, or a decided match's states
		// before the decision (the scenes between rounds have their own case).
		for (const int other : {0, kMatchStart, 4, 5, kFight, 11, 12, 13, 14, 17, 21, 22, -1}) {
			CHECK_FALSE(intro_skip::SkipFrame(other, frame));
			CHECK(intro_skip::ConfirmedBits(other, frame) == 0u);
		}
	}
	CHECK_FALSE(intro_skip::SkipFrame(kIntro, -15));
}

TEST_CASE("a skip frame gives the game both players' skip bits and nothing else, and plays back unchanged")
{
	InIntro game;
	const FrameInputs sent{{kSkipBit | input::kLP | input::kRight, input::kHP | 0x20000000u}};
	const FrameInputs onlySkip{{kSkipBit, 0}};
	const FrameInputs skip = GivenInputs(game, sent, 30);
	CHECK(skip == onlySkip);
	CHECK(ReadsConfirmed(game, 30));
	// What a replay recorded plays back as it was given.
	CHECK(GivenInputs(game, skip, 30) == skip);
	// The other intro frames get neutral inputs and need no confirmation.
	const FrameInputs neutral;
	CHECK(GivenInputs(game, sent, 31) == neutral);
	CHECK_FALSE(ReadsConfirmed(game, 31));
}

TEST_CASE("a request starts at a new press in the intro and lasts to the intro's end")
{
	intro_skip::Request request;
	CHECK(request.Next(kIntro, 0) == 0u);
	CHECK(request.Next(kIntro, input::kRight) == input::kRight); // a direction asks nothing
	CHECK_FALSE(request.Requested());
	CHECK(request.Next(kIntro, input::kLP) == (kSkipBit | input::kLP));
	CHECK(request.Requested());
	CHECK(request.Next(kIntro, 0) == kSkipBit); // released: still asked
	CHECK(request.Next(kIntro, input::kDown) == (kSkipBit | input::kDown));
	CHECK(request.Next(kMatchStart, 0) == 0u); // the intro is over
	CHECK_FALSE(request.Requested());
	CHECK(request.Next(kFight, input::kHK) == input::kHK);
}

TEST_CASE("a button already down as the intro starts asks only once released and pressed again")
{
	intro_skip::Request request;
	CHECK(request.Next(kMatchStart, input::kHP) == input::kHP);
	CHECK(request.Next(kIntro, input::kHP) == input::kHP);
	CHECK(request.Next(kIntro, input::kHP) == input::kHP);
	CHECK(request.Next(kIntro, 0) == 0u);
	CHECK(request.Next(kIntro, input::kHP) == (kSkipBit | input::kHP));
	// Down at the first frame this copy sends, in the intro itself.
	intro_skip::Request fresh;
	CHECK(fresh.Next(kIntro, input::kMK) == input::kMK);
	CHECK(fresh.Next(kIntro, input::kMK | input::kLK) == (kSkipBit | input::kMK | input::kLK)); // another button is new
}

TEST_CASE("the skip bit sent is the request alone, whatever the pad's own skip button says")
{
	intro_skip::Request request;
	CHECK(request.Next(kFight, kSkipBit) == 0u); // outside the intro it asks nothing
	CHECK(request.Next(kIntro, kSkipBit) == 0u); // held into the intro
	CHECK(request.Next(kIntro, 0) == 0u);
	CHECK(request.Next(kIntro, kSkipBit) == kSkipBit); // the game's skip button, pressed
	CHECK(request.Next(kIntro, 0) == kSkipBit);
}

TEST_CASE("a decided match's scenes read the skip bit like the intro, and its fight never does")
{
	constexpr int kFinish = 7; // any state but the fight
	for (int frame = 0; frame <= 60; frame++) {
		INFO("frame " << frame);
		const bool every = frame % intro_skip::kEvery == 0;
		CHECK(intro_skip::SkipFrame(kFinish, frame, true) == every);
		CHECK(intro_skip::ConfirmedBits(kFinish, frame, true) == (every ? kSkipBit : 0u));
		CHECK(intro_skip::SkipFrame(kIntro, frame, true) == every);
		// The deciding KO's last fight frames still read the pads' inputs.
		CHECK_FALSE(intro_skip::SkipFrame(kFight, frame, true));
		CHECK(intro_skip::ConfirmedBits(kFight, frame, true) == 0u);
		// Between two rounds Finish's skip frames read it too (RoundScenes).
		CHECK(intro_skip::SkipFrame(kFinish, frame, false) == every);
	}
	CHECK(intro_skip::Skippable(kIntro, false));
	CHECK_FALSE(intro_skip::Skippable(kFight, true));
	CHECK_FALSE(intro_skip::Skippable(kMatchStart, false));
	CHECK(intro_skip::Skippable(kMatchStart, true));
}

TEST_CASE("after the deciding round a new press asks for the skip until the scenes end")
{
	constexpr int kFinish = 7;
	constexpr int kDecidedAt = 1000;
	constexpr int kGuardEnds = kDecidedAt + intro_skip::kScenesGuardFrames;
	intro_skip::Request request;
	// The button that landed the last hit is still down as the match is decided.
	CHECK(request.Next(kFight, input::kHP, false, kDecidedAt - 2) == input::kHP);
	CHECK(request.Next(kFight, input::kHP, true, kDecidedAt - 1) == input::kHP);
	CHECK(request.Next(kFinish, input::kHP, true, kDecidedAt) == input::kHP);
	CHECK(request.ScenesFrom() == kDecidedAt);
	CHECK(request.Next(kFinish, input::kHP, true, kGuardEnds) == input::kHP); // still held: no press
	CHECK_FALSE(request.Requested());
	CHECK(request.Next(kFinish, 0, true, kGuardEnds + 1) == 0u);
	CHECK(request.Next(kFinish, input::kLK, true, kGuardEnds + 2) == (kSkipBit | input::kLK));
	CHECK(request.Requested());
	CHECK(request.Next(kFinish, 0, true, kGuardEnds + 3) == kSkipBit); // released: still asked
	CHECK(request.Next(11, input::kLeft, true, kGuardEnds + 4) == (kSkipBit | input::kLeft)); // through the next scene
	// A rematch in place resets the match: nothing is decided, nothing asked.
	CHECK(request.Next(kMatchStart, 0, false, kGuardEnds + 5) == 0u);
	CHECK_FALSE(request.Requested());
	CHECK(request.ScenesFrom() == -1);
	// The same press at a later round's end falls inside that round's own guard.
	CHECK(request.Next(kFinish, input::kMP, false, kGuardEnds + 6) == input::kMP);
	CHECK_FALSE(request.Requested());
	CHECK(request.ScenesFrom() == kGuardEnds + 6);
}

TEST_CASE("a press in the scenes' first second asks nothing, then or later")
{
	constexpr int kFinish = 7;
	constexpr int kDecidedAt = 500;
	intro_skip::Request request;
	CHECK(request.Next(kFight, 0, false, kDecidedAt - 1) == 0u);
	int frame = kDecidedAt;
	// Mashed through the KO: every press inside the guard is ignored...
	for (; frame < kDecidedAt + intro_skip::kScenesGuardFrames; frame++) {
		INFO("frame " << frame);
		const PackedInput mash = (frame % 4 == 0) ? input::kMP : 0;
		CHECK(request.Next(kFinish, mash, true, frame) == mash);
		CHECK_FALSE(request.Requested());
	}
	// ...and not taken up once it is over: nothing new is down.
	CHECK(request.Next(kFinish, 0, true, frame++) == 0u);
	CHECK(request.Next(kFinish, 0, true, frame++) == 0u);
	CHECK_FALSE(request.Requested());
	// A press after it asks: here the game's own skip button.
	CHECK(request.Next(kFinish, kSkipBit, true, frame++) == kSkipBit);
	CHECK(request.Requested());
}

TEST_CASE("with no press the request is never set, however long the decided match's scenes run")
{
	constexpr int kFinish = 7;
	constexpr int kDecidedAt = 800;
	intro_skip::Request request;
	CHECK(request.Next(kFight, 0, false, kDecidedAt - 1) == 0u);
	// Far past the guard and past any scene's own length (the winner's demo, the
	// results' 600-tick pages), through every state the scenes pass: nothing asks.
	const int states[] = {kFinish, 11, 13, kFinish};
	for (int frame = kDecidedAt; frame < kDecidedAt + 5000; frame++) {
		INFO("frame " << frame);
		CHECK(request.Next(states[(frame / 1000) % 4], 0, true, frame) == 0u);
		CHECK_FALSE(request.Requested());
	}
	// The same with a stick held (not an attack): a direction is no press.
	for (int frame = kDecidedAt + 5000; frame < kDecidedAt + 5300; frame++) {
		CHECK(request.Next(kFinish, input::kLeft, true, frame) == input::kLeft);
		CHECK_FALSE(request.Requested());
	}
	// And the same between two rounds, the match not decided.
	namespace rf = sfve::mod::sfv::resultflow;
	intro_skip::Request round;
	CHECK(round.Next(kFight, 0, false, 99) == 0u);
	for (int frame = 100; frame < 100 + 3000; frame++) {
		const int state = (frame / 500) % 2 == 0 ? rf::kFinish : rf::kRoundResultDemo;
		CHECK(round.Next(state, 0, false, frame) == 0u);
		CHECK_FALSE(round.Requested());
	}
	// The intro, likewise: it ends on a press only.
	intro_skip::Request intro;
	for (int frame = 0; frame < 3000; frame++) CHECK(intro.Next(kIntro, 0, false, frame) == 0u);
	CHECK_FALSE(intro.Requested());
}

TEST_CASE("a button held through the guard counts only once released and pressed again")
{
	constexpr int kFinish = 7;
	intro_skip::Request request;
	int frame = 200;
	CHECK(request.Next(kFight, input::kLK, true, frame++) == input::kLK);
	for (int i = 0; i <= intro_skip::kScenesGuardFrames; i++) request.Next(kFinish, input::kLK, true, frame++);
	CHECK_FALSE(request.Requested());
	CHECK(request.Next(kFinish, input::kLK, true, frame++) == input::kLK);
	CHECK(request.Next(kFinish, 0, true, frame++) == 0u);
	CHECK(request.Next(kFinish, input::kLK, true, frame++) == (kSkipBit | input::kLK));
}

TEST_CASE("the scenes' clock starts at the first frame seen decided and starts over when the fight is back")
{
	constexpr int kFinish = 7;
	intro_skip::ScenesClock clock;
	CHECK(clock.From() == -1);
	CHECK(clock.Shown(100) == 0);
	clock.Note(intro_skip::Scenes(kFight, true), 99); // the deciding hit's frame is still the fight
	CHECK(clock.From() == -1);
	clock.Note(intro_skip::Scenes(kFinish, true), 100);
	clock.Note(intro_skip::Scenes(kFinish, true), 130);
	CHECK(clock.From() == 100);
	CHECK(clock.Shown(130) == 30);
	// A rollback took the hit back: the fight again, then a later decision.
	clock.Note(intro_skip::Scenes(kFight, false), 131);
	CHECK(clock.From() == -1);
	clock.Note(intro_skip::Scenes(kFinish, true), 140);
	CHECK(clock.From() == 140);
	CHECK_FALSE(intro_skip::Scenes(kIntro, false));
	CHECK(intro_skip::Scenes(kFinish, false)); // a round's end that decides nothing
	CHECK_FALSE(intro_skip::Scenes(4, false)); // the next round's call
}

TEST_CASE("a script's skip press comes the given number of frames into the scenes, for a few frames")
{
	CHECK_FALSE(intro_skip::ScriptedPress(100, 150, -1)); // off
	CHECK_FALSE(intro_skip::ScriptedPress(-1, 150, 0));   // outside the scenes
	CHECK_FALSE(intro_skip::ScriptedPress(100, 189, 90));
	for (int k = 0; k < intro_skip::kScriptedPressFrames; k++) CHECK(intro_skip::ScriptedPress(100, 190 + k, 90));
	CHECK_FALSE(intro_skip::ScriptedPress(100, 190 + intro_skip::kScriptedPressFrames, 90));
	// Pressed, released, pressed: a request when past the guard.
	intro_skip::Request request;
	int asked = -1;
	for (int frame = 99; frame < 400 && asked < 0; frame++) {
		const bool scenes = frame >= 100;
		const PackedInput local = intro_skip::ScriptedPress(request.ScenesFrom(), frame, 90) ? input::kLP : 0;
		request.Next(scenes ? 7 : kFight, local, scenes, frame);
		if (request.Requested()) asked = frame;
	}
	CHECK(asked == 190);
	// A press scheduled inside the guard is ignored, and nothing is asked.
	intro_skip::Request early;
	for (int frame = 99; frame < 400; frame++) {
		const bool scenes = frame >= 100;
		const PackedInput local = intro_skip::ScriptedPress(early.ScenesFrom(), frame, 10) ? input::kLP : 0;
		early.Next(scenes ? 7 : kFight, local, scenes, frame);
	}
	CHECK_FALSE(early.Requested());
}

TEST_CASE("the skip ends Finish, the winner's demo and the draw's demo the way their own phases move on")
{
	namespace rf = sfve::mod::sfv::resultflow;
	using intro_skip::ScenesAction;
	using intro_skip::ScenesStep;
	using intro_skip::ScenesStepFor;
	// Finish: whichever phase it waits in, the flow asks for its next state.
	for (int phase = 0; phase <= 17; phase++) {
		CHECK(ScenesStepFor(rf::kFinish, rf::kFinishVtable, phase, false) == ScenesStep{ScenesAction::RequestNext, -1});
	}
	// The winner's demo: its 90-tick wait before the setup, then its skip phase.
	CHECK(ScenesStepFor(rf::kMatchResultDemo, rf::kMatchResultDemoVtable, 1, false) == ScenesStep{ScenesAction::SetPhase, 2});
	CHECK(ScenesStepFor(rf::kMatchResultDemo, rf::kMatchResultDemoVtable, 3, false) == ScenesStep{ScenesAction::ResultSkip, 4});
	for (const int phase : {0, 2, 4, 5, 6, 7}) {
		INFO("phase " << phase);
		CHECK(ScenesStepFor(rf::kMatchResultDemo, rf::kMatchResultDemoVtable, phase, false) == ScenesStep{});
	}
	// The draw's demo: the wait for its demo, then its 300-tick wait.
	CHECK(ScenesStepFor(rf::kMatchDrawDemo, rf::kMatchDrawDemoVtable, 2, false) == ScenesStep{ScenesAction::SetPhase, 3});
	CHECK(ScenesStepFor(rf::kMatchDrawDemo, rf::kMatchDrawDemoVtable, 4, false) == ScenesStep{ScenesAction::SetPhase, 5});
	for (const int phase : {1, 3, 5, 6, 7}) {
		INFO("phase " << phase);
		CHECK(ScenesStepFor(rf::kMatchDrawDemo, rf::kMatchDrawDemoVtable, phase, false) == ScenesStep{});
	}
	// The states that leave on their first tick, and the results' own, play on.
	for (const int state : {rf::kRoundOver, rf::kMatchOver, rf::kMatchEnd, 9, 10, kFight, kIntro, kMatchStart}) {
		INFO("state " << state);
		for (int phase = 0; phase <= 7; phase++) CHECK(ScenesStepFor(state, rf::kFinishVtable, phase, false) == ScenesStep{});
	}
	// A state of another class (another flow's) is left alone.
	CHECK(ScenesStepFor(rf::kFinish, rf::kMatchResultDemoVtable, 3, false) == ScenesStep{});
	CHECK(ScenesStepFor(rf::kMatchResultDemo, rf::kFinishVtable, 3, false) == ScenesStep{});
	CHECK(ScenesStepFor(rf::kMatchDrawDemo, 0, 4, false) == ScenesStep{});
}

TEST_CASE("the results screens' question for any button hears the skip request, once the match is decided")
{
	constexpr uint32_t kAnyButton = 0xFFFFBFF0u; // the results pages' mask (rva 0x49FB00)
	CHECK((intro_skip::kAttackBits & kAnyButton) == intro_skip::kAttackBits);
	CHECK((kSkipBit & kAnyButton) == 0u);
	// The skip request on a decided match's skip frame is a press of any button.
	CHECK(intro_skip::PadsHear(kSkipBit, kAnyButton, true));
	CHECK(intro_skip::PadsHear(0, kAnyButton, true) == false);   // the other frames: nothing pressed
	CHECK(intro_skip::PadsHear(kSkipBit, kAnyButton, false) == false); // the intro's request presses nothing else
	// The game's own skip question hears the bit itself, decided or not.
	CHECK(intro_skip::PadsHear(kSkipBit, kSkipBit, true));
	CHECK(intro_skip::PadsHear(kSkipBit, kSkipBit, false));
	// A question for one button or a direction is not one for any button.
	for (const uint32_t mask : {0x1u, 0x2u, 0x40u, 0x10u | 0x20u}) {
		INFO("mask " << mask);
		CHECK_FALSE(intro_skip::PadsHear(kSkipBit, mask, true));
	}
	// Whatever the inputs hold of the mask is heard as before.
	CHECK(intro_skip::PadsHear(input::kLK, 0x40u, false));
	CHECK(intro_skip::PadsHear(input::kHP, kAnyButton, false));
}

TEST_CASE("the scenes' ids are the flow's, apart from the fight's and the intro's")
{
	namespace rf = sfve::mod::sfv::resultflow;
	CHECK(rf::kFinish == kFight + 1);
	CHECK(rf::kRoundOver == rf::kFinish + 1);
	CHECK(rf::kMatchEnd == rf::kMatchOver + 1);
	// Every one of them is a state whose skip frames read the bit once decided.
	for (const int state : {rf::kFinish, rf::kRoundOver, rf::kMatchResultDemo, rf::kMatchDrawDemo, rf::kMatchOver, rf::kMatchEnd}) {
		CHECK(intro_skip::Skippable(state, true));
		CHECK(intro_skip::Scenes(state, true));
	}
}

TEST_CASE("the scenes between two rounds: Finish, RoundOver and the round demos, not the next round's call")
{
	namespace rf = sfve::mod::sfv::resultflow;
	for (const int state : {rf::kFinish, rf::kRoundOver, rf::kRoundResultDemo, rf::kRoundDoubleKODemo}) {
		INFO("state " << state);
		CHECK(intro_skip::RoundScenes(state));
		CHECK(intro_skip::Skippable(state, false));
		CHECK(intro_skip::Scenes(state, false));
		CHECK(intro_skip::ConfirmedBits(state, 30, false) == kSkipBit);
		CHECK(intro_skip::ConfirmedBits(state, 31, false) == 0u);
	}
	// RoundStart and Ready are the next round's call ("ROUND 2", "FIGHT"),
	// RoundJump goes straight to it; none is skipped, nor the fight.
	for (const int state : {rf::kRoundStart, 5, 17, kFight, kIntro, kMatchStart, rf::kMatchResultDemo, rf::kMatchOver}) {
		INFO("state " << state);
		CHECK_FALSE(intro_skip::RoundScenes(state));
		CHECK_FALSE(intro_skip::Scenes(state, false));
		CHECK(intro_skip::ConfirmedBits(state, 30, false) == (state == kIntro ? kSkipBit : 0u));
	}
}

TEST_CASE("between two rounds a fresh press after the guard asks, and the next round's call clears it")
{
	namespace rf = sfve::mod::sfv::resultflow;
	constexpr int kRoundOverAt = 2000;
	constexpr int kGuardEnds = kRoundOverAt + intro_skip::kScenesGuardFrames;
	intro_skip::Request request;
	int frame = kRoundOverAt - 1;
	// The button that landed the KO is still down; the match is not decided.
	CHECK(request.Next(kFight, input::kMK, false, frame++) == input::kMK);
	for (; frame < kGuardEnds; frame++) {
		INFO("frame " << frame);
		const PackedInput mash = (frame % 5 == 0) ? input::kLP : 0;
		CHECK(request.Next(rf::kFinish, mash, false, frame) == mash);
		CHECK_FALSE(request.Requested());
	}
	CHECK(request.ScenesFrom() == kRoundOverAt);
	CHECK(request.Next(rf::kFinish, input::kHP, false, frame++) == (kSkipBit | input::kHP));
	CHECK(request.Next(rf::kRoundOver, 0, false, frame++) == kSkipBit);
	CHECK(request.Next(rf::kRoundResultDemo, 0, false, frame++) == kSkipBit);
	// The next round's call: nothing asked, nothing kept for the next round.
	CHECK(request.Next(rf::kRoundStart, input::kHP, false, frame++) == input::kHP);
	CHECK_FALSE(request.Requested());
	CHECK(request.ScenesFrom() == -1);
	CHECK(request.Next(5, 0, false, frame++) == 0u);
	CHECK(request.Next(kFight, 0, false, frame++) == 0u);
	CHECK_FALSE(request.Requested());
}

TEST_CASE("a press after the guard is the only ask for the scenes between two rounds, and the next round's call clears it")
{
	namespace rf = sfve::mod::sfv::resultflow;
	intro_skip::Request request;
	int frame = 500;
	CHECK(request.Next(kFight, 0, false, frame++) == 0u);
	const int from = frame;
	for (; frame < from + 4 * intro_skip::kScenesGuardFrames; frame++) {
		CHECK(request.Next(rf::kFinish, 0, false, frame) == 0u);
		CHECK_FALSE(request.Requested());
	}
	CHECK(request.Next(rf::kFinish, input::kMK, false, frame++) == (kSkipBit | input::kMK));
	CHECK(request.Next(rf::kRoundDoubleKODemo, 0, false, frame++) == kSkipBit);
	// Never into the next round's call or its fight.
	CHECK(request.Next(rf::kRoundStart, 0, false, frame++) == 0u);
	CHECK(request.Next(5, 0, false, frame++) == 0u);
	CHECK(request.Next(kFight, 0, false, frame++) == 0u);
}

TEST_CASE("Finish is not cut while the KO's slow motion bit is set, and is once the game has cleared it")
{
	namespace rf = sfve::mod::sfv::resultflow;
	using intro_skip::ScenesAction;
	using intro_skip::ScenesStep;
	using intro_skip::ScenesStepFor;
	CHECK(intro_skip::kKoStopBit == 0x20u);
	for (int phase = 0; phase <= 17; phase++) {
		INFO("phase " << phase);
		CHECK(ScenesStepFor(rf::kFinish, rf::kFinishVtable, phase, true) == ScenesStep{});
		CHECK(ScenesStepFor(rf::kFinish, rf::kFinishVtable, phase, false) == ScenesStep{ScenesAction::RequestNext, -1});
		// The default is the step as before the gate: a caller that says nothing cuts.
		CHECK(ScenesStepFor(rf::kFinish, rf::kFinishVtable, phase, false) == ScenesStep{ScenesAction::RequestNext, -1});
	}
	// Only Finish waits on it: the winner's demo and the draw's demo move as before.
	CHECK(ScenesStepFor(rf::kMatchResultDemo, rf::kMatchResultDemoVtable, rf::kResultSkipPhase, true) ==
		ScenesStep{ScenesAction::ResultSkip, rf::kResultSkippedPhase});
	CHECK(ScenesStepFor(rf::kMatchDrawDemo, rf::kMatchDrawDemoVtable, rf::kDrawDemoPhase, true) ==
		ScenesStep{ScenesAction::SetPhase, rf::kDrawAfterDemoPhase});
}

TEST_CASE("the skip ends Finish between two rounds as at the match's end, and leaves the round demos to the game")
{
	namespace rf = sfve::mod::sfv::resultflow;
	using intro_skip::ScenesAction;
	using intro_skip::ScenesStep;
	using intro_skip::ScenesStepFor;
	// Finish is the same state whether or not the round decides the match.
	CHECK(ScenesStepFor(rf::kFinish, rf::kFinishVtable, 12, false) == ScenesStep{ScenesAction::RequestNext, -1});
	// The round demos end on the confirmed skip bit by themselves.
	for (int phase = 0; phase <= 5; phase++) {
		INFO("phase " << phase);
		CHECK(ScenesStepFor(rf::kRoundResultDemo, rf::kRoundResultDemoVtable, phase, false) == ScenesStep{});
		CHECK(ScenesStepFor(rf::kRoundDoubleKODemo, rf::kRoundDoubleKODemoVtable, phase, false) == ScenesStep{});
	}
	// The any-button answer stays a decided match's (the results screens').
	CHECK_FALSE(intro_skip::PadsHear(kSkipBit, 0xFFFFBFF0u, false));
}
