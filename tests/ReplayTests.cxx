// Replays (EMBER_LIBRARY.md section 26): the file, the recorder and a
// playback, on MockFighter. Two fighters and a watcher over loopback record
// the same battle (the fight, frames that ignore inputs, a held WIN, a
// rematch in place and the game after it); the three recordings agree; and a
// fresh game playing one of them back reaches every state the fighters did,
// frame by frame. A playback that leaves the recording, or runs past its end,
// fails rather than play on.

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "TestInputs.hxx"
#include "core/ReplayFile.hxx"
#include "core/ReplayRecorder.hxx"
#include "core/RollbackSession.hxx"
#include "ember/RematchDecision.hxx"

#include <windows.h>
#include <xxhash.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>

namespace {

using namespace sfve;
using namespace sfve::testbed;
namespace rip = sfve::adapter::rematch_in_place;
using Answer = rip::Answer;

constexpr std::uint32_t kAsked = sf4e::rematch::AskingBit;
constexpr std::uint32_t kChose = sf4e::rematch::ChoiceBit;

// Frames [kIgnoreFrom, kIgnoreTo) read no inputs, as SFV's round calls do.
constexpr int kIgnoreFrom = 120, kIgnoreTo = 170;

replay::Replay SampleReplay()
{
	replay::Replay r;
	r.header.product = "SFV Ember";
	r.header.build = "sfv-ember/13173604/0.2/0123456789abcdef";
	r.header.startedAt = 1'762'500'000;
	r.header.slot = 1;
	r.header.players[0] = {"player1", 3, 0, 1, "RYU", 0, 1};
	r.header.players[1] = {"player2", 7, 2, 4, "CNL", 2, 4};
	r.header.stage = 12;
	r.header.stageCode = "S25";
	r.header.rounds = 3;
	r.header.roundSeconds = 99;
	r.header.timeFrames = 5940;
	r.header.seed = 0xDEADBEEF;
	r.header.inputDelay = 2;
	r.header.games.push_back({true, 1, {1, 2}, 3, 3, 4500});
	r.header.complete = true;
	for (int f = 0; f < 600; f++) {
		RecordedFrame frame;
		frame.flags = f >= 500 ? RecordedFrame::kHeld : f < 100 ? 0 : RecordedFrame::kFight;
		frame.inputs = f < 100 ? FrameInputs{} : ScriptedFrame(f / 4, 3);
		if (frame.Held()) frame.inputs.p[0] |= kChose;
		r.frames.push_back(frame);
	}
	replay::CountFights(r.frames, &r.header.fightFrames, &r.header.fightStretches);
	for (int f = 0; f <= 600; f += 60) r.checkpoints.push_back({f, 0x1000u + (uint64_t)f});
	return r;
}

TEST_CASE("a replay file reads back as written, and anything else is refused")
{
	const replay::Replay r = SampleReplay();
	CHECK(r.header.fightFrames == 400u);
	CHECK(r.header.fightStretches == 1u);
	const std::vector<uint8_t> bytes = replay::Encode(r);
	// Run-length coded: the 100 neutral frames are one run.
	CHECK(bytes.size() < 600 * 9);

	replay::Replay back;
	std::string error;
	REQUIRE_MESSAGE(replay::Decode(bytes.data(), bytes.size(), &back, &error), error);
	CHECK(back.frames == r.frames);
	CHECK(back.checkpoints == r.checkpoints);
	CHECK(back.header.build == r.header.build);
	CHECK(back.header.players[1].name == "player2");
	CHECK(back.header.players[1].code == "CNL");
	CHECK(back.header.players[1].launchColour == 4);
	CHECK(back.header.seed == 0xDEADBEEFu);
	REQUIRE(back.header.games.size() == 1u);
	CHECK(back.header.games[0].winner == 1);
	CHECK(back.header.games[0].wins[1] == 2);
	CHECK(back.header.complete);
	replay::Header header;
	REQUIRE(replay::DecodeHeader(bytes.data(), bytes.size(), &header, &error));
	CHECK(header.stageCode == "S25");

	SUBCASE("a byte changed anywhere fails the hash")
	{
		for (const size_t at : {(size_t)9, (size_t)20, bytes.size() / 2, bytes.size() - 9}) {
			std::vector<uint8_t> damaged = bytes;
			damaged[at] ^= 0x40;
			CHECK_FALSE(replay::Decode(damaged.data(), damaged.size(), &back, &error));
		}
	}
	SUBCASE("a cut file, another magic or another version")
	{
		CHECK_FALSE(replay::Decode(bytes.data(), bytes.size() - 1, &back, &error));
		CHECK_FALSE(replay::Decode(bytes.data(), 10, &back, &error));
		CHECK_FALSE(replay::Decode(nullptr, 0, &back, &error));
		std::vector<uint8_t> other = bytes;
		other[0] = 'X';
		CHECK_FALSE(replay::Decode(other.data(), other.size(), &back, &error));
		CHECK(error == "not an SFV Ember replay");
		replay::Replay v2 = r;
		std::vector<uint8_t> versioned = replay::Encode(v2);
		versioned[8] = 2; // the version, then the hash made good again
		const uint64_t hash = XXH64(versioned.data(), versioned.size() - 8, 0);
		for (int i = 0; i < 8; i++) versioned[versioned.size() - 8 + i] = (uint8_t)(hash >> (8 * i));
		CHECK_FALSE(replay::Decode(versioned.data(), versioned.size(), &back, &error));
		CHECK(error == "a replay of another version");
	}
	SUBCASE("checkpoints at the session's interval, or at beta.8's 60 (SampleReplay's), load; any other spacing is refused")
	{
		// The sample's checkpoints are 60 apart, as a beta.8 file's are: still
		// multiples of the interval (read back above).
		CHECK(60 % RollbackSession::kCheckpointInterval == 0);
		replay::Replay now = r;
		now.checkpoints.clear();
		for (int f = 0; f <= 600; f += RollbackSession::kCheckpointInterval) now.checkpoints.push_back({f, 0x2000u + (uint64_t)f});
		std::vector<uint8_t> b = replay::Encode(now);
		REQUIRE_MESSAGE(replay::Decode(b.data(), b.size(), &back, &error), error);
		CHECK(back.checkpoints == now.checkpoints);
		replay::Replay odd = r;
		odd.checkpoints = {{RollbackSession::kCheckpointInterval + 1, 1}}; // inside the frames, not a multiple
		b = replay::Encode(odd);
		CHECK_FALSE(replay::Decode(b.data(), b.size(), &back, &error));
	}
	SUBCASE("checkpoints past the frames, or a frame of no known kind")
	{
		replay::Replay bad = r;
		bad.checkpoints.push_back({660, 1});
		std::vector<uint8_t> b = replay::Encode(bad);
		CHECK_FALSE(replay::Decode(b.data(), b.size(), &back, &error));
		bad = r;
		bad.frames[3].flags = 0x80;
		b = replay::Encode(bad);
		CHECK_FALSE(replay::Decode(b.data(), b.size(), &back, &error));
		bad = r;
		bad.frames[3].flags = RecordedFrame::kHeld | RecordedFrame::kFight;
		b = replay::Encode(bad);
		CHECK_FALSE(replay::Decode(b.data(), b.size(), &back, &error));
	}
}

TEST_CASE("a replay's header keeps each side's V-Skill and V-Trigger, and one written without them reads 0")
{
	replay::Replay r = SampleReplay();
	r.header.players[0].vSkill = 1;
	r.header.players[1].vTrigger = 1;
	const std::vector<uint8_t> bytes = replay::Encode(r);
	replay::Replay back;
	std::string error;
	REQUIRE_MESSAGE(replay::Decode(bytes.data(), bytes.size(), &back, &error), error);
	CHECK(back.header.players[0].vSkill == 1);
	CHECK(back.header.players[0].vTrigger == 0);
	CHECK(back.header.players[1].vSkill == 0);
	CHECK(back.header.players[1].vTrigger == 1);

	// The file with another header (after the magic and the version: the
	// header's length and text), the rest as it was and the hash made good.
	uint32_t length = 0;
	std::memcpy(&length, bytes.data() + 12, 4);
	nlohmann::json header = nlohmann::json::parse(bytes.begin() + 16, bytes.begin() + 16 + length);
	const auto withHeader = [&](const nlohmann::json& replaced) {
		const std::string text = replaced.dump();
		std::vector<uint8_t> file(bytes.begin(), bytes.begin() + 12);
		for (int i = 0; i < 4; i++) file.push_back((uint8_t)((uint32_t)text.size() >> (8 * i)));
		file.insert(file.end(), text.begin(), text.end());
		file.insert(file.end(), bytes.begin() + 16 + length, bytes.end() - 8);
		const uint64_t hash = XXH64(file.data(), file.size(), 0);
		for (int i = 0; i < 8; i++) file.push_back((uint8_t)(hash >> (8 * i)));
		return file;
	};
	// As a build before section 40 wrote it: no V-Skill or V-Trigger.
	for (nlohmann::json& player : header["players"]) {
		player.erase("vSkill");
		player.erase("vTrigger");
	}
	const std::vector<uint8_t> old = withHeader(header);
	REQUIRE_MESSAGE(replay::Decode(old.data(), old.size(), &back, &error), error);
	for (const replay::Player& player : back.header.players) {
		CHECK(player.vSkill == 0);
		CHECK(player.vTrigger == 0);
	}
	CHECK(back.frames == r.frames);
	// A value of the wrong kind is refused, as any other field's is.
	header["players"][0]["vSkill"] = "II";
	const std::vector<uint8_t> bad = withHeader(header);
	CHECK_FALSE(replay::Decode(bad.data(), bad.size(), &back, &error));
}

TEST_CASE("a replay's header keeps each side's costume code, and one written without them reads none")
{
	replay::Replay r = SampleReplay();
	CHECK(r.header.players[0].loadCode == -1);
	r.header.players[0].loadCode = 0x290; // LP+MK+HP (EMBER_LIBRARY.md section 55)
	r.header.players[1].loadCode = 0;
	std::vector<uint8_t> bytes = replay::Encode(r);
	replay::Replay back;
	std::string error;
	REQUIRE_MESSAGE(replay::Decode(bytes.data(), bytes.size(), &back, &error), error);
	CHECK(back.header.players[0].loadCode == 0x290);
	CHECK(back.header.players[1].loadCode == 0);
	// A header from before codes: none, as the battle it recorded loaded.
	bytes = replay::Encode(SampleReplay());
	REQUIRE_MESSAGE(replay::Decode(bytes.data(), bytes.size(), &back, &error), error);
	CHECK(back.header.players[0].loadCode == -1);
	CHECK(back.header.players[1].loadCode == -1);
}

TEST_CASE("a replay's header keeps P1's start state hash, and one written without it reads 0")
{
	replay::Replay r = SampleReplay();
	r.header.startHash = 0x0123456789ABCDEFull;
	std::vector<uint8_t> bytes = replay::Encode(r);
	const std::string text(bytes.begin(), bytes.end());
	CHECK(text.find("\"startHash\":\"0123456789abcdef\"") != std::string::npos);
	replay::Replay back;
	std::string error;
	REQUIRE_MESSAGE(replay::Decode(bytes.data(), bytes.size(), &back, &error), error);
	CHECK(back.header.startHash == 0x0123456789ABCDEFull);
	// Not known: not written, and a header without it reads 0.
	bytes = replay::Encode(SampleReplay());
	CHECK(std::string(bytes.begin(), bytes.end()).find("startHash") == std::string::npos);
	REQUIRE_MESSAGE(replay::Decode(bytes.data(), bytes.size(), &back, &error), error);
	CHECK(back.header.startHash == 0);
}

TEST_CASE("the recorder keeps a frame's last report, and only frames made final")
{
	replay::Recorder rec;
	rec.Start();
	RecordedFrame a{FrameInputs{{1, 2}}, RecordedFrame::kFight}, b{FrameInputs{{3, 4}}, RecordedFrame::kFight};
	rec.OnFrame(0, a);
	rec.OnFrame(1, a);
	rec.OnFrame(1, b); // a rollback's replay of frame 1
	rec.OnFrame(2, a);
	rec.OnCheckpoint(0, 7);
	rec.Confirm(2);
	ReplayFrames frames;
	std::vector<std::pair<int, uint64_t>> checkpoints;
	rec.Take(&frames, &checkpoints);
	REQUIRE(frames.size() == 2u);
	CHECK(frames[1] == b);
	CHECK(checkpoints.size() == 1u);
	rec.Confirm(1); // never goes back
	CHECK(rec.FinalFrames() == 2u);
	CHECK_FALSE(rec.Broken());
	rec.OnFrame(5, a); // a gap: the recording cannot be trusted
	CHECK(rec.Broken());
}

// Two fighters and a watcher over loopback, each with a recorder.
struct RecordedTrio {
	MockFighter gameA, gameB, gameW;
	RollbackSession a{gameA}, b{gameB}, w{gameW};
	replay::Recorder recA, recB, recW;

