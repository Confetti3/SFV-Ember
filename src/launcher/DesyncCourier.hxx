#pragma once

// The launcher's side of a desync's report (EMBER_LIBRARY.md section 51): it
// sends the reports the game wrote down (ember/DesyncReporter.hxx) to the
// report intake, as the launcher sends a crash's report without asking. The
// game process cannot: the mod's network guard keeps it off every host but
// loopback and its peers. The launcher waits on the game outside the guard.
//
// One thread, from the first start of the game for the rest of the
// launcher's life: a pass over the waiting reports every 5 seconds and
// whenever the game ends, so a report is on its way within seconds of the
// fight that ended on its desync, not at the next launcher start. A pass
// that finds nothing is a directory listing. Each pass:
//   - reports of another build than the installed one (the sfv_ember.dll next
//     to the launcher) or older than a week are deleted unsent: they cannot
//     be acted on (desync::PlanSends);
//   - the rest go newest first, one at a time, each held open by this
//     launcher alone (the file is held, unshared, until it is settled, so a
//     second launcher of a test's copies skips it);
//   - nothing is sent past the caps (desync::kSendsPerDay in 24 hours,
//     desync::kSendsPerTenMinutes in ten minutes, the intake's own pace):
//     the reports wait, and the courier says so once in the launcher log.
//     They are counted in their own record (desync::kSentFile), not the crash
//     reports' (sent-reports.json), whose three a day neither hold desync
//     reports back nor are used up by them;
//   - a report damaged is dropped unread, and one is deleted unsent when Send
//     problem reports is off (the same setting as a crash's report);
//   - its log's tail is redacted as a crash report's logs are (the player's
//     Windows account, PC and player names, addresses, invitations, tokens),
//     and left out when those names cannot be read; its details (the match
//     key, scripts, rollbacks, inputs) carry none of them;
//   - the report's state file (desync-states, deflated, about 15 KB) goes with
//     it as a part named "state", past the same switch; an intake that does
//     not take the part yet is sent the report once more without it, and the
//     courier stops offering it for the rest of its life. An intake that does
//     not take sfv_ember.log yet is handled alike;
//   - deleted once the intake has it or refused it for good; kept for a later
//     pass, at most five tries in one launcher's life, when no answer came or
//     the intake was busy, with a pause of two minutes after each.
// Closing the launcher cancels a send under way; its report waits for the
// next launcher.

namespace sfve::launcher {

// The courier's thread, started once; a later call asks for a pass now.
void StartDesyncCourier();
// A pass now (the game ended); nothing before StartDesyncCourier.
void WakeDesyncCourier();

} // namespace sfve::launcher
