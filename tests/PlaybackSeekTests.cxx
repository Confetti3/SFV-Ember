// A replay's seeks (docs/design/beta10/replay-seek.md), in the core: where a
// playback may land (the game's fight frames), the ring of states it takes
// (their spacing, thinning and lookup), and a playback on MockFighter that
// goes back, restarts and plays on, reaching the states the recording's first
// pass did, hash by hash.

#include <doctest/doctest.h>

#include "TestInputs.hxx"
#include "core/PlaybackSnapshots.hxx"
#include "core/PlaybackSpan.hxx"
#include "core/ReplaySeeker.hxx"
#include "core/RollbackSession.hxx"

#include <string.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sfve;
using namespace sfve::testbed;

// Frames: 0-99 ignored, 100-399 fight, 400-449 ignored, 450-599 fight, 600-649
// held (the WIN's answers), 650-699 ignored, 700-899 fight, 900-929 held.
ReplayFrames SpanFrames()
{
	ReplayFrames frames(930);
	for (int f = 0; f < 930; f++) {
		const bool held = (f >= 600 && f < 650) || f >= 900;
		const bool fight = (f >= 100 && f < 400) || (f >= 450 && f < 600) || (f >= 700 && f < 900);
		frames[(size_t)f].flags = held ? RecordedFrame::kHeld : fight ? RecordedFrame::kFight : 0;
	}
	return frames;
}

TEST_CASE("a game's span is the frames between the held ones, with its first and last fight frame")
{
	const ReplayFrames frames = SpanFrames();
	for (const int inside : {0, 10, 100, 399, 400, 449, 450, 599}) {
		const reach::Span span = reach::SpanAt(frames, inside);
		INFO("frame " << inside);
		CHECK(span.from == 0);
		CHECK(span.to == 600);
		CHECK(span.first == 100);
		CHECK(span.last == 599);
		CHECK(span.Has());
		CHECK(span.Contains(inside));
	}
	// The game after the WIN's answers has its own span, and the answers none.
	for (const int inside : {650, 699, 700, 899}) {
		const reach::Span span = reach::SpanAt(frames, inside);
		INFO("frame " << inside);
		CHECK(span.from == 650);
		CHECK(span.to == 900);
		CHECK(span.first == 700);
		CHECK(span.last == 899);
		CHECK_FALSE(span.Contains(599));
		CHECK_FALSE(span.Contains(900));
	}
	for (const int held : {600, 625, 649, 900, 929, 930, 5000, -1}) {
		INFO("frame " << held);
		CHECK_FALSE(reach::SpanAt(frames, held).Has());
	}
	// A game with no fight frame has no span to seek in.
	ReplayFrames quiet(40);
	CHECK_FALSE(reach::SpanAt(quiet, 5).Has());
	CHECK(reach::SpanAt(quiet, 5).to == 40);
}

TEST_CASE("a landing is a fight frame: the nearest one before a target, or after it, within the bounds")
{
	const ReplayFrames frames = SpanFrames();
	// Before: the ignored stretch rounds back to the last fight frame before it.
	CHECK(reach::LandingBefore(frames, 300, 100) == 300);
	CHECK(reach::LandingBefore(frames, 420, 100) == 399);
	CHECK(reach::LandingBefore(frames, 449, 100) == 399);
	CHECK(reach::LandingBefore(frames, 450, 100) == 450);
	// ...not past the floor, and none where there is none.
	CHECK(reach::LandingBefore(frames, 50, 100) == -1);
	CHECK(reach::LandingBefore(frames, 420, 400) == -1);
	CHECK(reach::LandingBefore(frames, 420, 399) == 399);
	// After: to the next round's first fight frame, and not past the game's last.
	CHECK(reach::LandingAfter(frames, 300, 599) == 300);
	CHECK(reach::LandingAfter(frames, 400, 599) == 450);
	CHECK(reach::LandingAfter(frames, 449, 599) == 450);
	CHECK(reach::LandingAfter(frames, 420, 449) == -1);
	CHECK(reach::LandingAfter(frames, 0, 599) == 100);
	CHECK(reach::LandingAfter(frames, 599, 599) == 599);
	CHECK(reach::LandingAfter(frames, 5000, 599) == -1);
}

// A layout over a few bytes, which the tests write the frame into.
struct TinyState {
	uint8_t bytes[16] = {};
	StateLayout layout;
	TinyState() { layout.Add("tiny", bytes, sizeof(bytes)); }
	void Set(int frame) { memcpy(bytes, &frame, sizeof(frame)); }
	static int FrameOf(const PlaybackSnapshots::Snapshot& snapshot)
	{
		int frame = -1;
		memcpy(&frame, snapshot.bytes.data(), sizeof(frame));
		return frame;
	}
};