	explicit RecordedTrio(int delay)
	{
		for (MockFighter* g : {&gameA, &gameB, &gameW}) g->IgnoreInputsBetween(kIgnoreFrom, kIgnoreTo);
		const uint16_t base = LoopbackPortBase();
		LoopbackPortBase();
		RollbackSession::Config ca, cb, cw;
		ca.localPlayer = 0;
		ca.localPort = base;
		ca.remotePort = (uint16_t)(base + 1);
		ca.watchers.push_back({2, (uint16_t)(base + 2)});
		cb.localPlayer = 1;
		cb.localPort = (uint16_t)(base + 1);
		cb.remotePort = base;
		ca.inputDelay = cb.inputDelay = delay;
		ca.keepAllHashes = cb.keepAllHashes = cw.keepAllHashes = true;
		ca.hashEveryFrames = cb.hashEveryFrames = cw.hashEveryFrames = 1;
		ca.disconnectTimeoutMs = cb.disconnectTimeoutMs = 1000;
		cw.watching = true;
		cw.localPort = (uint16_t)(base + 2);
		cw.remotePort = base;
		Hook(ca, recA);
		Hook(cb, recB);
		Hook(cw, recW);
		std::string error;
		REQUIRE_MESSAGE(a.Start(ca, &error), error);
		REQUIRE_MESSAGE(b.Start(cb, &error), error);
		REQUIRE_MESSAGE(w.Start(cw, &error), error);
	}
	static void Hook(RollbackSession::Config& c, replay::Recorder& rec)
	{
		rec.Start();
		c.onFrame = [&rec](int frame, const RecordedFrame& f) { rec.OnFrame(frame, f); };
		c.onCheckpoint = [&rec](int frame, uint64_t hash) { rec.OnCheckpoint(frame, hash); };
	}
	static void Confirm(const RollbackSession& s, replay::Recorder& rec)
	{
		rec.Confirm(std::min(s.Frame(), s.LastConfirmedFrame() + 1));
	}
	void ConfirmAll()
	{
		Confirm(a, recA);
		Confirm(b, recB);
		Confirm(w, recW);
	}

