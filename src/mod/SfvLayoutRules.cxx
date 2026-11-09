#include "mod/SfvLayoutRules.hxx"

#include "mod/SfvFlow.hxx"
#include "mod/SfvOffsets.hxx"

#include <xxhash.h>

#include <stdio.h>

namespace sfve::mod {

namespace {

void* At(uintptr_t object, uint32_t offset)
{
	return reinterpret_cast<void*>(object + offset);
}

std::string Hex(uint64_t value)
{
	char hex[17];
	snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)value);
	return hex;
}

std::string MakeLayoutRulesId()
{
	// Laid out at made-up addresses: only sizes, flags and masks are hashed.
	StateLayout outside;
	AddStateOutsideTheGameSnapshot(outside, {0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x50000000});
	std::string text = "outside " + Hex(outside.LayoutId()) + "; flow record " + std::to_string(sizeof(FlowRecord)) + "; runs";
	for (const sfv::manager::RunRule& rule : sfv::manager::kRunRules) {
		text += " " + std::string(rule.owner) + "+" + std::to_string(rule.offset) + rule.kind + "[" +
			std::to_string(rule.maskFrom) + "," + std::to_string(rule.maskTo) + ")";
	}
	return Hex(XXH3_64bits(text.data(), text.size()));
}

} // namespace

void AddStateOutsideTheGameSnapshot(StateLayout& layout, const OutsideState& where)
{
	layout.Add("global.rng", At(where.base, sfv::rng::kGlobal + sfv::rng::kState), 4);
	namespace bf = sfv::battleframe;
	namespace iu = sfv::inputunit;
	const size_t frame = layout.Add("battleframe", At(where.battleFrame, bf::kState), bf::kStateSize);
	for (uint32_t own : {bf::kUpdates, bf::kTickedUpdates, bf::kClock}) layout.IgnoreForPeers(frame, own - bf::kState);
	const size_t counters = layout.Add("inputunit.counters", At(where.inputUnit, iu::kCounters), iu::kCountersSize);
	layout.IgnoreForPeers(counters, iu::kSyncClock - iu::kCounters);
	layout.Add("ruleunit.judge", At(where.ruleUnit, sfv::ruleunit::kJudgeState), 4);
	namespace bd = sfv::battledata;
	layout.Add("battledata.results", At(where.battleData, bd::kResults), bd::kRounds - bd::kResults);
	for (int i = 0; i < bd::kMaxRounds; i++) {
		const uint32_t record = bd::kRounds + i * bd::kRoundStride;
		const std::string name = "battledata.round" + std::to_string(i + 1);
		layout.Add(name, At(where.battleData, record), bd::kRoundStats);
		layout.Add(name + ".stats", At(where.battleData, record + bd::kRoundStats), bd::kRoundStatsSize, false);
	}
	layout.Add("battledata.judgement", At(where.battleData, bd::kDecided), bd::kResults + bd::kResultsSize - bd::kDecided);
	layout.Add("battledata.flowflags", At(where.battleData, bd::kFlowFlags), bd::kFlowFlagsSize);
}

const std::string& LayoutRulesId()
{
	static const std::string id = MakeLayoutRulesId();
	return id;
}

} // namespace sfve::mod
