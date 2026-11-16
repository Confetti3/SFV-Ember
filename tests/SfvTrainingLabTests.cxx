// SFV's Training battle in the Training lab's terms (ember/SfvLabInput): the
// input word a recording keeps, and the dummy's stun and free frame as the
// game counts them (CharaUnit +0x91E), fed through the frame meter's samples
// (ember/SfvMeterSample) into the library's stun watch and reply decision
// (training/LabCore.hxx), as SfvTrainingLab feeds them after each tick.

#include <doctest/doctest.h>

#include "ember/SfvLabInput.hxx"
#include "ember/SfvMeterSample.hxx"
#include "mod/GameInput.hxx"

#include "training/LabCore.hxx"

#include <array>
#include <vector>

using sfve::mod::FighterFrame;
using sfve::mod::sfv::fighter::StateKind;
namespace lab = sfve::adapter::lab;
namespace training = sf4e::training;
namespace input = sfve::mod::input;

namespace {

FighterFrame Frame(StateKind kind, int until, int script = 0, float frame = 0, float vital = 100)
{
	FighterFrame f;
	f.valid = true;
	f.stateKind = kind;
	f.untilActionable = until;
	f.script = script;
	f.scriptFrame = frame;
	f.posture = 0;
	f.vital = vital;
	f.vitalMax = 100;
	return f;
}

// A reply plan over one slot that crouches, then presses LP the next frame.
struct Bench {
	training::Session session;
	training::ReplyLab replies{11};
	sfve::adapter::MeterMove move;
	explicit Bench(int when)
	{
		session.Enter();
		session.SetReady(true);
		replies.plan.when = when;
		replies.Seed(session);
		training::Command load;
		load.action = training::Action::Load;
		load.generation = session.GetView().generation;
		load.side = 1;
		load.frames = {training::Input{2, 2}, training::Input{2 | 0x10, 2 | 0x10}};
		REQUIRE(session.Apply(load));
	}
	// One tick of the dummy as the game left it: whether the reply begins.
	bool Tick(const FighterFrame& dummy)
	{
		const training::FighterSample sample = sfve::adapter::MeterSample(dummy, move);
		const training::DummySeen seen = replies.Watch(sample, 600, lab::ScriptedFrames(dummy));
		session.Observe(false, false, -1, training::ReplyLab::Due(session, seen));
		session.Commit(training::Frame{});
		return replies.Decide(seen, session).started;
	}
};

} // namespace

TEST_CASE("A recording keeps SFV's battle input in the lab's word, HP at 0x400, and gives it back unchanged")
{
	CHECK(lab::ToLab(input::kHP) == 0x400);
	CHECK(lab::ToSfv(0x400) == input::kHP);
	for (const auto attack : input::kAttacks) CHECK((lab::ToLab(attack) & training::AttackButtons) != 0);
	CHECK(lab::ToLab(input::kLeft | input::kDown) == (training::LeftBit | 2u));
	for (uint32_t bits = 0; bits <= input::kInputBits; bits++) CHECK(lab::ToSfv(lab::ToLab(bits)) == bits);
	// Mirroring a recorded frame turns the screen's Left and Right only.
	const training::Input frame{lab::ToLab(input::kRight | input::kHP), lab::ToLab(input::kRight | input::kHP)};
	CHECK(lab::ToSfv(training::Mirror(frame).raw) == (input::kLeft | input::kHP));
}

TEST_CASE("The dummy's free frame is the game's own count while it is hit, blocking or getting up")
{
	CHECK(lab::ScriptedFrames(Frame(StateKind::GuardHit, 7)) == 7);
	CHECK(lab::ScriptedFrames(Frame(StateKind::Hit, 12)) == 12);
	CHECK(lab::ScriptedFrames(Frame(StateKind::Rise, 20)) == 20);
	CHECK(lab::ScriptedFrames(Frame(StateKind::BackRise, 35)) == 35); // the back rise times its way up too
	// Lying down the game cannot time it.
	CHECK(lab::ScriptedFrames(Frame(StateKind::LyingDown, sfve::mod::sfv::fighter::kUntimed)) == -1);
	// Free, or in a move of its own, the watch is not given a count.
	CHECK(lab::ScriptedFrames(Frame(StateKind::Stand, 0)) == -1);
	CHECK(lab::ScriptedFrames(Frame(StateKind::Attack, 9)) == -1);
}

TEST_CASE("A reply after block meets the dummy's first free frame")
{
	Bench bench(2);
	// Blockstun: the game counts 6, 5, ... to the tick its state is its own.
	int began = -1;
	for (int tick = 0; tick < 8 && began < 0; tick++) {
		const int left = 6 - tick;
		if (bench.Tick(left > 0 ? Frame(StateKind::GuardHit, left, 40, (float)tick) : Frame(StateKind::Stand, 0))) began = tick;
	}
	// The reply's LP is its second frame (lead 1), acted on a tick after it is
	// sent (DummyInputLag): it begins with two ticks and the lag to go.
	CHECK(began == 6 - (1 + training::DummyInputLag + 1));
	CHECK(bench.session.Replying());
}

TEST_CASE("A reply after hit is not made for a block")
{
	Bench bench(1);
	bool began = false;
	for (int tick = 0; tick < 8; tick++) began = bench.Tick(tick < 5 ? Frame(StateKind::GuardHit, 5 - tick, 40, (float)tick) : Frame(StateKind::Stand, 0)) || began;
	CHECK_FALSE(began);
}

TEST_CASE("A wake-up reply waits through the knockdown and meets the free frame of getting up")
{
	Bench bench(3);
	int tick = 0, began = -1;
	// The hit that knocks the dummy down, the fall, lying down (untimed), then
	// getting up timed by the game.
	const auto at = [&](const FighterFrame& f) {
		if (bench.Tick(f) && began < 0) began = tick;
		tick++;
	};
	for (int i = 0; i < 3; i++) at(Frame(StateKind::LaunchHit, 30 - i, 70, (float)i, 80));
	for (int i = 0; i < 10; i++) at(Frame(StateKind::LyingDown, sfve::mod::sfv::fighter::kUntimed, 71, (float)i, 80));
	CHECK(began < 0);
	const int riseFrom = tick;
	for (int left = 12; left > 0; left--) at(Frame(StateKind::Rise, left, 72, (float)(12 - left), 80));
	at(Frame(StateKind::Stand, 0, 0, 0, 80));
	CHECK(began == riseFrom + 12 - (1 + training::DummyInputLag + 1));
}
