#pragma once

// The game's side of a desync's report (EMBER_LIBRARY.md section 51;
// DesyncReport.hxx has what a report holds). The game process cannot send it:
// the mod's network guard keeps the whole process off every host but
// loopback and the session's peers. So the report is written down here, as
// one file in the product's settings folder (desync::kSpoolFolder), and the
// launcher, which waits on the game outside the guard, sends it
// (launcher/DesyncCourier).
//
// ReportDesync runs on the game thread as the check finds the battle's first
// mismatch (SfvVerifier::Step), so at most once per battle: it gathers the
// facts the game holds there (the session's region hashes, the room's match,
// both picks) and hands them to a thread of the report's own, which does
// everything that can wait on a disk: it asks whether Send problem reports
// is on (nothing is written when it is off, and past desync::kSpoolLimit
// reports waiting the oldest go to make room), hashes the mod for the build
// id, reads the tail of the mod's log and writes the file. Nothing there can stall or end the game:
// every failure is a log line and no report.
//
// What it gathers beyond the checkpoint's hashes (DesyncReport.hxx, Facts):
// the match key both fighters share, the previous checkpoint that matched,
// the rollbacks of the last 120 frames and both players' inputs for the 180
// before the checkpoint (DesyncTrace.hxx), the socket-read counters, and,
// on the report's thread, each fighter's script and frame read from the kept
// state and the state file's pointer census (a pass over the state's
// address-like words, VirtualQuery and ReadProcessMemory, after the mismatch
// and never in the fight). The launcher sends the report within seconds
// (DesyncCourier.hxx), with the state file beside it when the intake takes one.

#include "core/StartAgreement.hxx"
#include "ember/SfvVerification.hxx"

namespace sf4e {
class SessionClient;
}

namespace sfve::adapter {

// Game thread. `client` is the room client that found the mismatch;
// `player`: this copy fights (a watcher's mismatch is reported too, as a
// watcher's).
void ReportDesync(const RollbackSession& session, const sf4e::SessionClient& client,
	const CheckpointExchange::Mismatch& mismatch, bool player);

// Game thread, once a room battle's start state agreement wrote some of P1's
// bytes into this copy's state (core/StartAgreement.hxx): the same kind of
// report, titled as a start state repair, with this copy's state before the
// repair as its state file (frame 0). `layout` is the battle's; `delay` the
// session's input delay.
void ReportStartRepair(const StartAgreement::Report& report, const StateLayout& layout, bool watcher, int delay);

} // namespace sfve::adapter
