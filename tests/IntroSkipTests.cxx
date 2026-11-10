// The intro's skip (mod/IntroSkip.hxx): the bit both copies exchange, the
// frames the game reads it on, and each copy's request.

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
		for (const int other : {0, kMatchStart, 4, 5, kFight, 7, 21, 22, -1}) {
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
