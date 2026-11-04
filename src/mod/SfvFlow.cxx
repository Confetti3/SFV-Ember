#include "mod/SfvFlow.hxx"

#include "mod/GameHooks.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <string.h>

namespace sfve::mod {

namespace {

// The game's delegate (flowstate::kDelegate), as its own code reads it.
struct GameDelegate {
	const void* vtable;
	void (*destroy)(void* closure);
	const void* copy;
	void* closure;
	const void* invoke;
	bool Active() const { return invoke && destroy; } // the sequencer's test (rva 0x3E3680)
};
static_assert(sizeof(GameDelegate) == 40);

struct SharedPtr {
	uint8_t* object;
	uint8_t* control;
};

template <typename T>
T& Field(uint8_t* object, uint32_t offset)
{
	return *reinterpret_cast<T*>(object + offset);
}

size_t ClassSize(uintptr_t vtableRva)
{
	for (const sfv::flowstate::ClassSize& c : sfv::flowstate::kClassSizes) {
		if (c.vtable == vtableRva) return c.size;
	}
	return 0;
}

} // namespace

static_assert(sfv::flowstate::kBaseSize - sfv::flowstate::kSequencer == sizeof(FlowStateRecord::sequencer));
static_assert(sfv::flowstate::kMaxSize - sfv::flowstate::kBaseSize <= sizeof(FlowStateRecord::own));

bool FlowRollback::Bind(uint8_t* flowUnit, uintptr_t imageBase)
{
	Forget();
	const auto* begin = Field<SharedPtr*>(flowUnit, sfv::flowunit::kStatesBegin);
	const auto* end = Field<SharedPtr*>(flowUnit, sfv::flowunit::kStatesEnd);
	if (!begin || end <= begin || size_t(end - begin) > kMaxFlowStates) {
		Log("flow: %p has no state list (%p..%p)", (void*)flowUnit, (void*)begin, (void*)end);
		return false;
	}
	for (const SharedPtr* s = begin; s < end; s++) {
		if (!s->object || !s->control) {
			Log("flow: state %zu is empty", size_t(s - begin));
			states_.clear();
			return false;
		}
		const uintptr_t vtable = Field<uintptr_t>(s->object, 0) - imageBase;
		size_t size = ClassSize(vtable);
		if (!size) {
			Log("flow: state %zu has an unknown class (vtable rva 0x%llx); its own fields are not kept",
				size_t(s - begin), (unsigned long long)vtable);
			size = sfv::flowstate::kBaseSize;
		}
		states_.push_back({s->object, s->control, size});
	}
	flow_ = flowUnit;
	record_.stateCount = (int32_t)states_.size();
	Capture();
	Log("flow: bound %p with %zu states (current %d)", (void*)flow_, states_.size(), record_.current);
	return true;
}

void FlowRollback::Forget()
{
	flow_ = nullptr;
	states_.clear();
	const int32_t announced = record_.announced;
	record_ = {};
	record_.announced = announced;
}

void FlowRollback::Capture()
{
	if (!flow_) return;
	record_.current = Field<int32_t>(flow_, sfv::flowunit::kCurrentIndex);
	record_.requested = Field<int32_t>(flow_, sfv::flowunit::kRequestedIndex);
	record_.requestArg = Field<int32_t>(flow_, sfv::flowunit::kRequestArg);
	record_.requestFlag = Field<uint8_t>(flow_, sfv::flowunit::kRequestFlag);
	for (size_t i = 0; i < states_.size(); i++) {
		uint8_t* object = states_[i].object;
		FlowStateRecord& r = record_.states[i];
		r.arg = Field<int32_t>(object, sfv::flowstate::kArg);
		r.phase = Field<int32_t>(object, sfv::flowstate::kPhase);
		const GameDelegate& d = Field<GameDelegate>(object, sfv::flowstate::kDelegate);
		r.delegate = {d.vtable, reinterpret_cast<const void*>(d.destroy), d.copy, d.invoke, {}};
		// An inactive delegate's closure was freed; only an active one is read.
		if (d.Active() && d.closure) r.delegate.closure = *static_cast<const PhaseClosure*>(d.closure);
		memcpy(r.sequencer, object + sfv::flowstate::kSequencer, sizeof(r.sequencer));
		memcpy(r.own, object + sfv::flowstate::kBaseSize, states_[i].size - sfv::flowstate::kBaseSize);
	}
}

void FlowRollback::Apply()
{
	if (!flow_) return;
	for (size_t i = 0; i < states_.size(); i++) {
		uint8_t* object = states_[i].object;
		const FlowStateRecord& r = record_.states[i];
		Field<int32_t>(object, sfv::flowstate::kArg) = r.arg;
		Field<int32_t>(object, sfv::flowstate::kPhase) = r.phase;
		ApplyDelegate(object, r.delegate);
		memcpy(object + sfv::flowstate::kSequencer, r.sequencer, sizeof(r.sequencer));
		memcpy(object + sfv::flowstate::kBaseSize, r.own, states_[i].size - sfv::flowstate::kBaseSize);
	}
	SwitchCurrent(record_.current);
	Field<int32_t>(flow_, sfv::flowunit::kCurrentIndex) = record_.current;
	Field<int32_t>(flow_, sfv::flowunit::kRequestedIndex) = record_.requested;
	Field<int32_t>(flow_, sfv::flowunit::kRequestArg) = record_.requestArg;
	Field<uint8_t>(flow_, sfv::flowunit::kRequestFlag) = (uint8_t)record_.requestFlag;
}

// Leaves the state's delegate bound to the recorded phase function. When the
// live one already is, nothing changes; otherwise the live closure is freed
// the way the game resets a delegate (a state's Exit, rva 0x414DD0) and a
// new one is built the way the game binds a phase (rva 0x477FE0).
void FlowRollback::ApplyDelegate(uint8_t* object, const DelegateRecord& want)
{
	GameDelegate& live = Field<GameDelegate>(object, sfv::flowstate::kDelegate);
	const bool same = live.Active() == want.Active() &&
		(!want.Active() || (live.closure && *static_cast<const PhaseClosure*>(live.closure) == want.closure &&
							   live.invoke == want.invoke && reinterpret_cast<const void*>(live.destroy) == want.destroy));
	if (same) return;
	if (live.Active()) {
		live.destroy(live.closure);
		live.invoke = nullptr;
		live.destroy = nullptr;
	}
	if (want.Active()) {
		void* closure = GameAlloc(sizeof(PhaseClosure));
		memcpy(closure, &want.closure, sizeof(PhaseClosure));
		live.vtable = want.vtable;
		live.copy = want.copy;
		live.closure = closure;
		live.invoke = want.invoke;
		live.destroy = reinterpret_cast<void (*)(void*)>(const_cast<void*>(want.destroy));
	}
	stats_.delegatesRebuilt++;
}

// Points the flow's current-state shared pointer at state `index`, keeping
// the use counts right. The flow's list owns every state, so a release never
// frees one; a count that says otherwise is left alone.
void FlowRollback::SwitchCurrent(int index)
{
	SharedPtr& current = Field<SharedPtr>(flow_, sfv::flowunit::kCurrent);
	const State* want = index >= 0 && size_t(index) < states_.size() ? &states_[index] : nullptr;
	uint8_t* object = want ? want->object : nullptr;
	if (current.object == object) return;
	uint8_t* control = want ? want->control : nullptr;
	if (control) InterlockedIncrement(&Field<volatile LONG>(control, sfv::sharedctl::kUses));
	uint8_t* released = current.control;
	current = {object, control};
	if (released) {
		volatile LONG& uses = Field<volatile LONG>(released, sfv::sharedctl::kUses);
		if (uses > 1) {
			InterlockedDecrement(&uses);
		} else {
			stats_.refusedReleases++;
		}
	}
	stats_.stateSwitches++;
}

} // namespace sfve::mod
