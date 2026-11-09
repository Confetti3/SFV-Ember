// SFV's fighters in the frame meter's terms (ember/SfvMeterSample) and the
// library's meter over them (training/FrameMeter.hxx), on ticks recorded in
// SFV's Training (EMBER_LIBRARY.md section 39: RYU against RYU, read after
// each tick with the offsets of SfvFighterOffsets.hxx). The numbers each
// trace must give are the game's: its frame data and its own count of frames
// until each fighter can act.

#include <doctest/doctest.h>

#include "ember/SfvMeterSample.hxx"

#include "training/FrameMeter.hxx"

#include <array>
#include <vector>

using sfve::adapter::HeaderPhase;
using sfve::adapter::MeterFeed;
using sfve::adapter::MeterMove;
using sfve::adapter::MeterSample;
using sfve::adapter::MeterStatus;
using sfve::mod::FighterFrame;
using sfve::mod::ScriptHeader;
using sfve::mod::sfv::fighter::StateKind;
namespace training = sf4e::training;
using training::ActorStatus;

namespace {

// One fighter on one recorded tick: its state kind, script, script frame,
// header, posture, time scale, the action its state change started, frames
// until it can act, hitstop, vital and its maximum (whole units), and
// whether its projectile came out.
struct Tick {
	int kind, script;
	float frame;
	ScriptHeader header;
	int posture;
	float timeScale;
	int started, until;
	bool hitStop;
	int vital, vitalMax;
	bool projectile;
};
using Trace = std::vector<std::array<Tick, 2>>;

FighterFrame Frame(const Tick& tick)
{
	FighterFrame frame;
	frame.valid = true;
	frame.stateKind = static_cast<StateKind>(tick.kind);
	frame.script = tick.script;
	frame.scriptFrame = tick.frame;
	frame.header = tick.header;
	frame.posture = tick.posture;
	frame.timeScale = tick.timeScale;
	frame.startedAction = tick.started;
	frame.untilActionable = tick.until;
	frame.hitStop = tick.hitStop;
	frame.vital = static_cast<float>(tick.vital);
	frame.vitalMax = static_cast<float>(tick.vitalMax);
	frame.projectileOut = tick.projectile;
	return frame;
}

std::array<FighterFrame, 2> Frames(const std::array<Tick, 2>& tick)
{
	return {Frame(tick[0]), Frame(tick[1])};
}

// The meter after the trace, fed an update of one tick at a time, as
// SfvFrameMeter feeds it.
training::FrameMeter Measure(const Trace& trace)
{
	training::FrameMeter meter;
	MeterFeed feed;
	for (const auto& tick : trace) feed.Feed(meter, Frames(tick), 1);
	return meter;
}

// P1's standing LP into P2 standing still: it hits on its third frame.
const Trace kStandingLpHit = {
	{{{1, 0, 424.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 424.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 1025, 1025, false}}},
	{{{1, 0, 425.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 425.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 1025, 1025, false}}},
	{{{1, 0, 426.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 426.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 1025, 1025, false}}},
	{{{1, 0, 427.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 427.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 1025, 1025, false}}},
	{{{19, 600, 0.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 428.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 1025, 1025, false}}},
	{{{19, 600, 1.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 10, false, 1025, 1025, false}, {1, 0, 429.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 9, false, 1025, 1025, false}, {1, 0, 430.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 16, true, 1025, 1025, false}, {22, 141, 0.0f, {-1, 10, 14, 20}, 0, 0.0f, 141, 20, true, 995, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 15, true, 1025, 1025, false}, {22, 141, 0.0f, {-1, 10, 14, 20}, 0, 0.0f, 141, 19, true, 995, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 14, true, 1025, 1025, false}, {22, 141, 0.0f, {-1, 10, 14, 20}, 0, 0.0f, 141, 18, true, 995, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 13, true, 1025, 1025, false}, {22, 141, 0.0f, {-1, 10, 14, 20}, 0, 0.0f, 141, 17, true, 995, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 12, true, 1025, 1025, false}, {22, 141, 0.0f, {-1, 10, 14, 20}, 0, 0.0f, 141, 16, true, 995, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 11, true, 1025, 1025, false}, {22, 141, 0.0f, {-1, 10, 14, 20}, 0, 0.0f, 141, 15, true, 995, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 10, true, 1025, 1025, false}, {22, 141, 0.0f, {-1, 10, 14, 20}, 0, 0.0f, 141, 14, true, 995, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 9, true, 1025, 1025, false}, {22, 141, 0.0f, {-1, 10, 14, 20}, 0, 0.0f, 141, 13, true, 995, 1025, false}}},
	{{{19, 600, 3.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 8, false, 1025, 1025, false}, {22, 141, 1.25f, {-1, 10, 14, 20}, 0, 1.25f, 141, 12, false, 995, 1025, false}}},
	{{{19, 600, 4.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 7, false, 1025, 1025, false}, {22, 141, 2.5f, {-1, 10, 14, 20}, 0, 1.25f, 141, 11, false, 995, 1025, false}}},
	{{{19, 600, 5.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 6, false, 1025, 1025, false}, {22, 141, 3.75f, {-1, 10, 14, 20}, 0, 1.25f, 141, 10, false, 995, 1025, false}}},
	{{{19, 600, 6.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 5, false, 1025, 1025, false}, {22, 141, 5.0f, {-1, 10, 14, 20}, 0, 1.25f, 141, 9, false, 995, 1025, false}}},
	{{{19, 600, 7.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 4, false, 1025, 1025, false}, {22, 141, 6.25f, {-1, 10, 14, 20}, 0, 1.25f, 141, 8, false, 995, 1025, false}}},
	{{{19, 600, 8.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 3, false, 1025, 1025, false}, {22, 141, 7.5f, {-1, 10, 14, 20}, 0, 1.25f, 141, 7, false, 995, 1025, false}}},
	{{{19, 600, 9.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 2, false, 1025, 1025, false}, {22, 141, 8.75f, {-1, 10, 14, 20}, 0, 1.25f, 141, 6, false, 995, 1025, false}}},
	{{{19, 600, 10.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 1, false, 1025, 1025, false}, {22, 141, 10.0f, {-1, 10, 14, 20}, 0, 1.25f, 141, 5, false, 995, 1025, false}}},
	{{{1, 600, 11.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {22, 141, 11.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 4, false, 995, 1025, false}}},
	{{{1, 600, 12.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {22, 141, 12.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 3, false, 995, 1025, false}}},
	{{{1, 600, 13.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {22, 141, 13.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 2, false, 995, 1025, false}}},
	{{{1, 600, 14.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {22, 141, 14.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 1, false, 995, 1025, false}}},
	{{{1, 600, 15.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 141, 15.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 600, 16.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 141, 16.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 600, 17.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 141, 17.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 600, 18.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 141, 18.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 600, 19.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 141, 19.0f, {-1, 10, 14, 20}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 0, 0.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 0.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 0, 1.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 1.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 0, 2.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 2.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 0, 3.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 3.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
	{{{1, 0, 4.0f, {-1, 40, -1, 696}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 4.0f, {-1, 40, -1, 696}, 0, 1.0f, 141, 0, false, 995, 1025, false}}},
};

// The same LP, which P2 guards (Training's Guard All).
const Trace kStandingLpBlocked = {
	{{{1, 0, 14.0f, {-1, 40, -1, 696}, 0, 1.0f, 715, 0, false, 1025, 1025, false}, {1, 0, 524.0f, {-1, 40, -1, 696}, 0, 1.0f, 240, 0, false, 1025, 1025, false}}},
	{{{1, 0, 15.0f, {-1, 40, -1, 696}, 0, 1.0f, 715, 0, false, 1025, 1025, false}, {1, 0, 525.0f, {-1, 40, -1, 696}, 0, 1.0f, 240, 0, false, 1025, 1025, false}}},
	{{{1, 0, 16.0f, {-1, 40, -1, 696}, 0, 1.0f, 715, 0, false, 1025, 1025, false}, {1, 0, 526.0f, {-1, 40, -1, 696}, 0, 1.0f, 240, 0, false, 1025, 1025, false}}},
	{{{1, 0, 17.0f, {-1, 40, -1, 696}, 0, 1.0f, 715, 0, false, 1025, 1025, false}, {1, 0, 527.0f, {-1, 40, -1, 696}, 0, 1.0f, 240, 0, false, 1025, 1025, false}}},
	{{{1, 0, 18.0f, {-1, 40, -1, 696}, 0, 1.0f, 715, 0, false, 1025, 1025, false}, {1, 0, 528.0f, {-1, 40, -1, 696}, 0, 1.0f, 240, 0, false, 1025, 1025, false}}},
	{{{19, 600, 0.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 529.0f, {-1, 40, -1, 696}, 0, 1.0f, 240, 0, false, 1025, 1025, false}}},
	{{{19, 600, 1.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 10, false, 1025, 1025, false}, {12, 56, 0.0f, {-1, -1, -1, 67}, 0, 1.0f, 240, 0, false, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 9, false, 1025, 1025, false}, {12, 56, 1.0f, {-1, -1, -1, 67}, 0, 1.0f, 240, 0, false, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 16, true, 1025, 1025, false}, {23, 91, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 91, 18, true, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 15, true, 1025, 1025, false}, {23, 91, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 91, 17, true, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 14, true, 1025, 1025, false}, {23, 91, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 91, 16, true, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 13, true, 1025, 1025, false}, {23, 91, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 91, 15, true, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 12, true, 1025, 1025, false}, {23, 91, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 91, 14, true, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 11, true, 1025, 1025, false}, {23, 91, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 91, 13, true, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 10, true, 1025, 1025, false}, {23, 91, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 91, 12, true, 1025, 1025, false}}},
	{{{19, 600, 2.0f, {2, 4, 10, 20}, 0, 0.0f, 600, 9, true, 1025, 1025, false}, {23, 91, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 91, 11, true, 1025, 1025, false}}},
	{{{19, 600, 3.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 8, false, 1025, 1025, false}, {23, 91, 1.666656494140625f, {-1, 15, 16, 17}, 0, 1.666656494140625f, 91, 10, false, 1025, 1025, false}}},
	{{{19, 600, 4.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 7, false, 1025, 1025, false}, {23, 91, 3.33331298828125f, {-1, 15, 16, 17}, 0, 1.666656494140625f, 91, 9, false, 1025, 1025, false}}},
	{{{19, 600, 5.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 6, false, 1025, 1025, false}, {23, 91, 4.999969482421875f, {-1, 15, 16, 17}, 0, 1.666656494140625f, 91, 8, false, 1025, 1025, false}}},
	{{{19, 600, 6.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 5, false, 1025, 1025, false}, {23, 91, 6.6666412353515625f, {-1, 15, 16, 17}, 0, 1.6666717529296875f, 91, 7, false, 1025, 1025, false}}},
	{{{19, 600, 7.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 4, false, 1025, 1025, false}, {23, 91, 8.33331298828125f, {-1, 15, 16, 17}, 0, 1.6666717529296875f, 91, 6, false, 1025, 1025, false}}},
	{{{19, 600, 8.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 3, false, 1025, 1025, false}, {23, 91, 9.999984741210938f, {-1, 15, 16, 17}, 0, 1.6666717529296875f, 91, 5, false, 1025, 1025, false}}},
	{{{19, 600, 9.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 2, false, 1025, 1025, false}, {23, 91, 11.666656494140625f, {-1, 15, 16, 17}, 0, 1.6666717529296875f, 91, 4, false, 1025, 1025, false}}},
	{{{19, 600, 10.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 1, false, 1025, 1025, false}, {23, 91, 13.333328247070312f, {-1, 15, 16, 17}, 0, 1.6666717529296875f, 91, 3, false, 1025, 1025, false}}},
	{{{1, 600, 11.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {23, 91, 15.0f, {-1, 15, 16, 17}, 0, 1.6666717529296875f, 91, 2, false, 1025, 1025, false}}},
	{{{1, 600, 12.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {23, 91, 16.0f, {-1, 15, 16, 17}, 0, 1.0f, 91, 1, false, 1025, 1025, false}}},
	{{{1, 600, 13.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 0.0f, {-1, 40, -1, 696}, 0, 1.0f, 91, 0, false, 1025, 1025, false}}},
	{{{1, 600, 14.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 1.0f, {-1, 40, -1, 696}, 0, 1.0f, 91, 0, false, 1025, 1025, false}}},
	{{{1, 600, 15.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 2.0f, {-1, 40, -1, 696}, 0, 1.0f, 91, 0, false, 1025, 1025, false}}},
	{{{1, 600, 16.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 3.0f, {-1, 40, -1, 696}, 0, 1.0f, 91, 0, false, 1025, 1025, false}}},
	{{{1, 600, 17.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 4.0f, {-1, 40, -1, 696}, 0, 1.0f, 91, 0, false, 1025, 1025, false}}},
	{{{1, 600, 18.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 5.0f, {-1, 40, -1, 696}, 0, 1.0f, 91, 0, false, 1025, 1025, false}}},
	{{{1, 600, 19.0f, {2, 4, 10, 20}, 0, 1.0f, 600, 0, false, 1025, 1025, false}, {1, 0, 6.0f, {-1, 40, -1, 696}, 0, 1.0f, 91, 0, false, 1025, 1025, false}}},
};

// P1's LP Hadoken from mid screen, which P2 guards: the script has no attack
// of its own, and the fireball comes out on its fourteenth frame.
const Trace kHadokenBlocked = {
	{{{1, 0, 65.0f, {-1, 40, -1, 696}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 537.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{14, 2, 0.0f, {-1, -1, -1, 7}, 1, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 538.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{14, 2, 1.0f, {-1, -1, -1, 7}, 1, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 539.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{14, 2, 2.0f, {-1, -1, -1, 7}, 1, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 540.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{14, 2, 3.0f, {-1, -1, -1, 7}, 1, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 541.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 0.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 542.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 1.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 44, false, 1025, 1025, false}, {1, 0, 543.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 2.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 43, false, 1025, 1025, false}, {1, 0, 544.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 3.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 42, false, 1025, 1025, false}, {1, 0, 545.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 4.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 41, false, 1025, 1025, false}, {1, 0, 546.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 5.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 40, false, 1025, 1025, false}, {1, 0, 547.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 6.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 39, false, 1025, 1025, false}, {1, 0, 548.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 7.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 38, false, 1025, 1025, false}, {1, 0, 549.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 8.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 37, false, 1025, 1025, false}, {1, 0, 550.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 9.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 36, false, 1025, 1025, false}, {1, 0, 551.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 10.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 35, false, 1025, 1025, false}, {1, 0, 552.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 11.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 34, false, 1025, 1025, false}, {1, 0, 553.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 12.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 33, false, 1025, 1025, false}, {1, 0, 554.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 13.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 32, false, 1025, 1025, true}, {1, 0, 555.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 14.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 31, false, 1025, 1025, false}, {1, 0, 556.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 15.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 30, false, 1025, 1025, false}, {1, 0, 557.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 16.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 29, false, 1025, 1025, false}, {1, 0, 558.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 17.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 28, false, 1025, 1025, false}, {1, 0, 559.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 18.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 27, false, 1025, 1025, false}, {1, 0, 560.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 19.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 26, false, 1025, 1025, false}, {1, 0, 561.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 20.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 25, false, 1025, 1025, false}, {1, 0, 562.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 21.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 24, false, 1025, 1025, false}, {1, 0, 563.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 22.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 23, false, 1025, 1025, false}, {1, 0, 564.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 23.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 22, false, 1025, 1025, false}, {1, 0, 565.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 24.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 21, false, 1025, 1025, false}, {12, 53, 0.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 25.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 20, false, 1025, 1025, false}, {12, 56, 0.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 26.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 19, false, 1025, 1025, false}, {12, 56, 1.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 27.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 18, false, 1025, 1025, false}, {12, 56, 2.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 28.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 17, false, 1025, 1025, false}, {12, 56, 3.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 29.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 16, false, 1025, 1025, false}, {12, 56, 4.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 30.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 15, false, 1025, 1025, false}, {12, 56, 5.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 31.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 14, false, 1025, 1025, false}, {12, 56, 6.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 32.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 13, false, 1025, 1025, false}, {12, 56, 7.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 33.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 12, false, 1025, 1025, false}, {12, 56, 8.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 34.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 11, false, 1025, 1025, false}, {12, 56, 9.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 35.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 10, false, 1025, 1025, false}, {12, 56, 10.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 36.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 9, false, 1025, 1025, false}, {12, 56, 11.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 37.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 8, false, 1025, 1025, false}, {12, 56, 12.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 38.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 7, false, 1025, 1025, false}, {12, 56, 13.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 39.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 6, false, 1025, 1025, false}, {12, 56, 14.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 40.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 5, false, 1025, 1025, false}, {12, 56, 15.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 41.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 4, false, 1025, 1025, false}, {12, 56, 16.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 42.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 3, false, 1025, 1025, false}, {12, 56, 17.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 43.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 2, false, 1025, 1025, false}, {12, 56, 18.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{19, 900, 44.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 1, false, 1025, 1025, false}, {12, 56, 19.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 45.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 20.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 46.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 21.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 47.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 22.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 48.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 23.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 49.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 24.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 50.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 25.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 51.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 26.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 52.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 27.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 53.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 28.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 54.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 29.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 55.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 30.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 56.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 31.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 57.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 32.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 58.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {12, 56, 33.0f, {-1, -1, -1, 67}, 0, 1.0f, 80, 0, false, 1025, 1025, false}}},
	{{{1, 900, 59.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 80, 25, true, 1013, 1025, false}}},
	{{{1, 900, 60.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 80, 24, true, 1013, 1025, false}}},
	{{{1, 900, 61.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 80, 23, true, 1013, 1025, false}}},
	{{{1, 900, 62.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 80, 22, true, 1013, 1025, false}}},
	{{{1, 900, 63.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 80, 21, true, 1013, 1025, false}}},
	{{{1, 900, 64.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 80, 20, true, 1013, 1025, false}}},
	{{{1, 900, 65.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 80, 19, true, 1013, 1025, false}}},
	{{{1, 900, 66.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.0f, {-1, 15, 16, 17}, 0, 0.0f, 80, 18, true, 1013, 1025, false}}},
	{{{1, 900, 67.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 0.9375f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 17, false, 1013, 1025, false}}},
	{{{1, 900, 68.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 1.875f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 16, false, 1013, 1025, false}}},
	{{{1, 900, 69.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 2.8125f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 15, false, 1013, 1025, false}}},
	{{{1, 900, 70.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 3.75f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 14, false, 1013, 1025, false}}},
	{{{1, 900, 71.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 4.6875f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 13, false, 1013, 1025, false}}},
	{{{1, 900, 72.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 5.625f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 12, false, 1013, 1025, false}}},
	{{{1, 900, 73.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 6.5625f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 11, false, 1013, 1025, false}}},
	{{{1, 900, 74.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 7.5f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 10, false, 1013, 1025, false}}},
	{{{1, 900, 75.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 8.4375f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 9, false, 1013, 1025, false}}},
	{{{1, 900, 76.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 9.375f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 8, false, 1013, 1025, false}}},
	{{{1, 900, 77.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 10.3125f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 7, false, 1013, 1025, false}}},
	{{{1, 900, 78.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 11.25f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 6, false, 1013, 1025, false}}},
	{{{1, 900, 79.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 12.1875f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 5, false, 1013, 1025, false}}},
	{{{1, 900, 80.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 13.125f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 4, false, 1013, 1025, false}}},
	{{{1, 900, 81.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 14.0625f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 3, false, 1013, 1025, false}}},
	{{{1, 900, 82.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 15.0f, {-1, 15, 16, 17}, 0, 0.9375f, 80, 2, false, 1013, 1025, false}}},
	{{{1, 900, 83.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {23, 80, 16.0f, {-1, 15, 16, 17}, 0, 1.0f, 80, 1, false, 1013, 1025, false}}},
	{{{1, 900, 84.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 0.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1013, 1025, false}}},
	{{{1, 900, 85.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 1.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1013, 1025, false}}},
	{{{1, 900, 86.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 2.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1013, 1025, false}}},
	{{{1, 900, 87.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 3.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1013, 1025, false}}},
	{{{1, 900, 88.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 4.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1013, 1025, false}}},
	{{{1, 900, 89.0f, {-1, -1, 44, 100}, 0, 1.0f, 900, 0, false, 1025, 1025, false}, {1, 0, 5.0f, {-1, 40, -1, 696}, 0, 1.0f, 80, 0, false, 1013, 1025, false}}},
};

} // namespace

TEST_CASE("every state kind seen in SFV is a status the meter knows")
{
	CHECK(MeterStatus(StateKind::Stand, 0) == ActorStatus::Stand);
	CHECK(MeterStatus(StateKind::Attack, 0) == ActorStatus::Skill);
	CHECK(MeterStatus(StateKind::Guard, 0) == ActorStatus::GuardStand);
	CHECK(MeterStatus(StateKind::Guard, 1) == ActorStatus::GuardCrouch);
	CHECK(MeterStatus(StateKind::Hit, 0) == ActorStatus::Damage);
	CHECK(MeterStatus(StateKind::GuardHit, 1) == ActorStatus::DamageGuard);
	CHECK(MeterStatus(StateKind::LaunchHit, 4) == ActorStatus::DamageBlow);
	CHECK(MeterStatus(StateKind::LyingDown, 3) == ActorStatus::Down);
	CHECK(MeterStatus(StateKind::Held, 4) == ActorStatus::Sequence);
	// Kinds never seen in game are not guessed at.
	for (const int unseen : {0, 10, 11, 13, 20, 21, 28, 30}) CHECK_FALSE(MeterStatus(static_cast<StateKind>(unseen), 0).has_value());
}

TEST_CASE("the game's frames until actionable are the meter's, and its untimed is the meter's")
{
	MeterMove move;
	FighterFrame lying;
	lying.valid = true;
	lying.stateKind = StateKind::Down;
	lying.untilActionable = 1000;
	CHECK(MeterSample(lying, move).untilActionable == training::UntimedFrames);
	lying.stateKind = StateKind::GuardHit;
	lying.untilActionable = 18;
	const auto guarding = MeterSample(lying, move);
	CHECK(guarding.untilActionable == 18);
	CHECK(guarding.basicActionInhibited);
}

TEST_CASE("a script header's attack frames are its startup, active and recovery")
{
	const ScriptHeader standingLp{2, 4, 10, 20};
	CHECK(HeaderPhase(standingLp, 0, false) == 0);
	CHECK(HeaderPhase(standingLp, 1.5f, false) == 0);
	CHECK(HeaderPhase(standingLp, 2, false) == 1);
	CHECK(HeaderPhase(standingLp, 3, false) == 1);
	CHECK(HeaderPhase(standingLp, 4, false) == 3);
	// Before a later script's attack the move has already attacked once.
	CHECK(HeaderPhase(standingLp, 0, true) == 2);
	// No attack: -1, as -1 or a last frame past the end says.
	CHECK(HeaderPhase({-1, -1, 44, 100}, 10, false) == -1);
	CHECK(HeaderPhase({-1, 40, -1, 696}, 10, false) == -1);
	CHECK(HeaderPhase({2, 30, 10, 20}, 3, false) == -1);
}

TEST_CASE("a script with no attack of its own is startup before the move's attack and recovery after")
{
	MeterMove move;
	FighterFrame fireball;
	fireball.valid = true;
	fireball.stateKind = StateKind::Attack;
	fireball.script = 900;
	fireball.startedAction = 900;
	fireball.header = {-1, -1, 44, 100};
	CHECK(MeterSample(fireball, move).hitPhase == 0);
	fireball.projectileOut = true;
	CHECK(MeterSample(fireball, move).hitPhase == 1);
	fireball.projectileOut = false;
	CHECK(MeterSample(fireball, move).hitPhase == 3);
	// The move's follow-up script (a Shoryuken's landing) keeps its move.
	fireball.script = 935;
	CHECK(MeterSample(fireball, move).hitPhase == 3);
	CHECK(MeterSample(fireball, move).commandMove == 900);
	// Another input's move starts over.
	fireball.startedAction = 619;
	CHECK(MeterSample(fireball, move).hitPhase == 0);
	// Out of the attack the move is forgotten.
	fireball.stateKind = StateKind::Stand;
	const auto standing = MeterSample(fireball, move);
	CHECK(standing.hitPhase == -1);
	CHECK(standing.commandMove == 0);
	CHECK(move.action == -1);
}

TEST_CASE("RYU's standing LP measures as its frame data: 3, 2, 7, +4 on hit")
{
	const auto meter = Measure(kStandingLpHit);
	const auto& view = meter.View();
	CHECK(view.startupFrames[0] == 3);
	CHECK(view.moves[0].active == 2);
	CHECK(view.moves[0].recovery == 7);
	REQUIRE(view.lastAdvantage.valid);
	CHECK(view.lastAdvantage.attacker == 0);
	CHECK_FALSE(view.lastAdvantage.blocked);
	CHECK(view.lastAdvantage.frames[0] == 4);
	CHECK(view.lastAdvantage.frames[1] == -4);
}

TEST_CASE("RYU's standing LP is +2 on block")
{
	const auto meter = Measure(kStandingLpBlocked);
	const auto& view = meter.View();
	CHECK(view.startupFrames[0] == 3);
	CHECK(view.moves[0].active == 2);
	CHECK(view.moves[0].recovery == 7);
	REQUIRE(view.lastAdvantage.valid);
	CHECK(view.lastAdvantage.blocked);
	CHECK(view.lastAdvantage.frames[0] == 2);
}

TEST_CASE("RYU's LP Hadoken: 14 frames of startup, 31 of recovery, and the block measured from its fireball")
{
	const auto meter = Measure(kHadokenBlocked);
	const auto& view = meter.View();
	CHECK(view.startupFrames[0] == 14);
	CHECK(view.moves[0].active == 1);
	CHECK(view.moves[0].recovery == 31);
	REQUIRE(view.lastAdvantage.valid);
	CHECK(view.lastAdvantage.blocked);
	// P1 was free 39 frames before P2 left its blockstun (both from the game's
	// own count): the fireball met P2 after P1 had recovered.
	CHECK(view.lastAdvantage.frames[0] == 39);
}

TEST_CASE("an update that ran two ticks leaves a gap the meter starts over after")
{
	training::FrameMeter meter;
	MeterFeed feed;
	// The LP's first half, a tick an update.
	for (std::size_t at = 0; at < 10; ++at) feed.Feed(meter, Frames(kStandingLpHit[at]), 1);
	REQUIRE(meter.View().current[0].action == 600);
	feed.Feed(meter, Frames(kStandingLpHit[10]), 2);
	// Started over on the frame after the gap: nothing before it is kept.
	CHECK(meter.View().frames.size() <= 1);
	CHECK(meter.View().startupFrames[0] == -1);
}

TEST_CASE("a fighter that did not read is unknown, and its move is forgotten")
{
	MeterMove move;
	FighterFrame fireball;
	fireball.valid = true;
	fireball.stateKind = StateKind::Attack;
	fireball.startedAction = 900;
	fireball.header = {-1, -1, 44, 100};
	fireball.projectileOut = true;
	CHECK(MeterSample(fireball, move).hitPhase == 1);
	CHECK(move.attacked);
	const auto unread = MeterSample(FighterFrame{}, move);
	CHECK_FALSE(unread.valid);
	CHECK(move.action == -1);
	CHECK_FALSE(move.attacked);
}