TEST_CASE("the states are taken at the first frame and then each spacing, thinned and doubled when full")
{
	static_assert(PlaybackSnapshots::kMost == 48 && PlaybackSnapshots::kFirstSpacing == 120);
	TinyState state;
	PlaybackSnapshots ring(8, 10);
	ring.Clear(5);
	CHECK(ring.Game() == 5);
	CHECK(ring.Count() == 0);
	CHECK(ring.First() == nullptr);
	CHECK(ring.NearestBefore(100) == nullptr);
	const auto take = [&](int frame) {
		state.Set(frame);
		REQUIRE(ring.Due(frame));
		ring.Take(state.layout, frame);
	};
	// The first is due at once, then one each spacing.
	CHECK(ring.Due(30));
	take(30);
	for (int frame = 31; frame < 40; frame++) CHECK_FALSE(ring.Due(frame));
	CHECK(ring.Due(40));
	for (int frame = 40; frame <= 100; frame += 10) take(frame);
	CHECK(ring.Frames() == std::vector<int>{30, 40, 50, 60, 70, 80, 90, 100});
	CHECK(ring.Spacing() == 10);
	// The ninth finds them all taken: every other one goes, the first stays, the spacing doubles.
	take(110);
	CHECK(ring.Frames() == std::vector<int>{30, 50, 70, 90, 110});
	CHECK(ring.Spacing() == 20);
	CHECK(ring.First()->frame == 30);
	CHECK_FALSE(ring.Due(119));
	CHECK(ring.Due(130));
	// Each holds the state of its frame, wherever thinning moved it.
	for (const int frame : ring.Frames()) {
		const PlaybackSnapshots::Snapshot* held = ring.NearestBefore(frame + 1);
		REQUIRE(held != nullptr);
		CHECK(held->frame == frame);
		CHECK(TinyState::FrameOf(*held) == frame);
	}
	// The nearest state strictly before a frame: the playback plays on from it.
	CHECK(ring.NearestBefore(30) == nullptr);
	CHECK(ring.NearestBefore(31)->frame == 30);
	CHECK(ring.NearestBefore(50)->frame == 30);
	CHECK(ring.NearestBefore(51)->frame == 50);
	CHECK(ring.NearestBefore(100000)->frame == 110);
	CHECK_FALSE(ring.Due(90));
	CHECK_FALSE(ring.Due(105));
	// A new game starts the spacing and the states over.
	ring.Clear(9);
	CHECK(ring.Game() == 9);
	CHECK(ring.Count() == 0);
	CHECK(ring.Spacing() == 10);
	CHECK(ring.Due(0));
}

TEST_CASE("a state taken between two held ones keeps them in order")
{
	TinyState state;
	PlaybackSnapshots ring(16, 10);
	ring.Clear(0);
	for (const int frame : {0, 100, 50}) {
		state.Set(frame);
		REQUIRE(ring.Due(frame));
		ring.Take(state.layout, frame);
	}
	CHECK(ring.Frames() == std::vector<int>{0, 50, 100});
	CHECK(TinyState::FrameOf(*ring.NearestBefore(51)) == 50);
	CHECK(TinyState::FrameOf(*ring.NearestBefore(101)) == 100);
	CHECK(TinyState::FrameOf(*ring.NearestBefore(50)) == 0);
}

TEST_CASE("a long playback never holds more than 48 states, keeps its first and its spacing a doubling of 120")
{
	TinyState state;
	PlaybackSnapshots ring;
	ring.Clear(0);
	size_t most = 0;
	for (int frame = 0; frame < 60000; frame++) {
		if (!ring.Due(frame)) continue;
		state.Set(frame);
		ring.Take(state.layout, frame);
		most = std::max(most, ring.Count());
	}
	CHECK(most == PlaybackSnapshots::kMost);
	CHECK(ring.Count() <= PlaybackSnapshots::kMost);
	CHECK(ring.First()->frame == 0);
	int spacing = PlaybackSnapshots::kFirstSpacing;
	while (spacing < ring.Spacing()) spacing *= 2;
	CHECK(spacing == ring.Spacing());
	const std::vector<int> frames = ring.Frames();
	REQUIRE(frames.size() > 1);
	CHECK(std::is_sorted(frames.begin(), frames.end()));
	// No two are closer than the spacing they were taken at, the first spacing at least.
	for (size_t i = 1; i < frames.size(); i++) CHECK(frames[i] - frames[i - 1] >= PlaybackSnapshots::kFirstSpacing);
}

