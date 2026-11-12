#pragma once

// The round flow's part of the rollback state.
//
// SFV's round flow (FlowUnit and its states: Ready, Fight, Finish, ...) lives
// outside the game's snapshot, and a KO moves it: Fight's phase 1 sees the
// round judged and goes to phase 2, which requests Finish; the next tick
// switches. When that KO came from a guessed input, the rollback that removes
// it must move the flow back, or the battle stays stopped.
//
// The flow's objects hold heap closures and shared pointers, so a byte copy
// cannot restore them. FlowRollback keeps a typed record of the flow in its
// own memory, part of the layout: Capture fills it from the game, Apply writes
// a restored record back, moving the current state's shared pointer and
// binding phase delegates the way the game binds them (a GameAlloc closure the
// delegate frees). Code addresses are kept as rvas, so the record hashes the
// same in every process and a wrong rebinding shows in the hash.
//
// What the record keeps of each state is fixed by its class
// (flowstate::kClassSizes): the base and its own fields, only the base for a
// class whose own fields hold pointers, and only the base for a class not
// listed. It never changes after Bind, so every copy keeps the same fields.
//
// Bind checks every assumption the writes rest on (the state list, the
// delegate shape, the class sizes); Capture and Apply recheck the ones that
// can change. Once one fails the record stops touching the game and the
// failure is latched (Failure): the adapter reports it to the rollback
// session, which fails, because a rollback could no longer restore the flow.
//
// Entering or leaving a state may also start presentation (announcer, KO
// banner) that a restore does not take back; the battle state is exact.

#include "core/StateRegions.hxx"
#include "mod/GameMemory.hxx"

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace sfve::mod {

// Every field is written field by field, so no padding (the game's holds heap
// leftovers) reaches the hash.
struct FlowStateRecord {
	int32_t arg;
	int32_t phase;
	uint32_t boundPhase; // rva of the phase function its delegate calls; 0 when unbound
	float wait;
	float elapsed;
	int32_t rate;
	int32_t step;
	int32_t enabled;
	int32_t markedPhase;
	int32_t markCount;
	// The state class's own fields (flowstate::kBaseSize .. its size).
	uint8_t own[40];
};
static_assert(sizeof(FlowStateRecord) == 80, "no padding in the record");

constexpr size_t kMaxFlowStates = 32;

struct FlowRecord {
	int32_t current;
	int32_t requested;
	int32_t requestArg;
	uint8_t requestFlag;
	uint8_t zero[3];
	// The state the flow last announced (its state-change message); kept here
	// so a restore puts it back with the rest.
	int32_t announced;
	int32_t stateCount;
	FlowStateRecord states[kMaxFlowStates];
};

class FlowRollback {
public:
	FlowRollback() { record_.announced = -1; }

	// Reads and checks the flow's states. False (logged) when they do not look
	// like the flow this record was written for.
	bool Bind(uint8_t* flowUnit, uintptr_t imageBase);
	void Forget();

	// Adds the record to the layout; heap leftovers in states' own fields are
	// masked from the hash.
	void AddTo(StateLayout& layout);

	void Announce(int state) { record_.announced = state; }
	int Announced() const { return record_.announced; }

	// The flow unit the record mirrors, while it can (null before Bind or once
	// failed): what the decided match's skip asks for the next state, between
	// two updates (SfvAdapter, IntroSkip.hxx).
	uint8_t* Unit() const { return Usable() ? flow_ : nullptr; }

	void Capture();
	void Apply();

	// Why the record can no longer mirror or restore the game since Bind, or
	// null. Latched until the next Bind or Forget.
	const char* Failure() const { return failure_; }

	struct Stats {
		uint64_t stateSwitches = 0;   // restores that moved the current state
		uint64_t delegatesRebound = 0;
		uint64_t keptUses = 0;        // a release that would have been a state's last use
	};
	const Stats& GetStats() const { return stats_; }

private:
	struct State {
		uint8_t* object;
		uint8_t* control; // its shared_ptr control block in the flow's list
		size_t kept;      // bytes from the start the record keeps, by its class (the base, or the class size)
		uint8_t leftoverWords;
	};

	bool Usable() const { return flow_ && !failure_; }
	bool StateListUnchanged();
	bool CaptureDelegate(const State& state, FlowStateRecord& r);
	void ApplyDelegate(const State& state, const FlowStateRecord& r);
	void SwitchCurrent(int index);
	void Fail(const char* why);

	uint8_t* flow_ = nullptr;
	uintptr_t base_ = 0;
	const GameSharedPtr* listBegin_ = nullptr;
	const GameSharedPtr* listEnd_ = nullptr;
	const char* failure_ = nullptr; // a string literal
	std::vector<State> states_;
	FlowRecord record_{};
	Stats stats_;
};

} // namespace sfve::mod