	bool RunTo(int frames, bool watcherToo = true, DWORD deadlineMs = 30000)
	{
		const ULONGLONG start = GetTickCount64();
		while (a.Frame() < frames || b.Frame() < frames || (watcherToo && w.Frame() < frames)) {
			if (GetTickCount64() - start > deadlineMs) return false;
			if (a.Frame() < frames) a.Step(ScriptedInput(0, a.Frame(), kLoopbackSeed));
			else a.Poll();
			if (b.Frame() < frames) b.Step(ScriptedInput(1, b.Frame(), kLoopbackSeed));
			else b.Poll();
			if (watcherToo) {
				for (int extra = 0; extra < 4 && w.Frame() < frames; extra++) {
					if (w.Step(0) != RollbackSession::StepResult::Advanced) break;
				}
			}
			ConfirmAll();
			if (a.Failed() || b.Failed() || w.Failed()) return false;
			Sleep(0);
		}
		return true;
	}

	// The WIN held from the fighters' current frame (the watcher catches up
	// first): the fighters answer Rematch, P1 at its 15th held frame and P2
	// at its 40th. Each copy's decided frame.
	std::optional<int> decided[3];
	bool HoldAndRematch()
	{
		const int heldFrom = a.Frame();
		ULONGLONG start = GetTickCount64();
		while (w.Frame() < heldFrom && GetTickCount64() - start < 10000) {
			a.Poll();
			b.Poll();
			w.Step(0);
		}
		if (w.Frame() != heldFrom || b.Frame() != heldFrom) return false;
		RollbackSession* sessions[3] = {&a, &b, &w};
		MockFighter* games[3] = {&gameA, &gameB, &gameW};
		const int from[2] = {15, 40};
		start = GetTickCount64();
		while (!decided[0] || !decided[1] || !decided[2]) {
			if (GetTickCount64() - start > 30000) return false;
			for (int i = 0; i < 3; i++) {
				RollbackSession& s = *sessions[i];
				if (decided[i]) {
					s.Poll();
					continue;
				}
				const int held = s.HeldFrames();
				const std::uint32_t bits = i == 2 ? 0 : held >= from[i] ? kChose : kAsked;
				Answer answer = Answer::Wait;
				const RollbackSession::StepResult r = s.StepHeld(bits, [&](const FrameInputs& in) {
					answer = rip::Decide(in.p[0], in.p[1], s.HeldFrames());
					if (answer == Answer::Rematch) games[i]->Advance(FrameInputs{}, false);
				});
				if (r == RollbackSession::StepResult::Failed) return false;
				if (r == RollbackSession::StepResult::Advanced && answer != Answer::Wait) decided[i] = s.Frame();
			}
			ConfirmAll();
			Sleep(0);
		}
		return true;
	}
};

// Plays `recording` on a fresh game as NetplayMode plays a replay: a held
// frame from `heldFrom` until the answers decide, the rematch's change on the
// decided frame, and every other frame stepped. The playback's session.
struct Playback {
	MockFighter game;
	RollbackSession s{game};
	replay::Recorder rec; // the playback reports its frames too
	std::vector<std::pair<int, uint64_t>> checkpoints;

