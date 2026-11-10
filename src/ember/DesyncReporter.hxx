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

} // namespace sfve::adapter
