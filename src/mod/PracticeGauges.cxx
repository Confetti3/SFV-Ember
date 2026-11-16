#include "mod/PracticeGauges.hxx"

#include "core/GameAdapter.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameImage.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <iterator>

namespace sfve::mod::practice_gauges {

bool SetPractice(uint8_t* rollbackUnit, const uint8_t* battleData)
{
	namespace practice = sfv::practice;
	static const std::pair<int, int> kGauges[] = {{practice::kVitalParam, practice::kVitalRefill},
		{practice::kExtraParam, practice::kExtraRefill}, {practice::kVGaugeParam, practice::kVGaugeRefill}};
	if (!SetParams(rollbackUnit, battleData, kGauges, std::size(kGauges), "Practice gauges")) return false;
	Log("adapter: Practice gauges set for both players (vital %d, EX %d, V-gauge %d)", practice::kVitalRefill,
		practice::kExtraRefill, practice::kVGaugeRefill);
	return true;
}

bool SetStartFullExtra(uint8_t* rollbackUnit, const uint8_t* battleData)
{
	namespace practice = sfv::practice;
	static const std::pair<int, int> kGauges[] = {{practice::kExtraParam, practice::kExtraFullAtRoundStart}};
	if (!SetParams(rollbackUnit, battleData, kGauges, std::size(kGauges), "EX gauge full at each round's start")) return false;
	Log("adapter: the EX gauge starts full at every round's start for both players (param %d = %d)", practice::kExtraParam,
		practice::kExtraFullAtRoundStart);
	return true;
}

bool SetParams(uint8_t* rollbackUnit, const uint8_t* battleData, const std::pair<int, int>* gauges, size_t count,
	const char* what)
{
	namespace practice = sfv::practice;
	using SetParamFn = bool (*)(uint8_t* table, int param, int value);
	if (!battleData) return Log("adapter: no %s: the battle is not bound", what), false;
	const auto set = GameFn<SetParamFn>(sfv::kSetPlayerParam);
	bool sides[kPlayers] = {};
	for (int index = 0; index < kPlayers; index++) {
		uint8_t* chara = GameFindUnit(rollbackUnit, sfv::unit::kCharaUnitType, (uint32_t)index);
		const int side = chara ? Field<int32_t>(chara, practice::kCharaSide) : -1;
		if (side < 0 || side >= kPlayers || sides[side]) {
			return Log("adapter: no %s: fighter %d has side %d", what, index, side), false;
		}
		sides[side] = true;
		// The battle data is the adapter's (const for the rest of it): its
		// tables are what Training's menu writes too.
		uint8_t* table = const_cast<uint8_t*>(battleData) + practice::kPlayerParams + practice::kPlayerParamsStride * side;
		for (size_t k = 0; k < count; k++) {
			const auto& [param, value] = gauges[k];
			if (!set(table, param, value) || !set(chara + practice::kCharaParams, param, value)) {
				return Log("adapter: the game refused gauge %d = %d (%s) for side %d", param, value, what, side), false;
			}
		}
	}
	return true;
}

} // namespace sfve::mod::practice_gauges
