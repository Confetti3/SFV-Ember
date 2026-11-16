// What a replay's playback keeps of itself as it seeks (docs/design/beta10/
// replay-seek.md): the strip's round and clock made again from the recording
// after a seek, checkpoints compared by their frame id and again when reached
// again, and a playback judged whole by having been to the last frame.

#include <doctest/doctest.h>

#include "core/ReplayFile.hxx"
#include "ember/PlaybackBook.hxx"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace sfve;
using sfve::adapter::PlaybackBook;
using Compared = PlaybackBook::Compared;

// 40 frames of intro, a round's fight of 120 frames, 30 of scenes, a second
// round's fight of 100 and 30 more of scenes. Checkpoints every 20 frames.
std::shared_ptr<replay::Replay> Recording()
{
	auto r = std::make_shared<replay::Replay>();
	const auto add = [&](int count, bool fight) {
		for (int i = 0; i < count; i++) {
			RecordedFrame frame;
			frame.flags = fight ? RecordedFrame::kFight : 0;
			r->frames.push_back(frame);
		}
	};
	add(40, false);
	add(120, true);
	add(30, false);
	add(100, true);
	add(30, false);
	for (int f = 0; f <= (int)r->frames.size(); f += 20) r->checkpoints.push_back({f, 0x7000u + (uint64_t)f});
	return r;
}

// The playback plays frames [from, to) through the book, as the mode does.
void Play(PlaybackBook& book, const replay::Replay& r, int from, int to)
{
	for (int f = from; f < to; f++) book.NoteFrame(f, r.frames[(size_t)f].Fight(), 0);
}

TEST_CASE("the clock after a seek is the one the recording made at that frame")
{
	const auto r = Recording();
	// Played straight: the round and the time into its fight after each frame.
	PlaybackBook straight;
	straight.Start(r);
	std::vector<std::pair<int, uint32_t>> clock{{0, 0}};
	for (int f = 0; f < (int)r->frames.size(); f++) {
		Play(straight, *r, f, f + 1);
		clock.push_back({straight.Round(), straight.Cursor()});
	}
	// The first round's fight, its frames numbered from 1; the second round's.
	CHECK(clock[40] == std::make_pair(0, 0u));
	CHECK(clock[41] == std::make_pair(0, 1u));
	CHECK(clock[160] == std::make_pair(0, 120u));
	CHECK(clock[190] == std::make_pair(0, 120u));
	CHECK(clock[191] == std::make_pair(1, 1u));
	CHECK(clock[290] == std::make_pair(1, 100u));
	// A playback put back at any frame has that clock, whatever it played before.
	for (const int frame : {0, 1, 40, 41, 100, 160, 175, 190, 191, 250, 290, 300, 319}) {
		PlaybackBook sought;
		sought.Start(r);
		Play(sought, *r, 0, 300);
		sought.NoteSeek(frame, 0);
		INFO("frame " << frame);
		CHECK(sought.Round() == clock[(size_t)frame].first);
		CHECK(sought.Cursor() == clock[(size_t)frame].second);
		// ...and plays on from there as the straight playback did.
		Play(sought, *r, frame, std::min(frame + 150, (int)r->frames.size()));
		PlaybackBook same;
		same.Start(r);
		Play(same, *r, 0, std::min(frame + 150, (int)r->frames.size()));
		CHECK(sought.Round() == same.Round());
		CHECK(sought.Cursor() == same.Cursor());
	}
}

TEST_CASE("a game in place starts its rounds over, and a seek is within its game")
{
	auto r = Recording();
	// A held WIN, then a second game: its first fight is round 0 again.
	for (int i = 0; i < 20; i++) {
		RecordedFrame held;
		held.flags = RecordedFrame::kHeld;
		r->frames.push_back(held);
	}
	const int second = (int)r->frames.size();
	for (int i = 0; i < 50; i++) {
		RecordedFrame frame;
		frame.flags = RecordedFrame::kFight;
		r->frames.push_back(frame);
	}
	PlaybackBook book;
	book.Start(r);
	Play(book, *r, 0, 300);
	CHECK(book.Round() == 1);
	for (int f = 300; f < second; f++) book.NoteFrame(f, false, 0);
	for (int f = second; f < second + 30; f++) book.NoteFrame(f, true, 1);
	CHECK(book.Round() == 0);
	CHECK(book.Cursor() == 30u);
	// Put back to the second game's frame 10 into its fight: its frames before are the game's.
	book.NoteSeek(second + 10, second);
	CHECK(book.Round() == 0);
	CHECK(book.Cursor() == 10u);
}

