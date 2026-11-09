#include "mod/FighterReader.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

namespace sfve::mod {

namespace {

namespace fighter = sfv::fighter;

// Projectile units a battle can hold, past any SFV fighter's (sixteen seen).
constexpr uint32_t kMaxProjectiles = 64;

float Fixed(int32_t value)
{
	return static_cast<float>(value) / 65536.0f;
}

// A field of a game object, read only if the memory is readable.
template <typename T>
bool ReadField(const uint8_t* object, uint32_t offset, T* out)
{
	return object && TryRead(object + offset, out);
}

} // namespace

bool FighterReader::Bind(uint8_t* anyUnit, uintptr_t imageBase)
{
	Forget();
	for (uint32_t index = 0; index < 2; index++) {
		uint8_t* unit = GameFindUnit(anyUnit, sfv::unit::kCharaUnitType, index);
		int32_t side = -1;
		if (!unit || !ReadField(unit, fighter::kSide, &side) || side < 0 || side > 1 || fighters_[side].unit) {
			return Log("frame meter: fighter %u has no side of its own (%d)", index, side), false;
		}
		Fighter& found = fighters_[side];
		found.unit = unit;
		found.actionPart = GameFindPart(unit, fighter::kActionPartType);
		found.statusPart = GameFindPart(unit, fighter::kStatusPartType);
		if (!found.actionPart || !found.statusPart) return Log("frame meter: P%d has no action or status part", side + 1), false;
	}
	for (uint32_t index = 0; index < kMaxProjectiles; index++) {
		uint8_t* unit = GameFindUnit(anyUnit, fighter::kProjectileUnitType, index);
		// One already out when the meter starts came out before it.
		int32_t live = 0;
		if (unit && ReadField(unit, fighter::kProjectileLive, &live)) projectiles_.push_back({unit, live != 0});
	}
	for (int kind = 1; kind <= fighter::kStateKinds; kind++) {
		if (!TryRead(reinterpret_cast<const void*>(imageBase + fighter::kStateTable + 8u * kind), &states_[kind]) || !states_[kind]) {
			return Log("frame meter: the state table has no state %d", kind), false;
		}
	}
	Log("frame meter: bound to P1 %p and P2 %p with %zu projectile units", (void*)fighters_[0].unit,
		(void*)fighters_[1].unit, projectiles_.size());
	bound_ = true;
	return true;
}

void FighterReader::Forget()
{
	bound_ = false;
	fighters_ = {};
	projectiles_.clear();
	states_ = {};
}

int FighterReader::StateKind(const uint8_t* state) const
{
	for (int kind = 1; kind <= fighter::kStateKinds; kind++)
		if (states_[kind] == reinterpret_cast<uintptr_t>(state)) return kind;
	return 0;
}

bool FighterReader::ReadFighter(const Fighter& f, FighterFrame& out) const
{
	uint8_t* machine = nullptr;
	const uint8_t* state = nullptr;
	const uint8_t* player = nullptr;
	const int32_t* header = nullptr;
	int32_t hitStopElapsed = 0, hitStopTotal = 0, frame = 0, timeScale = 0, vital = 0, vitalMax = 0;
	int16_t untilActionable = 0, posture = 0, total = 0;
	ScriptHeader boundaries;
	if (!ReadField(f.unit, fighter::kStateMachine, &machine) || !ReadField(machine, fighter::kCurrentState, &state) ||
		!ReadField(f.unit, fighter::kHitStopElapsed, &hitStopElapsed) || !ReadField(f.unit, fighter::kHitStopTotal, &hitStopTotal) ||
		!ReadField(f.unit, fighter::kStartedAction, &out.startedAction) ||
		!ReadField(f.unit, fighter::kUntilActionable, &untilActionable) ||
		!ReadField(f.unit, fighter::kCachedAdvantage, &out.gameAdvantage) ||
		!ReadField(f.actionPart, fighter::kScriptPlayer, &player) || !ReadField(player, fighter::kScriptHeader, &header) ||
		!ReadField(player, fighter::kScriptNumber, &out.script) || !ReadField(player, fighter::kScriptFrame, &frame) ||
		!ReadField(player, fighter::kScriptTimeScale, &timeScale) ||
		!ReadField(reinterpret_cast<const uint8_t*>(header), 0, &boundaries) ||
		!ReadField(reinterpret_cast<const uint8_t*>(header), fighter::kHeaderTotalShort, &total) ||
		!ReadField(reinterpret_cast<const uint8_t*>(header), fighter::kHeaderPosture, &posture) ||
		!ReadField(f.statusPart, fighter::kVital, &vital) || !ReadField(f.statusPart, fighter::kVitalMax, &vitalMax)) {
		return false;
	}
	out.stateKind = StateKind(state);
	out.scriptFrame = Fixed(frame);
	// The game reads the total as a short (kSetScript); the header's other
	// three are whole ints.
	out.header = {boundaries.first, boundaries.last, boundaries.interrupt, total};
	out.posture = posture;
	out.timeScale = Fixed(timeScale);
	out.untilActionable = untilActionable;
	out.hitStop = hitStopElapsed < hitStopTotal;
	out.vital = Fixed(vital);
	out.vitalMax = Fixed(vitalMax);
	out.valid = true;
	return true;
}

bool FighterReader::Read(std::array<FighterFrame, 2>& fighters)
{
	fighters = {};
	if (!bound_) return false;
	for (int side = 0; side < 2; side++) {
		if (!ReadFighter(fighters_[side], fighters[side])) {
			fighters = {};
			return false;
		}
	}
	// A projectile is out from the tick its live flag rises.
	for (Projectile& projectile : projectiles_) {
		int32_t live = 0, owner = -1;
		if (!ReadField(projectile.unit, fighter::kProjectileLive, &live) || !ReadField(projectile.unit, fighter::kProjectileOwner, &owner))
			continue;
		if (live && !projectile.live && owner >= 0 && owner < 2) fighters[owner].projectileOut = true;
		projectile.live = live != 0;
	}
	return true;
}

} // namespace sfve::mod
