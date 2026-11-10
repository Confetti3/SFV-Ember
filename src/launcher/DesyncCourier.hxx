#pragma once

// The launcher's side of a desync's report (EMBER_LIBRARY.md section 51):
// it sends the reports the game wrote down (ember/DesyncReporter.hxx) to the
// report intake, as the launcher sends a crash's report without asking. The
// game process cannot: the mod's network guard keeps it off every host but
// loopback and its peers. The launcher waits on the game outside the guard.
//
// One thread, from the first start of the game for the rest of the
// launcher's life: a pass over the waiting reports now, every 30 seconds and
// whenever the game ends. Each report, oldest first:
//   - taken by this launcher alone (the file is held open, unshared, until
//     it is settled, so a second launcher of a test's copies skips it);
//   - dropped unread when it is damaged or older than a week, and unsent
//     when Send problem reports is off (the same setting, and the same
//     record of sent reports with its daily limit, as a crash's report);
//   - its log's tail redacted as a crash report's logs are (the player's
//     Windows account, PC and player names, addresses, invitations, tokens),
//     and left out when those names cannot be read;
//   - sent once with sfv_ember.log; when the intake does not take that log
//     yet (an intake from before section 51) sent once more without it, the
//     same counted send;
//   - deleted once the intake has it or refused it for good; kept for a
//     later pass, at most two tries in one launcher's life, when no answer
//     came, the intake was busy or the day's reports are used up.
// Closing the launcher cancels a send under way; its report waits for the
// next launcher.

namespace sfve::launcher {

// The courier's thread, started once; a later call asks for a pass now.
void StartDesyncCourier();
// A pass now (the game ended); nothing before StartDesyncCourier.
void WakeDesyncCourier();

} // namespace sfve::launcher
