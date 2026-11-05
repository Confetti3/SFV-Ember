#pragma once

// Two RollbackSessions over loopback UDP, each running a MockFighter and
// playing the scripted inputs (TestInputs.hxx): the rollback core's tests and
// the desync check's tests (EmberAdapterTests) run real peers with it.

#include <doctest/doctest.h>

#include "TestInputs.hxx"
#include "core/RollbackSession.hxx"

#include <windows.h>

#include <string>

namespace sfve::testbed {

// The scripted inputs' seed both peers play.
constexpr uint32_t kLoopbackSeed = 7;

// Two free-looking loopback ports per pair, apart for each test process.
inline uint16_t LoopbackPortBase()
{
	static uint16_t next = (uint16_t)(20000 + (GetCurrentProcessId() % 5000) * 4);
	const uint16_t port = next;
	next += 2;
	return port;
}

struct LoopbackPair {
	MockFighter gameA, gameB;
	RollbackSession a{gameA}, b{gameB};

	LoopbackPair(unsigned bugs, int delay, bool keepAllHashes = false) : gameA(bugs), gameB(bugs)
	{
		const uint16_t base = LoopbackPortBase();
		RollbackSession::Config ca, cb;
		ca.localPlayer = 0;
		ca.localPort = base;
		ca.remotePort = (uint16_t)(base + 1);
		cb.localPlayer = 1;
		cb.localPort = (uint16_t)(base + 1);
		cb.remotePort = base;
		ca.inputDelay = cb.inputDelay = delay;
		ca.verifyLoads = cb.verifyLoads = true;
		ca.keepAllHashes = cb.keepAllHashes = keepAllHashes;
		ca.disconnectTimeoutMs = cb.disconnectTimeoutMs = 1000;
		std::string error;
		REQUIRE_MESSAGE(a.Start(ca, &error), error);
		REQUIRE_MESSAGE(b.Start(cb, &error), error);
	}

	// Steps both peers until each has run `frames` frames, or the deadline. A
	// peer already there only polls, as a host holding its frame does, so
	// the other can still finish synchronizing and catch up.
	bool RunTo(int frames, DWORD deadlineMs = 30000)
	{
		const ULONGLONG start = GetTickCount64();
		while (a.Frame() < frames || b.Frame() < frames) {
			if (GetTickCount64() - start > deadlineMs) return false;
			if (a.Frame() < frames) a.Step(ScriptedInput(0, a.Frame(), kLoopbackSeed));
			else a.Poll();
			if (b.Frame() < frames) b.Step(ScriptedInput(1, b.Frame(), kLoopbackSeed));
			else b.Poll();
			if (a.Failed() || b.Failed()) return false;
			Sleep(0);
		}
		return true;
	}
};

} // namespace sfve::testbed
