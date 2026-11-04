#include <doctest/doctest.h>

#include "TestInputs.hxx"
#include "core/RollbackSession.hxx"

#include <windows.h>

#include <algorithm>

using namespace sfve;
using namespace sfve::testbed;

namespace {

constexpr uint32_t kSeed = 7;

uint16_t PortBase()
{
	static uint16_t next = (uint16_t)(20000 + (GetCurrentProcessId() % 5000) * 4);
	const uint16_t port = next;
	next += 2;
	return port;
}

struct Pair {
	MockFighter gameA, gameB;
	RollbackSession a{gameA}, b{gameB};

	Pair(unsigned bugs, int delay) : gameA(bugs), gameB(bugs)
	{
		const uint16_t base = PortBase();
		RollbackSession::Config ca, cb;
		ca.localPlayer = 0;
		ca.localPort = base;
		ca.remotePort = (uint16_t)(base + 1);
		cb.localPlayer = 1;
		cb.localPort = (uint16_t)(base + 1);
		cb.remotePort = base;
		ca.inputDelay = cb.inputDelay = delay;
		ca.verifyLoads = cb.verifyLoads = true;
		ca.disconnectTimeoutMs = cb.disconnectTimeoutMs = 1000;
		std::string error;
		REQUIRE_MESSAGE(a.Start(ca, &error), error);
		REQUIRE_MESSAGE(b.Start(cb, &error), error);
	}

	// Steps both peers until each has run `frames` frames, or the deadline.
	bool RunTo(int frames, DWORD deadlineMs = 30000)
	{
		const ULONGLONG start = GetTickCount64();
		while (a.Frame() < frames || b.Frame() < frames) {
			if (GetTickCount64() - start > deadlineMs) return false;
			if (a.Frame() < frames) a.Step(ScriptedInput(0, a.Frame(), kSeed));
			if (b.Frame() < frames) b.Step(ScriptedInput(1, b.Frame(), kSeed));
			if (a.Failed() || b.Failed()) return false;
			Sleep(0);
		}
		return true;
	}
};

// Hash of an offline reference run at each frame.
std::vector<uint64_t> ReferenceHashes(int frames)
{
	MockFighter reference;
	std::vector<uint64_t> hashes;
	hashes.push_back(reference.Layout().HashLive());
	for (int f = 0; f < frames; f++) {
		reference.Advance(ScriptedFrame(f, kSeed), false);
		hashes.push_back(reference.Layout().HashLive());
	}
	return hashes;
}

} // namespace

TEST_CASE("two peers over loopback UDP roll back and agree with an offline run")
{
	Pair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(600));
	// Let confirmation catch up with the last frames.
	pair.RunTo(620);

	const auto& sa = pair.a.GetStats();
	const auto& sb = pair.b.GetStats();
	MESSAGE("A rollbacks " << sa.rollbacks << " (longest " << sa.longestRollback << "), B rollbacks " << sb.rollbacks);
	CHECK(sa.rollbacks + sb.rollbacks > 0);
	CHECK(sa.loadMismatches == 0);
	CHECK(sb.loadMismatches == 0);

	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	REQUIRE(confirmed >= 400);
	const std::vector<uint64_t> reference = ReferenceHashes(confirmed);
	int compared = 0;
	for (int f = confirmed - 200; f <= confirmed; f++) {
		uint64_t ha = 0, hb = 0;
		if (!pair.a.HashAt(f, &ha) || !pair.b.HashAt(f, &hb)) continue;
		INFO("frame " << f);
		CHECK(ha == hb);
		CHECK(ha == reference[f]);
		compared++;
	}
	CHECK(compared > 100);
}

TEST_CASE("input delay keeps the peers in step")
{
	Pair pair(kBugNone, 2);
	REQUIRE(pair.RunTo(400));
	pair.RunTo(420);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	REQUIRE(confirmed > 300);
	uint64_t ha = 0, hb = 0;
	REQUIRE(pair.a.HashAt(confirmed, &ha));
	REQUIRE(pair.b.HashAt(confirmed, &hb));
	CHECK(ha == hb);
}

TEST_CASE("an unmasked heap pointer makes identical play look like a desync")
{
	Pair pair(kBugUnmaskedPointer, 0);
	REQUIRE(pair.RunTo(120));
	pair.RunTo(140);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	uint64_t ha = 0, hb = 0;
	REQUIRE(pair.a.HashAt(confirmed, &ha));
	REQUIRE(pair.b.HashAt(confirmed, &hb));
	CHECK(ha != hb);
	CHECK(pair.gameA.Health(0) == pair.gameB.Health(0));
}

TEST_CASE("a peer that goes away ends the other's session cleanly")
{
	Pair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(60));
	pair.b.Close();
	const ULONGLONG start = GetTickCount64();
	while (!pair.a.Failed() && GetTickCount64() - start < 5000) {
		pair.a.Step(ScriptedInput(0, pair.a.Frame(), kSeed));
		Sleep(1);
	}
	CHECK(pair.a.Failed());
	CHECK_FALSE(pair.a.Live());
	CHECK(pair.a.Step(0) == RollbackSession::StepResult::Failed);
}

TEST_CASE("peers keep matching per-region checkpoints for confirmed frames")
{
	Pair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(400));
	pair.RunTo(420);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	const int checkpoint = pair.a.LatestCheckpoint(confirmed);
	REQUIRE(checkpoint >= 300);
	CHECK(checkpoint % RollbackSession::kCheckpointInterval == 0);
	std::vector<uint64_t> ha, hb;
	REQUIRE(pair.a.RegionHashesAt(checkpoint, &ha));
	REQUIRE(pair.b.RegionHashesAt(checkpoint, &hb));
	CHECK(ha.size() == pair.gameA.Layout().Regions().size());
	CHECK(ha == hb);
}