TEST_CASE("a checkpoint is compared by its frame, again when reached again, and counted once")
{
	const auto r = Recording();
	PlaybackBook book;
	book.Start(r);
	uint64_t recorded = 0;
	CHECK(book.NoteCheckpoint(7, 1, &recorded) == Compared::NotACheckpoint);
	CHECK(book.NoteCheckpoint(100000, 1, &recorded) == Compared::NotACheckpoint);
	CHECK(book.Compared() == 0);
	CHECK(book.NoteCheckpoint(60, 0x7000u + 60, &recorded) == Compared::Equal);
	CHECK(recorded == 0x7000u + 60);
	CHECK(book.NoteCheckpoint(80, 0x7000u + 80, &recorded) == Compared::Equal);
	CHECK(book.Compared() == 2);
	CHECK(book.Comparisons() == 2);
	// The seek goes back and reaches them again: compared again, never double-counted.
	CHECK(book.NoteCheckpoint(60, 0x7000u + 60, &recorded) == Compared::Equal);
	CHECK(book.NoteCheckpoint(80, 0x7000u + 80, &recorded) == Compared::Equal);
	CHECK(book.Compared() == 2);
	CHECK(book.Comparisons() == 4);
	CHECK(book.Differing() == 0);
	// A difference after a seek is the first of that checkpoint, logged once...
	CHECK(book.NoteCheckpoint(80, 0xBAD, &recorded) == Compared::FirstDifference);
	CHECK(recorded == 0x7000u + 80);
	CHECK(book.Differing() == 1);
	CHECK(book.Compared() == 2);
	// ...is not logged again when it differs again, and is not undone by a later equal one.
	CHECK(book.NoteCheckpoint(80, 0xBAD, &recorded) == Compared::Differs);
	CHECK(book.NoteCheckpoint(80, 0x7000u + 80, &recorded) == Compared::Differs);
	CHECK(book.Differing() == 1);
	// Another checkpoint differing is another difference; one that never differs stays equal.
	CHECK(book.NoteCheckpoint(100, 0xBAD, &recorded) == Compared::FirstDifference);
	CHECK(book.Differing() == 2);
	CHECK(book.NoteCheckpoint(60, 0x7000u + 60, &recorded) == Compared::Equal);
	CHECK(book.Compared() == 3);
	// The start state is compared once, with the same counts.
	book.NoteStart(true);
	CHECK(book.Compared() == 4);
	CHECK(book.Differing() == 2);
	book.NoteStart(false);
	CHECK(book.Differing() == 3);
}

TEST_CASE("a playback is whole when it has been to the last frame, however many frames it played")
{
	const auto r = Recording();
	const int total = (int)r->frames.size();
	PlaybackBook book;
	book.Start(r);
	CHECK(book.Active());
	CHECK_FALSE(book.Whole());
	Play(book, *r, 0, total - 1);
	CHECK(book.Furthest() == total - 1);
	CHECK_FALSE(book.Whole());
	// Back, and on again over the same frames, to the last: frames played pass the recording's length.
	book.NoteSeek(100, 0);
	Play(book, *r, 100, 200);
	CHECK(book.Furthest() == total - 1);
	CHECK_FALSE(book.Whole());
	book.NoteSeek(0, 0);
	Play(book, *r, 0, total);
	CHECK(book.Whole());
	CHECK(book.Furthest() == total);
	CHECK(book.Played() > total);
	// A playback that jumped forward over its middle is whole as well as one that played it (frames are not counted).
	PlaybackBook skipped;
	skipped.Start(r);
	Play(skipped, *r, 0, 10);
	Play(skipped, *r, 300, total);
	CHECK(skipped.Whole());
	// A book with no playback notes nothing.
	PlaybackBook idle;
	idle.NoteFrame(5, true, 0);
	idle.NoteSeek(5, 0);
	CHECK(idle.Played() == 0);
	CHECK_FALSE(idle.Whole());
	CHECK(idle.NoteCheckpoint(20, 1, nullptr) == Compared::NotACheckpoint);
}

} // namespace