// A recording made by playing a MockFighter on scripted inputs: the frames
// that read inputs and those that do not.
ReplayFrames Record(int frames, int ignoreFrom, int ignoreTo)
{
	MockFighter game;
	game.IgnoreInputsBetween(ignoreFrom, ignoreTo);
	ReplayFrames out;
	for (int f = 0; f < frames; f++) {
		RecordedFrame frame;
		frame.flags = game.InputsMatter() ? RecordedFrame::kFight : 0;
		frame.inputs = GivenInputs(game, ScriptedFrame(f, 3), f);
		game.Advance(frame.inputs, false);
		out.push_back(frame);
	}
	return out;
}

constexpr int kIgnoreFrom = 100, kIgnoreTo = 180;

// A playback, driven as the netplay mode drives one: each frame offered to
// the seeker before its update, a seek restoring the nearest state before its
// frame and playing on to it.
struct SeekPlayback {
	MockFighter game;
	RollbackSession session{game};
	ReplaySeeker seeker;
	const ReplayFrames& frames;
	std::vector<std::pair<int, uint64_t>> checkpoints; // each report, a checkpoint reached again included

	explicit SeekPlayback(const ReplayFrames& recording) : frames(recording)
	{
		game.IgnoreInputsBetween(kIgnoreFrom, kIgnoreTo);
		RollbackSession::Config c;
		c.replay = &recording;
		c.keepAllHashes = true;
		c.hashEveryFrames = 1;
		c.onCheckpoint = [this](int frame, uint64_t hash) { checkpoints.push_back({frame, hash}); };
		std::string error;
		REQUIRE_MESSAGE(session.Start(c, &error), error);
	}
	bool PlayTo(int frame)
	{
		while (session.Frame() < frame) {
			seeker.Before(session, frames);
			if (session.Step(0) != RollbackSession::StepResult::Advanced) return false;
		}
		return true;
	}
	// The playback's seek: false when no state could be put back.
	bool SeekTo(int frame)
	{
		if (frame < session.Frame() && seeker.Restore(session, frame) < 0) return false;
		return PlayTo(frame);
	}
	uint64_t HashAt(int frame) const
	{
		uint64_t hash = 0;
		REQUIRE(session.HashAt(frame, &hash));
		return hash;
	}
};

TEST_CASE("a playback that goes back, restarts and plays on reaches the states of its first pass")
{
	constexpr int kFrames = 1500;
	const ReplayFrames recording = Record(kFrames, kIgnoreFrom, kIgnoreTo);
	// The first pass: every frame's hash.
	std::vector<uint64_t> first;
	{
		SeekPlayback straight(recording);
		REQUIRE(straight.PlayTo(kFrames));
		for (int f = 0; f <= kFrames; f++) first.push_back(straight.HashAt(f));
	}
	SeekPlayback play(recording);
	const reach::Span span = reach::SpanAt(recording, 0);
	REQUIRE(span.Has());
	CHECK(span.first == 0);
	REQUIRE(play.PlayTo(900));
	SeekGround ground = play.seeker.Ground(play.session, recording, false);
	CHECK(ground.frame == 900);
	CHECK(ground.canRestore);
	CHECK(ground.ringFirst == 0);
	CHECK(ground.snapshots >= 5);
	CHECK(ground.span.Has());
	// A state is taken only at the start of a fight frame: none in the ignored stretch.
	for (const int frame : {kIgnoreFrom, 140, kIgnoreTo - 1}) CHECK_FALSE(recording[(size_t)frame].Fight());

	SUBCASE("back to a frame, then on to the end")
	{
		const size_t reports = play.checkpoints.size();
		REQUIRE(play.SeekTo(400));
		CHECK(play.session.Frame() == 400);
		CHECK(play.game.Frame() == 400);
		// The frames played again hash as they did, and the checkpoints they pass are reported again, equal.
		for (int f = 300; f <= 400; f++) CHECK(play.HashAt(f) == first[(size_t)f]);
		for (size_t i = reports; i < play.checkpoints.size(); i++) {
			CHECK(play.checkpoints[i].second == first[(size_t)play.checkpoints[i].first]);
		}
		CHECK(play.checkpoints.size() > reports);
		REQUIRE(play.PlayTo(kFrames));
		for (int f = 400; f <= kFrames; f++) CHECK(play.HashAt(f) == first[(size_t)f]);
		for (const auto& [frame, hash] : play.checkpoints) CHECK(hash == first[(size_t)frame]);
		CHECK(play.seeker.Restored() == 1);
	}
	SUBCASE("a step back, several in a row, and a restart to the first fight frame")
	{
		// A step back is a seek to the frame before: the state before it, played on.
		for (int target = 899; target > 880; target--) {
			REQUIRE(play.SeekTo(target));
			CHECK(play.session.Frame() == target);
			CHECK(play.game.Frame() == target);
			CHECK(play.HashAt(target) == first[(size_t)target]);
		}
		// Restart: the first fight frame's update played again, from the first state.
		REQUIRE(play.SeekTo(1));
		CHECK(play.session.Frame() == 1);
		CHECK(play.HashAt(1) == first[1]);
		REQUIRE(play.PlayTo(kFrames));
		for (int f = 1; f <= kFrames; f++) CHECK(play.HashAt(f) == first[(size_t)f]);
	}
	SUBCASE("a loop between two frames, played over and over")
	{
		const int a = 600, b = 680;
		for (int round = 0; round < 3; round++) {
			REQUIRE(play.SeekTo(a));
			REQUIRE(play.PlayTo(b));
			for (int f = a; f <= b; f++) CHECK(play.HashAt(f) == first[(size_t)f]);
		}
		REQUIRE(play.PlayTo(kFrames));
		for (int f = b; f <= kFrames; f++) CHECK(play.HashAt(f) == first[(size_t)f]);
		for (const auto& [frame, hash] : play.checkpoints) CHECK(hash == first[(size_t)frame]);
	}
	SUBCASE("a seek to a frame no state is before has nothing to put back")
	{
		CHECK(play.seeker.Restore(play.session, 0) < 0);
		CHECK(play.session.Frame() == 900);
		CHECK_FALSE(play.session.Failed());
	}
}

