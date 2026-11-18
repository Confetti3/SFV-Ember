#include "mod/SfvLayoutRules.hxx"

#include "core/StartAgreement.hxx"

#include "mod/ModConfig.hxx"
#include "mod/SfvFlow.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/SfvRegionNames.hxx"
#include "mod/SocketReads.hxx"

#include <xxhash.h>

#include <stdio.h>

#include <iterator>

namespace sfve::mod {

static_assert(kMaxFighterUnits == sfv::unit::kMostCharaUnits, "every fighter unit the adapter binds has its outside runs");

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

std::string MakeLayoutRulesText()
{
	// The outside state laid out at fixed made-up addresses, so each region's
	// place in its object (its offset) is hashed with its size, flags and masks.
	OutsideState where = {0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x50000000, 0x58000000};
	for (size_t fighter = 0; fighter < std::size(where.fighterParts); fighter++) {
		for (size_t row = 0; row < kMaxPartRuns; row++) where.fighterParts[fighter][row] = 0x60000000 + (fighter * kMaxPartRuns + row) * 0x1000000;
	}
	StateLayout outside;
	AddStateOutsideTheGameSnapshot(outside, where);
	std::string text = "outside " + Hex(outside.LayoutId());
	for (const StateRegion& region : outside.Regions()) text += " " + region.name + "@" + Hex(reinterpret_cast<uintptr_t>(region.ptr));
	text += "; runs";
	for (const sfv::manager::RunRule& rule : sfv::manager::kRunRules) {
		text += " " + std::string(rule.owner) + "+" + std::to_string(rule.offset) + rule.kind + "[" +
			std::to_string(rule.maskFrom) + "," + std::to_string(rule.maskTo) + ")";
		if (rule.maskEvery) text += "/" + std::to_string(rule.maskEvery);
	}
	// Whose run each of the game's regions is, which the rules above match by
	// the owner's name (RegionNames' tables).
	text += "; region owners " + RegionOwnersText();
	// The flow record (SfvFlow): what each state class restores and which of
	// its words peers leave out.
	namespace fs = sfv::flowstate;
	text += "; flow record " + std::to_string(sizeof(FlowRecord)) + ", " + std::to_string(kMaxFlowStates) + " states, base " +
		std::to_string(fs::kBaseSize) + ", largest " + std::to_string(fs::kMaxSize) + "; classes";
	for (const fs::ClassSize& state : fs::kClassSizes) {
		text += " " + Hex(state.vtable) + ":" + std::to_string(state.size) + (state.kept == state.size ? std::string("+own") : state.kept > fs::kBaseSize ? "+own to " + std::to_string(state.kept) : std::string()) + "/" +
			std::to_string(state.leftoverWords);
	}
	// What every copy must do alike that no table above shows
	// (docs/design/beta8/rollback-fixes.md): which words the hashes test for
	// addresses (core/StateRegions.hxx; also inside the outside state's
	// LayoutId), the CRT math the modes that roll back take, and where a
	// battle that rolls back runs its tick's tasks (ModConfig.hxx): the
	// placement this copy takes (a development build's -sfve_tasks changes
	// it, so a -sfve_tasks=0 copy cannot meet a default one; the default's
	// text is kRollbackTickTasks' name as before); and whether the fight
	// socket reads that may feed fight state are given the fight's position,
	// and the rules that decide them: their version, call sites and offsets
	// (ChooseSocketAnchor, socket_reads::AnchorRulesText).
	// And the start agreement's version: it settles which words the hashes
	// zero below 1 TB (start_state::kVersion 3, the fighters' address ranges),
	// so two copies whose agreements cannot read each other hash apart. And
	// the code rules' version (kRollbackCodeRules).
	text += "; address rule " + std::to_string(kAddressRuleVersion) + "; start agreement " +
		std::to_string(start_state::kVersion) + "; rollback code rules " + std::to_string(kRollbackCodeRules) +
		"; rollback math " + CrtMathName(kRollbackCrtMath) +
		"; rollback tick tasks " + TickTasksName(ChooseTickTasks(Config(), true)) + "; socket anchors " +
		(ChooseSocketAnchor(Config()) ? socket_reads::AnchorRulesText() : std::string("off"));
	return text;
}

} // namespace

void AddStateOutsideTheGameSnapshot(StateLayout& layout, const OutsideState& where)
{
	layout.Add("global.rng", At(where.base, sfv::rng::kGlobal + sfv::rng::kState), 4);
	namespace bf = sfv::battleframe;
	namespace iu = sfv::inputunit;
	// Its counts are 0 on every copy at frame 0 (SfvAdapter::
	// ZeroBattleFrameCounts) and compared like the rest.
	layout.Add("battleframe", At(where.battleFrame, bf::kState), bf::kStateSize);
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
	if (where.koWait) {
		namespace kw = sfv::kowait;
		layout.Add("kowait.state", At(where.koWait, kw::kState), kw::kStateSize);
		layout.Add("kowait.position", At(where.koWait, kw::kPosition), kw::kPositionSize);
	}
	static_assert(std::size(sfv::kPartRunsOutsideSnapshot) <= kMaxPartRuns);
	for (size_t fighter = 0; fighter < std::size(where.fighterParts); fighter++) {
		for (size_t row = 0; row < std::size(sfv::kPartRunsOutsideSnapshot); row++) {
			const sfv::PartRun& run = sfv::kPartRunsOutsideSnapshot[row];
			if (!where.fighterParts[fighter][row]) continue;
			layout.Add("CharaUnit[" + std::to_string(fighter) + "]." + run.name, At(where.fighterParts[fighter][row], run.offset),
				run.size, run.checked);
		}
	}
}

const std::string& LayoutRulesText()
{
	static const std::string text = MakeLayoutRulesText();
	return text;
}

const std::string& LayoutRulesId()
{
	static const std::string id = Hex(XXH3_64bits(LayoutRulesText().data(), LayoutRulesText().size()));
	return id;
}

} // namespace sfve::mod
