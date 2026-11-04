#include "mod/FramePacer.hxx"

#include <windows.h>

#include <stdio.h>

namespace sfve::mod {

namespace {

double NowMs()
{
	LARGE_INTEGER now, perSecond;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&perSecond);
	return (double)now.QuadPart * 1000.0 / (double)perSecond.QuadPart;
}

constexpr double kSpinMs = 0.5; // the end of a wait spins, as timer wake-ups jitter
constexpr double kPeriodMs = 1000.0 / 60.0; // the battle's frame period

} // namespace

FramePacer::FramePacer()
{
	controller_.InitDefaults();
	controller_.continuous = true;
	// Ember's gain assumes both sides repay half the rift each; here only the
	// side ahead can act (it lengthens frames), so it repays the whole: at the
	// default gain a 6 ms/s rate difference between two copies left a steady
	// 1.5-frame rift.
	controller_.riftGain = 4.0 / 120.0;
	timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
}

FramePacer::~FramePacer()
{
	if (timer_) CloseHandle(timer_);
}

void FramePacer::Reset()
{
	controller_.Reset();
	controller_.ResetStats();
	pingMs_ = -1;
	lastFrameMs_ = 0.0;
}

void FramePacer::Frame(const RollbackSession& session, bool stalled)
{
	const double previous = lastFrameMs_;
	lastFrameMs_ = NowMs();
	if (stalled) {
		// Stalled frames already wait, and their rift sample is unreliable.
		controller_.OnPredictionStall();
		return;
	}
	RollbackSession::Rift rift{};
	if (!session.ReadRift(&rift)) return;
	pingMs_ = rift.pingMs;
	controller_.OnRiftSample(rift.localFramesBehind, rift.remoteFramesBehind);
	const double shift = controller_.NextShiftMs();
	// Only right after a frame of the session (not after a held one) is the
	// previous frame's start a reference for this one.
	if (shift <= 0.0 || previous <= 0.0 || lastFrameMs_ - previous > 2.0 * kPeriodMs) return;
	// The engine's limiter has already waited out this frame's period; only
	// waiting past it makes the frame longer.
	WaitUntil(previous + kPeriodMs + shift);
	lastFrameMs_ = NowMs();
	controller_.OnShiftApplied(sf4e::pacing::AppliedShiftMs(kPeriodMs, lastFrameMs_ - previous, shift));
}

void FramePacer::WaitUntil(double deadlineMs)
{
	const double sleepMs = deadlineMs - NowMs() - kSpinMs;
	if (timer_ && sleepMs > 0.0) {
		LARGE_INTEGER due;
		due.QuadPart = -(LONGLONG)(sleepMs * 10000.0); // relative, 100 ns units
		if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer_, INFINITE);
	}
	while (NowMs() < deadlineMs) YieldProcessor();
}

std::string FramePacer::Describe() const
{
	const sf4e::pacing::PacingController& p = controller_;
	char text[256];
	snprintf(text, sizeof(text),
		"{\"riftFrames\":%.2f,\"maxRiftFrames\":%.2f,\"outstandingMs\":%.2f,\"slowedMs\":%.1f,\"samples\":%llu,"
		"\"pingMs\":%d}",
		p.riftFramesEma, p.maxAbsRiftFrames, p.outstandingMs, p.msSlowedTotal, (unsigned long long)p.riftSamples,
		pingMs_);
	return text;
}

} // namespace sfve::mod
