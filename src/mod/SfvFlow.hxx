#pragma once

// The round flow's part of the rollback state.
//
// SFV's round flow (FlowUnit and its states: Ready, Fight, Finish, ...) lives
// outside the game's snapshot, and a KO moves it: Fight's phase 1 sees the
// round judged and goes to phase 2, which requests Finish; the next tick
// switches. When that KO came from a predicted input, the rollback that
// removes it must move the flow back, or the battle stays stopped.
//
// The flow's objects hold heap closures and shared pointers, so a byte copy
// cannot restore them. FlowRollback keeps a typed record of the flow in its
// own memory, which is part of the layout: Capture fills the record from the
// game before every save and hash; Apply writes a restored record back,
// moving the current state's shared pointer and rebuilding phase delegates
// the way the game builds them (a GameAlloc closure the delegate frees).
//
// Entering or leaving a state may also start presentation (announcer, KO
// banner) that a restore does not take back; the battle state is exact.

#include "core/StateRegions.hxx"

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace sfve::mod {

// The bound phase function of a flow state: what the delegate's closure holds.
struct PhaseClosure {
	const void* function;
	void* state;
	bool operator==(const PhaseClosure&) const = default;
};

struct DelegateRecord {
	const void* vtable;
	const void* destroy;
	const void* copy;
	const void* invoke;
	PhaseClosure closure; // contents of the live closure; zero when inactive
	bool Active() const { return invoke && destroy; }
};

struct SequencerRecord {
	float wait;
	float elapsed;
	int32_t rate;
	int32_t step;
	int32_t enabled;
};

// Every field the record holds is written field by field, so no padding of
// the game's (which holds heap leftovers) or of ours reaches the hash.
struct FlowStateRecord {
	int32_t arg;
	int32_t phase;
	DelegateRecord delegate;
	SequencerRecord sequencer;
	int32_t markedPhase;
	int32_t markCount;
	int32_t zero; // always 0: keeps `own` on an 8-byte word boundary
	// The state class's fields (flowstate::kBaseSize .. its size). Restored but
	// not hashed: until a state first runs, some hold whatever the heap left.
	uint8_t own[40];
};
static_assert(sizeof(FlowStateRecord) == 128, "no padding in the record");

constexpr size_t kMaxFlowStates = 32;

struct FlowRecord {
	int32_t current;
	int32_t requested;
	int32_t requestArg;
	int32_t requestFlag;
	// The state the flow last announced (its state-change message); kept here
	// so a restore puts it back with the rest.
	int32_t announced;
	int32_t stateCount;
	FlowStateRecord states[kMaxFlowStates];
};

class FlowRollback {
public:
	FlowRollback() { record_.announced = -1; }

	// Reads the flow's states. False (and logged) when they do not look like
	// the versus flow this record was written for.
	bool Bind(uint8_t* flowUnit, uintptr_t imageBase);
	void Forget();

	// Adds the record to the layout, its states' own fields masked from the hash.
	void AddTo(StateLayout& layout);

	void Announce(int state) { record_.announced = state; }
	int Announced() const { return record_.announced; }

	void Capture();
	void Apply();

	struct Stats {
		uint64_t stateSwitches = 0;   // restores that moved the current state
		uint64_t delegatesRebuilt = 0;
		uint64_t refusedReleases = 0; // a state with no owner left: not released
	};
	const Stats& GetStats() const { return stats_; }

private:
	struct State {
		uint8_t* object;
		uint8_t* control; // its shared_ptr control block in the flow's list
		size_t kept; // bytes from the start the record keeps (flowstate::kBaseSize or the class size)
	};

	void SwitchCurrent(int index);
	void ApplyDelegate(uint8_t* object, const DelegateRecord& want);

	uint8_t* flow_ = nullptr;
	std::vector<State> states_;
	FlowRecord record_{};
	Stats stats_;
};

} // namespace sfve::mod