	Playback(const ReplayFrames& recording, int ignoreFrom = kIgnoreFrom, int ignoreTo = kIgnoreTo)
	{
		game.IgnoreInputsBetween(ignoreFrom, ignoreTo);
		RollbackSession::Config c;
		c.replay = &recording;
		c.keepAllHashes = true;
		c.hashEveryFrames = 1;
		c.onCheckpoint = [this](int frame, uint64_t hash) { checkpoints.push_back({frame, hash}); };
		rec.Start();
		c.onFrame = [this](int frame, const RecordedFrame& f) { rec.OnFrame(frame, f); };
		std::string error;
		REQUIRE_MESSAGE(s.Start(c, &error), error);
	}
	// Steps to the end: the frames, then the step that finds none left.
	RollbackSession::StepResult Run(int heldFrom, int holdFor = -1)
	{
		RollbackSession::StepResult r = RollbackSession::StepResult::Advanced;
		bool holding = false;
		while (r == RollbackSession::StepResult::Advanced) {
			if (s.Frame() == heldFrom) holding = true;
			if (holding) {
				Answer answer = Answer::Wait;
				r = s.StepHeld(0, [&](const FrameInputs& in) {
					answer = rip::Decide(in.p[0], in.p[1], s.HeldFrames());
					if (holdFor >= 0 && s.HeldFrames() + 1 >= holdFor) answer = Answer::Rematch; // a wrong decision
					if (answer == Answer::Rematch) game.Advance(FrameInputs{}, false);
				});
				if (answer != Answer::Wait) holding = false;
			} else {
				r = s.Step(0);
			}
		}
		return r;
	}
};

TEST_CASE("two fighters and a watcher record the same battle, and its playback reaches every state they did")
{
	for (const int delay : {1, 3}) {
		INFO("input delay " << delay);
		RecordedTrio trio(delay);
		REQUIRE(trio.RunTo(300));
		REQUIRE(trio.RunTo(310, false));
		const int heldFrom = trio.a.Frame();
		REQUIRE(trio.HoldAndRematch());
		CHECK(*trio.decided[0] == *trio.decided[1]);
		CHECK(*trio.decided[2] == *trio.decided[0]);
		const int resumed = trio.a.Frame();
		REQUIRE(trio.RunTo(resumed + 250));
		// The battle's end: the frames final on all three copies.
		const int end = (int)std::min({trio.recA.FinalFrames(), trio.recB.FinalFrames(), trio.recW.FinalFrames()});
		REQUIRE(end > resumed + 200);

		ReplayFrames fa, fb, fw;
		std::vector<std::pair<int, uint64_t>> ca, cb, cw;
		trio.recA.Take(&fa, &ca);
		trio.recB.Take(&fb, &cb);
		trio.recW.Take(&fw, &cw);
		CHECK_FALSE(trio.recA.Broken());
		CHECK_FALSE(trio.recB.Broken());
		CHECK_FALSE(trio.recW.Broken());
		REQUIRE(fa.size() >= (size_t)end);
		REQUIRE(fb.size() >= (size_t)end);
		REQUIRE(fw.size() >= (size_t)end);
		fa.resize((size_t)end);
		fb.resize((size_t)end);
		fw.resize((size_t)end);
		// The three copies recorded the same frames: the confirmed inputs.
		CHECK(fa == fb);
		CHECK(fw == fa);
		// The frames hold what happened: the ignored stretch, the held WIN and
		// the fight on both sides of them.
		uint32_t fight = 0, stretches = 0;
		replay::CountFights(fa, &fight, &stretches);
		CHECK(stretches == 3u); // before the ignored frames, after them to the WIN, and the game after it
		CHECK(!fa[kIgnoreFrom].Fight());
		CHECK(fa[kIgnoreFrom].inputs == FrameInputs{});
		CHECK(fa[(size_t)heldFrom].Held());
		CHECK(fa[(size_t)*trio.decided[0] - 1].Held());
		CHECK(fa[(size_t)*trio.decided[0]].Fight());
		// The fighters' checkpoints, every interval up to the end, alike.
		CHECK(ca.size() >= (size_t)(end / RollbackSession::kCheckpointInterval));
		CHECK(std::equal(ca.begin(), ca.begin() + (ptrdiff_t)std::min(ca.size(), cb.size()), cb.begin()));

		// The battle again from A's recording, on a game that never saw it.
		Playback play(fa);
		const RollbackSession::StepResult last = play.Run(heldFrom);
		CHECK(last == RollbackSession::StepResult::Failed);
		CHECK(play.s.FailureReason().find("the replay ends") == 0);
		CHECK(play.s.Frame() == end);
		int compared = 0;
		for (int f = 0; f <= end; f++) {
			uint64_t live = 0, played = 0;
			if (!trio.a.HashAt(f, &live) || !play.s.HashAt(f, &played)) continue;
			INFO("frame " << f);
			CHECK(played == live);
			compared++;
		}
		CHECK(compared >= end - 2); // every frame, frame 0 and the frozen WIN's included
		// Its checkpoints are the recording's.
		for (const auto& [frame, hash] : play.checkpoints) {
			const auto it = std::find_if(ca.begin(), ca.end(), [&](const auto& c) { return c.first == frame; });
			if (it == ca.end()) continue;
			INFO("checkpoint " << frame);
			CHECK(it->second == hash);
		}
		// What the playback gave the game is what the fighters' sessions gave it.
		ReplayFrames fp;
		std::vector<std::pair<int, uint64_t>> cp;
		play.rec.Confirm(play.s.Frame());
		play.rec.Take(&fp, &cp);
		CHECK(fp == fa);
	}
}

TEST_CASE("a playback that leaves its recording fails at that frame")
{
	RecordedTrio trio(2);
	REQUIRE(trio.RunTo(300));
	REQUIRE(trio.RunTo(310, false));
	const int heldFrom = trio.a.Frame();
	REQUIRE(trio.HoldAndRematch());
	REQUIRE(trio.RunTo(trio.a.Frame() + 100));
	ReplayFrames fa;
	std::vector<std::pair<int, uint64_t>> ca;
	trio.recA.Take(&fa, &ca);

	SUBCASE("held where the battle was played")
	{
		Playback play(fa);
		CHECK(play.Run(heldFrom - 30) == RollbackSession::StepResult::Failed);
		CHECK(play.s.FailureReason().find("the playback left the recording at frame " + std::to_string(heldFrom - 30) + " ") == 0);
	}
	SUBCASE("the WIN decided early, so the game goes on where the recording held")
	{
		Playback play(fa);
		CHECK(play.Run(heldFrom, 5) == RollbackSession::StepResult::Failed);
		CHECK(play.s.FailureReason().find("the playback left the recording at frame " + std::to_string(heldFrom + 5) + " ") == 0);
	}
	SUBCASE("a game whose frames read inputs where the recorded ones did not")
	{
		Playback play(fa, kIgnoreFrom + 10, kIgnoreTo);
		CHECK(play.Run(heldFrom) == RollbackSession::StepResult::Failed);
		CHECK(play.s.FailureReason().find("the playback left the recording at frame " + std::to_string(kIgnoreFrom) + " ") == 0);
	}
}

} // namespace
