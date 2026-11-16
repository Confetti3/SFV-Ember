#pragma once

// NetplayMode's pipe commands for tests and cross-copy desync checks, and
// what they do to the battle (player builds refuse them at the pipe,
// ControlPipe.cxx; compiling them only into development builds is S9's):
//
// - "hash <frame>", "checkpoint [frame]" (latest confirmed per-region
//   checkpoint), "dump <frame>" (that frame's saved state to a file, once it
//   is confirmed) and "freeze <frame>" (hold the battle at that frame so both
//   screens can be compared);
// - "perturb <frame> <region name or size> <offset> <add>" adds to a 32-bit
//   value of this copy's state once the session has played that frame: a real
//   desync, for checking that the peers' hashes then differ;
// - "skipafter <n>" (-1 off): this copy presses an attack n frames into each
//   decided match's scenes (IntroSkip.hxx);
// - "vital <P1 percent> <P2 percent>" (0 or 100: as the round gives it): each
//   fighter's vital set to that share of its maximum on the first frame of
//   every round's fight, for runs with no rollback (docs/design/beta9/skip.md
//   section 2);
// - "riftgain N": the pacer's rift gain as N/120 (FramePacer).
//
// And -sfve_start_perturb (PerturbStart): one dword of this copy's start
// state changed before the start state's agreement, which then repairs it.
//
// Every member is read and written under the mode's mutex (the commands take
// it; the battle's frame holds it).

#include "core/RollbackSession.hxx"
#include "mod/FramePacer.hxx"
#include "mod/SfvAdapter.hxx"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sfve::mod {

class NetplayDevCommands {
public:
	// The commands, over the mode's `mutex`, its battle's `session` (null
	// between battles) and its `pacer`; all three outlive the commands.
	void Register(std::mutex& mutex, const std::unique_ptr<RollbackSession>& session, FramePacer& pacer);

	// "freeze": the battle stands still from that frame on.
	bool Frozen(const RollbackSession& session) const { return freezeFrame_ >= 0 && session.Frame() >= freezeFrame_; }
	// "skipafter" (tests, runroom's -skipAfter): the script presses an attack
	// this many frames into each decided match's scenes; -1 never.
	int SkipAfter() const { return skipAfter_; }
	// "dump": the asked frame's state written to the log folder (as
	// state-<instance>-<frame>.bin) once it is confirmed.
	void DumpIfRequested(RollbackSession* session, int instance);
	// "perturb": the change made once the session has played its frame.
	void PerturbIfDue(RollbackSession* session);
	// "vital": the first frame of a round's fight gives the fighters their shares.
	void GiveVital(SfvAdapter& adapter, int frame);

	// -sfve_start_perturb's "<region>,<offset>,<add>" applied to `adapter`'s
	// start state before frame 0 (development).
	static void PerturbStart(SfvAdapter& adapter, const std::string& spec);

private:
	int skipAfter_ = -1;
	int vitalPercent_[2] = {0, 0}; // "vital": each side's share of its maximum at a fight's start; 0 none
	int dumpFrame_ = -1;           // "dump <frame>": write that frame's saved state to the log folder
	std::vector<uint8_t> dumpBytes_; // its latest save, until it is confirmed
	int freezeFrame_ = -1;         // "freeze <frame>": stop advancing there (to compare screens)
	struct Perturbation {
		int frame = -1; // none pending
		std::string region;
		size_t offset = 0;
		int32_t add = 0;
	};
	Perturbation perturb_;
};

} // namespace sfve::mod
