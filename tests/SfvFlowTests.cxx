// The round flow's record (mod/SfvFlow) over a flow laid out in memory the
// way SFV lays one out, without the game: what each state keeps is fixed by
// its class, and a broken assumption latches a failure, which the adapter
// reports to the rollback session (GameAdapter::StateFailure).

#include <doctest/doctest.h>

#include "mod/GameMemory.hxx"
#include "mod/SfvFlow.hxx"
#include "mod/SfvOffsets.hxx"

#include <stdint.h>

using namespace sfve::mod;

namespace {

namespace fs = sfve::mod::sfv::flowstate;
namespace fu = sfve::mod::sfv::flowunit;

constexpr uintptr_t kBase = 0x140000000; // the image base the vtables are read against
constexpr uint32_t kPlainClass = 0x2C6A390;   // 152 bytes: its 8 own bytes are kept
constexpr uint32_t kPointerClass = 0x2C6A668; // Finish, 168 bytes: own fields hold pointers, only the base is kept
constexpr uint32_t kUnlistedClass = 0x1234560; // in no table: only the base is kept
constexpr int kStates = 3;

// A FlowUnit and its states as SFV lays them out: the state list (a vector of
// shared pointers), the current state's shared pointer and index, and each
// state's vtable. Every delegate is unbound, so a restore never allocates.
// It points into itself, so it is never copied.
struct FakeFlow {
	alignas(16) uint8_t unit[512] = {};
	alignas(16) uint8_t states[kStates][fs::kMaxSize] = {};
	alignas(16) uint8_t controls[kStates][16] = {};
	GameSharedPtr list[kStates] = {};

	FakeFlow()
	{
		const uint32_t classes[kStates] = {kPlainClass, kPointerClass, kUnlistedClass};
		for (int i = 0; i < kStates; i++) {
			Field<uintptr_t>(states[i], 0) = kBase + classes[i];
			Field<int32_t>(controls[i], kControlUses) = 2; // the list's use and the current pointer's
			list[i] = {states[i], controls[i]};
		}
		Field<const GameSharedPtr*>(unit, fu::kStatesBegin) = list;
		Field<const GameSharedPtr*>(unit, fu::kStatesEnd) = list + kStates;
		Field<GameSharedPtr>(unit, fu::kCurrent) = list[0];
	}
	FakeFlow(const FakeFlow&) = delete;
	FakeFlow& operator=(const FakeFlow&) = delete;

	uint64_t& Own(int state) { return Field<uint64_t>(states[state], fs::kBaseSize); }
	int32_t& Phase(int state) { return Field<int32_t>(states[state], fs::kPhase); }
};

} // namespace

TEST_CASE("what a flow state keeps is fixed by its class, whatever its fields hold")
{
	FakeFlow flow;
	// An address-like value in a class kept whole: it was once enough to stop
	// keeping that state's own fields, differently in each copy.
	const uint64_t addressLike = 0x00000204F5F982A0ull;
	REQUIRE(LooksLikeAddress(addressLike));
	flow.Own(0) = addressLike;
	flow.Own(1) = 1;
	flow.Own(2) = 2;
	FlowRollback record;
	REQUIRE(record.Bind(flow.unit, kBase));

	flow.Own(0) = 7;
	flow.Own(1) = 8;
	flow.Own(2) = 9;
	record.Apply();
	CHECK(record.Failure() == nullptr);
	CHECK(flow.Own(0) == addressLike); // a plain class's own fields are restored as recorded
	CHECK(flow.Own(1) == 8);           // a class whose own fields hold pointers keeps only its base
	CHECK(flow.Own(2) == 9);           // so does a class not listed
}

TEST_CASE("a flow state list that changes after Bind latches a failure, and the record stops touching the game")
{
	FakeFlow flow;
	FlowRollback record;
	REQUIRE(record.Bind(flow.unit, kBase));
	REQUIRE(record.Failure() == nullptr);
	flow.Phase(0) = 5;
	record.Apply(); // a restore puts the recorded phase back
	CHECK(flow.Phase(0) == 0);

	Field<const GameSharedPtr*>(flow.unit, fu::kStatesEnd) = flow.list + kStates - 1;
	record.Capture();
	REQUIRE(record.Failure() != nullptr);

	// Latched: with the list as it was, the record still writes nothing.
	Field<const GameSharedPtr*>(flow.unit, fu::kStatesEnd) = flow.list + kStates;
	flow.Phase(0) = 5;
	record.Capture();
	record.Apply();
	CHECK(flow.Phase(0) == 5);
	CHECK(record.Failure() != nullptr);

	// A new battle's Bind starts again.
	CHECK(record.Bind(flow.unit, kBase));
	CHECK(record.Failure() == nullptr);
}

TEST_CASE("a flow whose delegate is not a phase binding is not bound, and leaves no failure behind")
{
	FakeFlow flow;
	// Bound (destroy and invoke set) but not the phase binder's functions.
	Field<uintptr_t>(flow.states[1], fs::kDelegate + 8) = kBase + 0x10;
	Field<uintptr_t>(flow.states[1], fs::kDelegate + 32) = kBase + 0x20;
	FlowRollback record;
	CHECK_FALSE(record.Bind(flow.unit, kBase));
	CHECK(record.Failure() == nullptr);
}