TEST_CASE("putting a state back is refused for what it cannot take, and a diagnostic playback does not seek")
{
	const ReplayFrames recording = Record(300, kIgnoreFrom, kIgnoreTo);
	SeekPlayback play(recording);
	REQUIRE(play.PlayTo(250));
	// A null state, or a frame before the start, is refused and changes nothing.
	CHECK_FALSE(play.session.RestoreReplay(nullptr, 10));
	std::vector<uint8_t> bytes(play.session.Layout().TotalSize());
	play.session.Layout().SaveTo(bytes.data());
	CHECK_FALSE(play.session.RestoreReplay(bytes.data(), -1));
	CHECK(play.session.Frame() == 250);
	CHECK(play.session.RestoreReplay(bytes.data(), 250));
	CHECK(play.session.Frame() == 250);
	// A diagnostic playback never offers to seek.
	CHECK_FALSE(play.seeker.Ground(play.session, recording, true).canRestore);
	CHECK(play.seeker.Ground(play.session, recording, false).canRestore);
}

TEST_CASE("a game the layout no longer describes is neither written by a seek nor read for a snapshot")
{
	const ReplayFrames recording = Record(300, kIgnoreFrom, kIgnoreTo);
	SUBCASE("a seek back: refused before a byte is written, and the session fails")
	{
		SeekPlayback play(recording);
		REQUIRE(play.PlayTo(250));
		std::vector<uint8_t> bytes(play.session.Layout().TotalSize());
		play.session.Layout().SaveTo(bytes.data());
		REQUIRE(play.PlayTo(260));
		play.game.FailState();
		CHECK_FALSE(play.session.RestoreReplay(bytes.data(), 250));
		CHECK(play.session.Failed());
		CHECK(play.session.FailureReason() == MockFighter::kStateFailure);
		CHECK(play.game.Frame() == 260); // the state at 250 was not written back
	}
	SUBCASE("a snapshot: not taken while the game reports the failure")
	{
		SeekPlayback play(recording);
		REQUIRE(recording[0].Fight());
		play.game.FailState();
		play.seeker.Before(play.session, recording);
		CHECK(play.seeker.Taken() == 0);
		SeekPlayback healthy(recording);
		healthy.seeker.Before(healthy.session, recording);
		CHECK(healthy.seeker.Taken() == 1);
	}
}

TEST_CASE("a long playback keeps a bounded ring and still seeks to the start")
{
	constexpr int kFrames = 9000;
	const ReplayFrames recording = Record(kFrames, kIgnoreFrom, kIgnoreTo);
	std::vector<uint64_t> first;
	{
		SeekPlayback straight(recording);
		REQUIRE(straight.PlayTo(kFrames));
		for (int f = 0; f <= kFrames; f++) first.push_back(straight.HashAt(f));
	}
	SeekPlayback play(recording);
	REQUIRE(play.PlayTo(kFrames));
	const SeekGround ground = play.seeker.Ground(play.session, recording, false);
	CHECK(ground.snapshots <= PlaybackSnapshots::kMost);
	CHECK(ground.snapshots > 8);
	CHECK(ground.ringFirst == 0);
	for (const int target : {8000, 4321, 2800, 700, 1}) {
		REQUIRE(play.SeekTo(target));
		CHECK(play.session.Frame() == target);
		CHECK(play.HashAt(target) == first[(size_t)target]);
		REQUIRE(play.PlayTo(std::min(kFrames, target + 200)));
	}
	REQUIRE(play.PlayTo(kFrames));
	for (int f = 0; f <= kFrames; f++) CHECK(play.HashAt(f) == first[(size_t)f]);
}

} // namespace
