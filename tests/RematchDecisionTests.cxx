// The WIN's answers (ember/RematchDecision.hxx) and the held frames that
// carry them (RollbackSession::StepHeld): both copies decide alike on the
// same confirmed frame, and a game that goes on from there stays in step.

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "ember/RematchDecision.hxx"

#include <windows.h>

#include <algorithm>
#include <functional>
#include <optional>

using namespace sfve;
using namespace sfve::testbed;
namespace rip = sfve::adapter::rematch_in_place;
using rip::Answer;
using rip::Decide;

namespace {

constexpr std::uint32_t kChose = sf4e::rematch::ChoiceBit;
constexpr std::uint32_t kAsked = sf4e::rematch::AskingBit;
constexpr std::uint32_t kNo = rip::kDeclineBit;

} // namespace

TEST_CASE("both fighters' Rematch goes on in place at once, whenever it comes")
{
	CHECK(Decide(kChose, kChose, 0) == Answer::Rematch);
	CHECK(Decide(kChose, kChose, rip::kOpenFrames + 5) == Answer::Rematch);
	// A pad's own bits beside the answer change nothing.
	CHECK(Decide(kChose | 0x3F, kChose | 0x800, 200) == Answer::Rematch);
}

TEST_CASE("a decline, or a copy that offers no rematch, leaves at once")
{
	CHECK(Decide(kNo, 0, 0) == Answer::Leave);
	CHECK(Decide(kChose, kNo, 3) == Answer::Leave);
	CHECK(Decide(kAsked, kNo, 3) == Answer::Leave);
	// The decline outranks a Rematch on the same frame.
	CHECK(Decide(kChose | kNo, kChose, 3) == Answer::Leave);
	CHECK(rip::LocalBits(false, false, kChose) == kNo);
	CHECK(rip::LocalBits(true, true, kChose) == kNo);
	CHECK(rip::LocalBits(true, false, kChose | 0x7FF) == kChose);
	CHECK(rip::LocalBits(true, false, 0) == 0u);
}

TEST_CASE("no answers yet wait for the prompt to open, then time out")
{
	// The first held frames carry what was sent before the prompt opened.
	for (int held = 0; held < rip::kOpenFrames; held++) CHECK(Decide(0, 0, held) == Answer::Wait);
	CHECK(Decide(0, 0, rip::kOpenFrames) == Answer::Leave);
	// One fighter chose and the other's time ran out: back to the room.
	CHECK(Decide(kChose, 0, rip::kOpenFrames + 1) == Answer::Leave);
}

TEST_CASE("a fighter still asked holds the WIN until the longest wait")
{
	CHECK(Decide(kAsked, kAsked, 400) == Answer::Wait);
	CHECK(Decide(kChose, kAsked, 400) == Answer::Wait);
	CHECK(Decide(kAsked, 0, rip::kMostFrames - 1) == Answer::Wait);
	CHECK(Decide(kAsked, kChose, rip::kMostFrames) == Answer::Leave);
	CHECK(Decide(kChose, kChose, rip::kMostFrames + 10) == Answer::Rematch);
}

TEST_CASE("each side's Rematch, as the prompt reads the choices")
{
	CHECK(rip::Choices(0, 0) == 0u);
	CHECK(rip::Choices(kChose, kAsked) == 1u);
	CHECK(rip::Choices(kAsked, kChose | kNo) == 2u);
	CHECK(rip::Choices(kChose, kChose) == 3u);
	CHECK((rip::kBits & 0xFFFu) == 0u); // above every bit a pad makes (GameInput's kInputBits)
}

TEST_CASE("each side's decline, as the prompt reads the declines")
{
	CHECK(rip::Declines(0, 0) == 0u);
	CHECK(rip::Declines(kChose, kAsked) == 0u);
	CHECK(rip::Declines(kNo, kChose) == 1u);
	CHECK(rip::Declines(kAsked, kNo | kChose) == 2u);
	CHECK(rip::Declines(kNo, kNo) == 3u);
}

// Peers that answer at different times, A after `aFrom` held frames and B
// after `bFrom` (-1: B declines at once), each counting its own held frames.
struct HeldRun {
	std::optional<int> decidedAt[2];   // the session frame each peer decided on
	Answer answer[2] = {Answer::Wait, Answer::Wait};
	bool ok = false;
};

// Each peer's answer bits on its `held`th held frame.
using AnswerSchedule = std::function<std::uint32_t(int peer, int held)>;

HeldRun RunHeld(LoopbackPair& pair, const AnswerSchedule& bitsOf)
{
	HeldRun run;
	RollbackSession* sessions[2] = {&pair.a, &pair.b};
	MockFighter* games[2] = {&pair.gameA, &pair.gameB};
	const ULONGLONG start = GetTickCount64();
	while (!run.decidedAt[0] || !run.decidedAt[1]) {
		if (GetTickCount64() - start > 30000) return run;
		for (int i = 0; i < 2; i++) {
			RollbackSession& s = *sessions[i];
			if (run.decidedAt[i]) {
				s.Poll();
				continue;
			}
			const std::uint32_t bits = bitsOf(i, s.HeldFrames());
			Answer answer = Answer::Wait;
			const RollbackSession::StepResult r = s.StepHeld(bits, [&](const FrameInputs& in) {
				answer = Decide(in.p[0], in.p[1], s.HeldFrames());
				// The rematch's change to the game is the decided frame's.
				if (answer == Answer::Rematch) games[i]->Advance(FrameInputs{}, false);
			});
			if (r == RollbackSession::StepResult::Failed) return run;
			if (r == RollbackSession::StepResult::Advanced && answer != Answer::Wait) {
				run.decidedAt[i] = s.Frame();
				run.answer[i] = answer;
			}
		}
		Sleep(0);
	}
	run.ok = true;
	return run;
}

