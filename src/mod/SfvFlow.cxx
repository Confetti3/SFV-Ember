#include "mod/SfvFlow.hxx"

#include "mod/GameHooks.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <stddef.h>
#include <string.h>

#include <iterator>

namespace sfve::mod {

namespace {

namespace fs = sfv::flowstate;
namespace fu = sfv::flowunit;

// The game's delegate (flowstate::kDelegate), as its own code reads it.
struct GameDelegate {
	const void* vtable;
	void (*destroy)(void* closure);
	const void* copy;
	void* closure;
	const void* invoke;
	bool Bound() const { return invoke && destroy; } // the sequencer's test (rva 0x3E3680)
};
static_assert(sizeof(GameDelegate) == 40);

// What the phase binder's closure holds.
struct PhaseClosure {
	uintptr_t function;
	uint8_t* state;
};
static_assert(sizeof(PhaseClosure) == 16);

static_assert(fs::kMaxSize - fs::kBaseSize <= sizeof(FlowStateRecord::own));
static_assert(offsetof(FlowRecord, states) % 8 == 0 && offsetof(FlowStateRecord, own) % 8 == 0 &&
	sizeof(FlowStateRecord) % 8 == 0, "own fields on 8-byte words, as the hash masks them");

// Every field the record keeps, game side and record side, so Capture and
// Apply cannot list different fields.
template <typename F>
void VisitStateFields(uint8_t* state, FlowStateRecord& r, F&& f)
{
	f(Field<int32_t>(state, fs::kArg), r.arg);
	f(Field<int32_t>(state, fs::kPhase), r.phase);
	f(Field<float>(state, fs::kWait), r.wait);
	f(Field<float>(state, fs::kElapsed), r.elapsed);
	f(Field<int32_t>(state, fs::kRate), r.rate);
	f(Field<int32_t>(state, fs::kStep), r.step);
	f(Field<int32_t>(state, fs::kEnabled), r.enabled);
	f(Field<int32_t>(state, fs::kMarkedPhase), r.markedPhase);
	f(Field<int32_t>(state, fs::kMarkCount), r.markCount);
}

template <typename F>
void VisitUnitFields(uint8_t* flow, FlowRecord& r, F&& f)
{
	f(Field<int32_t>(flow, fu::kCurrentIndex), r.current);
	f(Field<int32_t>(flow, fu::kRequestedIndex), r.requested);
	f(Field<int32_t>(flow, fu::kRequestArg), r.requestArg);
	f(Field<uint8_t>(flow, fu::kRequestFlag), r.requestFlag);
}

constexpr auto kToRecord = [](auto& game, auto& record) { record = game; };
constexpr auto kToGame = [](auto& game, auto& record) { game = record; };

constexpr bool EachClassOnce()
{
	for (size_t i = 0; i < std::size(fs::kClassSizes); i++) {
		for (size_t j = i + 1; j < std::size(fs::kClassSizes); j++) {
			if (fs::kClassSizes[i].vtable == fs::kClassSizes[j].vtable) return false;
		}
	}
	return true;
}
static_assert(EachClassOnce(), "a class listed twice: only the first entry would be used");

const fs::ClassSize* FindClass(uintptr_t vtableRva)
{
	for (const fs::ClassSize& c : fs::kClassSizes) {
		if (c.vtable == vtableRva) return &c;
	}
	return nullptr;
}

} // namespace

bool FlowRollback::Bind(uint8_t* flowUnit, uintptr_t imageBase)
{
	Forget();
	base_ = imageBase;
	const auto* begin = Field<const GameSharedPtr*>(flowUnit, fu::kStatesBegin);
	const auto* end = Field<const GameSharedPtr*>(flowUnit, fu::kStatesEnd);
	if (!begin || end <= begin || size_t(end - begin) > kMaxFlowStates) {
		Log("flow: %p has no state list (%p..%p)", (void*)flowUnit, (const void*)begin, (const void*)end);
		return false;
	}
	for (const GameSharedPtr* s = begin; s < end; s++) {
		if (!s->object || !s->control) {
			Log("flow: state %zu is empty", size_t(s - begin));
			states_.clear();
			return false;
		}
		const uintptr_t vtable = Field<uintptr_t>(s->object, 0) - imageBase;
		const fs::ClassSize* known = FindClass(vtable);
		if (!known) {
			Log("flow: state %zu has an unknown class (vtable rva 0x%llx); only its base is kept",
				size_t(s - begin), (unsigned long long)vtable);
		}
		const size_t kept = known && known->keepOwn ? known->size : fs::kBaseSize;
		states_.push_back({s->object, s->control, kept, known ? known->leftoverWords : uint8_t(0)});
	}
	flow_ = flowUnit;
	listBegin_ = begin;
	listEnd_ = end;
	record_.stateCount = (int32_t)states_.size();
	Capture();
	if (failure_) {
		Forget(); // not bound: there is nothing a session could restore yet
		return false;
	}
	Log("flow: bound %p with %zu states (current %d)", (void*)flow_, states_.size(), record_.current);
	return true;
}

void FlowRollback::Forget()
{
	flow_ = nullptr;
	listBegin_ = listEnd_ = nullptr;
	failure_ = nullptr;
	states_.clear();
	const int32_t announced = record_.announced;
	record_ = {};
	record_.announced = announced;
}

void FlowRollback::AddTo(StateLayout& layout)
{
	const size_t region = layout.Add("flow", &record_, sizeof(record_));
	for (size_t i = 0; i < states_.size(); i++) {
		const size_t own = offsetof(FlowRecord, states) + i * sizeof(FlowStateRecord) + offsetof(FlowStateRecord, own);
		for (size_t k = 0; k < sizeof(FlowStateRecord::own) / 8; k++) {
			if (states_[i].leftoverWords & (1u << k)) layout.IgnoreWord(region, own + 8 * k);
		}
	}
}

void FlowRollback::Capture()
{
	if (!Usable()) return;
	if (!StateListUnchanged()) return;
	VisitUnitFields(flow_, record_, kToRecord);
	for (size_t i = 0; i < states_.size(); i++) {
		const State& s = states_[i];
		FlowStateRecord& r = record_.states[i];
		VisitStateFields(s.object, r, kToRecord);
		if (!CaptureDelegate(s, r)) return;
		memcpy(r.own, s.object + fs::kBaseSize, s.kept - fs::kBaseSize);
	}
}

void FlowRollback::Apply()
{
	if (!Usable()) return;
	if (!StateListUnchanged()) return;
	for (size_t i = 0; i < states_.size(); i++) {
		const State& s = states_[i];
		FlowStateRecord& r = record_.states[i];
		VisitStateFields(s.object, r, kToGame);
		ApplyDelegate(s, r);
		if (failure_) return;
		memcpy(s.object + fs::kBaseSize, r.own, s.kept - fs::kBaseSize);
	}
	SwitchCurrent(record_.current);
	VisitUnitFields(flow_, record_, kToGame);
}

// The flow's state list is fixed once the battle has built it; the record
// only ever addresses the states it saw at Bind.
bool FlowRollback::StateListUnchanged()
{
	if (Field<const GameSharedPtr*>(flow_, fu::kStatesBegin) == listBegin_ &&
		Field<const GameSharedPtr*>(flow_, fu::kStatesEnd) == listEnd_) {
		return true;
	}
	Fail("the round flow's state list changed");
	return false;
}

// Records which phase function a bound delegate calls, after checking it is
// the phase binder's delegate: the only shape Apply can rebuild.
bool FlowRollback::CaptureDelegate(const State& state, FlowStateRecord& r)
{
	const GameDelegate& d = Field<GameDelegate>(state.object, fs::kDelegate);
	if (!d.Bound()) {
		r.boundPhase = 0;
		return true;
	}
	const auto rva = [&](const void* p) { return reinterpret_cast<uintptr_t>(p) - base_; };
	const auto* closure = static_cast<const PhaseClosure*>(d.closure);
	if (rva(d.vtable) != fs::kPhaseDelegateVtable || rva(reinterpret_cast<const void*>(d.destroy)) != fs::kPhaseDelegateDestroy ||
		rva(d.copy) != fs::kPhaseDelegateCopy || rva(d.invoke) != fs::kPhaseDelegateInvoke || !closure ||
		closure->state != state.object || closure->function <= base_) {
		Fail("a round flow state's delegate is not a phase binding");
		return false;
	}
	r.boundPhase = (uint32_t)(closure->function - base_);
	return true;
}

// Leaves the state's delegate bound to the recorded phase function. When the
// live one already is, nothing changes; otherwise the live closure is freed
// the way a state's Exit resets its delegate (rva 0x414DD0) and a new one is
// built the way the phase binder builds one (rva 0x477FE0), without running
// the phase (the binder's assignment would).
void FlowRollback::ApplyDelegate(const State& state, const FlowStateRecord& r)
{
	GameDelegate& live = Field<GameDelegate>(state.object, fs::kDelegate);
	const uint32_t liveRva =
		live.Bound() ? (uint32_t)(static_cast<const PhaseClosure*>(live.closure)->function - base_) : 0;
	if (liveRva == r.boundPhase) return;
	if (live.Bound()) {
		live.destroy(live.closure);
		live.invoke = nullptr;
		live.destroy = nullptr;
		live.closure = nullptr;
	}
	if (r.boundPhase) {
		auto* closure = static_cast<PhaseClosure*>(GameAlloc(sizeof(PhaseClosure)));
		if (!closure) {
			Fail("the game's allocator returned nothing for a round flow delegate");
			return;
		}
		*closure = {base_ + r.boundPhase, state.object};
		live.vtable = reinterpret_cast<const void*>(base_ + fs::kPhaseDelegateVtable);
		live.copy = reinterpret_cast<const void*>(base_ + fs::kPhaseDelegateCopy);
		live.closure = closure;
		live.invoke = reinterpret_cast<const void*>(base_ + fs::kPhaseDelegateInvoke);
		live.destroy = reinterpret_cast<void (*)(void*)>(base_ + fs::kPhaseDelegateDestroy);
	}
	stats_.delegatesRebound++;
}

// Points the flow's current-state shared pointer at state `index`, keeping
// the use counts right. The flow's list owns every state, so a release is
// never a state's last use; a count that says otherwise is left alone.
void FlowRollback::SwitchCurrent(int index)
{
	GameSharedPtr& current = Field<GameSharedPtr>(flow_, fu::kCurrent);
	const State* want = index >= 0 && size_t(index) < states_.size() ? &states_[index] : nullptr;
	const GameSharedPtr next = want ? GameSharedPtr{want->object, want->control} : GameSharedPtr{};
	if (current.object == next.object) return;
	if (next.control) AddRef(next.control);
	const GameSharedPtr released = current;
	current = next;
	if (released.control && !ReleaseKeepingAlive(released.control)) stats_.keptUses++;
	stats_.stateSwitches++;
}

void FlowRollback::Fail(const char* why)
{
	if (failure_) return;
	failure_ = why;
	Log("flow: %s; the round flow can no longer be restored", why);
}

} // namespace sfve::mod