HeldRun RunHeld(LoopbackPair& pair, int aFrom, int bFrom)
{
	const int from[2] = {aFrom, bFrom};
	return RunHeld(pair, [&](int peer, int held) {
		return from[peer] < 0 ? kNo : held >= from[peer] ? kChose : kAsked;
	});
}

TEST_CASE("held frames: both peers decide on the same frame and stay in step after")
{
	for (const int delay : {1, 3}) {
		INFO("input delay " << delay);
		LoopbackPair pair(kBugNone, delay);
		REQUIRE(pair.RunTo(300));
		pair.RunTo(310); // past the fight's last guesses
		// Both at the same frame, as the room's battle is when it takes the end.
		REQUIRE(pair.a.Frame() == pair.b.Frame());
		const int heldFrom = pair.a.Frame();
		const int gameFrameA = pair.gameA.Frame();
		const HeldRun run = RunHeld(pair, 20, 45);
		REQUIRE(run.ok);
		CHECK(run.answer[0] == Answer::Rematch);
		CHECK(run.answer[1] == Answer::Rematch);
		CHECK(*run.decidedAt[0] == *run.decidedAt[1]);
		// B answered last: the decision is its 45th held frame plus the delay.
		CHECK(*run.decidedAt[0] - heldFrom > 45);
		// The game stood still but for the rematch's one change.
		CHECK(pair.gameA.Frame() == gameFrameA + 1);
		CHECK(pair.gameA.Frame() == pair.gameB.Frame());
		const int resumed = pair.a.Frame();
		REQUIRE(pair.RunTo(resumed + 300));
		pair.RunTo(resumed + 320);
		const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
		int compared = 0;
		for (int f = heldFrom; f <= confirmed; f++) {
			uint64_t ha = 0, hb = 0;
			if (!pair.a.HashAt(f, &ha) || !pair.b.HashAt(f, &hb)) continue;
			INFO("frame " << f);
			CHECK(ha == hb);
			compared++;
		}
		CHECK(compared > 5);
		CHECK(pair.a.GetStats().heldReplays == 0u);
		CHECK(pair.b.GetStats().heldReplays == 0u);
		CHECK(pair.a.GetStats().heldFrames > 45u);
		CHECK(pair.a.HeldFrames() == 0); // the next Step ended the hold
	}
}

TEST_CASE("held frames: one decline sends both peers back on the same frame")
{
	LoopbackPair pair(kBugNone, 2);
	REQUIRE(pair.RunTo(200));
	pair.RunTo(210);
	REQUIRE(pair.a.Frame() == pair.b.Frame());
	const int gameFrame = pair.gameB.Frame();
	const HeldRun run = RunHeld(pair, 10, -1);
	REQUIRE(run.ok);
	CHECK(run.answer[0] == Answer::Leave);
	CHECK(run.answer[1] == Answer::Leave);
	CHECK(*run.decidedAt[0] == *run.decidedAt[1]);
	CHECK(pair.gameB.Frame() == gameFrame); // nothing restarted
}

TEST_CASE("held frames: a runtime's exit mid-WIN is a decline both peers leave on")
{
	// A asked and then chose; B asked until the runtime ended its battle at
	// its 40th held frame (RoomBattle::DeclineToExit): from there it sends
	// the decline instead of leaving alone, and A, which chose, leaves with it
	// on the same frame rather than waiting for inputs that never come.
	LoopbackPair pair(kBugNone, 2);
	REQUIRE(pair.RunTo(200));
	pair.RunTo(210);
	REQUIRE(pair.a.Frame() == pair.b.Frame());
	const int heldFrom = pair.a.Frame();
	const HeldRun run = RunHeld(pair, [](int peer, int held) {
		if (peer == 0) return held >= 10 ? kChose : kAsked;
		return held >= 40 ? kNo : kAsked;
	});
	REQUIRE(run.ok);
	CHECK(run.answer[0] == Answer::Leave);
	CHECK(run.answer[1] == Answer::Leave);
	CHECK(*run.decidedAt[0] == *run.decidedAt[1]);
	CHECK(*run.decidedAt[0] - heldFrom > 40);
}

TEST_CASE("the reported WIN of 2026-11-09: P2 asked, P1 sent nothing, 37 held frames")
{
	// The reporter's replay: P1 sent 0 on every held frame, P2 asked from the second.
	// The decision never ended that WIN (it waits for 60 held frames before
	// "nobody asked" can end it, and P2 was asked), so its close came from
	// outside the decision: the runtime ending the battle, now a decline.
	CHECK(Decide(0, 0x8000, 0) == Answer::Wait);
	for (int held = 1; held < 37; held++) CHECK(Decide(0, kAsked, held) == Answer::Wait);
	CHECK(Decide(0, kAsked | kNo, 37) == Answer::Leave);
}
