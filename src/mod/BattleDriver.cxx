#include "mod/BattleDriver.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/WriteWatch.hxx"

#include <windows.h>

#include <stdio.h>

#include <algorithm>
#include <vector>

namespace sfve::mod {

namespace {

constexpr uint32_t kStatusEveryFrames = 30;

} // namespace

void BattleDriver::OnBattleFrame(uint8_t* battleFrame, BattleFrameFn original)
{
	adapter_.SeeBattleFrame(battleFrame, original);
	gameThread_ = GetCurrentThreadId();
	frames_++;
	if (!adapter_.Bind()) {
		adapter_.RunOriginal(); // the battle's objects are not all known yet
	} else {
		if (boundBattle_ != adapter_.Battle()) {
			boundBattle_ = adapter_.Battle();
			LogInventory();
			mode_->NewBattle(adapter_);
		}
		if (restart_.exchange(false)) mode_->Restart();
		mode_->Frame(adapter_);
	}
	NoteBattleSeen({adapter_.Battle(), adapter_.Fighting(), adapter_.TicksInLastUpdate(), GameFrameNumber()});
	if (frames_ % kStatusEveryFrames == 0) PublishStatus();
}

void BattleDriver::LogInventory() const
{
	const StateLayout& layout = adapter_.Layout();
	Log("inventory: %zu regions, %zu bytes", layout.Regions().size(), layout.TotalSize());
	for (const StateRegion& r : layout.Regions()) Log("inventory:   %-28s %p %8zu bytes", r.name.c_str(), r.ptr, r.size);
	// Each callback calls a closure {function, ...} it was given; log the
	// distinct functions behind them.
	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	std::vector<uintptr_t> seen;
	for (const SfvAdapter::PostRestore& cb : adapter_.PostRestoreCallbacks()) {
		const uintptr_t inner = *static_cast<const uintptr_t*>(cb.context) - base;
		if (std::find(seen.begin(), seen.end(), inner) != seen.end()) continue;
		seen.push_back(inner);
		Log("inventory: post-restore target rva 0x%llX", (unsigned long long)inner);
	}
	Log("inventory: %zu post-restore callbacks", adapter_.PostRestoreCallbacks().size());
}

void BattleDriver::PublishStatus()
{
	std::string json = "{\"frames\":" + std::to_string(frames_) + ",\"adapter\":" + adapter_.Describe() +
		",\"mode\":" + mode_->Describe() + "}";
	std::lock_guard lock(statusMutex_);
	status_ = std::move(json);
}

void BattleDriver::RegisterControl()
{
	RegisterStatus("battle", [this] {
		std::lock_guard lock(statusMutex_);
		return status_;
	});
	RegisterCommand("restart", [this](const std::string&) {
		restart_ = true;
		return std::string("ok");
	});
	RegisterCommand("watch", [this](const std::string& args) {
		// The region by name ("game.541") or by size (the first of that size).
		char key[64] = {};
		size_t offset = 0;
		if (sscanf_s(args.c_str(), "%63s %zi", key, (unsigned)sizeof(key), &offset) != 2) {
			return std::string("error usage: watch <region name or size> <offset>");
		}
		const StateRegion* r = adapter_.Layout().FindByNameOrSize(key);
		if (!r || offset >= r->size) return std::string("error no such region");
		const bool ok = ArmWriteWatch(gameThread_, reinterpret_cast<uintptr_t>(r->ptr) + offset, 4);
		return std::string(ok ? "ok armed on " : "error could not arm ") + r->name;
	});
	RegisterCommand("watchhits", [](const std::string&) { return "ok " + WriteWatchHits(); });
	RegisterCommand("gamestack", [](const std::string&) {
		if (!GameThreadId()) return std::string("error the game thread is not known yet");
		return "ok" + ThreadStack(GameThreadId());
	});
	// "scan <u32> [<u32>...]": every 4-byte-aligned field of the state holding
	// one of the values, as "region+offset=value" (up to 64).
	RegisterCommand("scan", [this](const std::string& args) {
		std::vector<uint32_t> values;
		for (const char* p = args.c_str(); *p;) {
			char* end = nullptr;
			const unsigned long v = strtoul(p, &end, 0);
			if (end == p) break;
			values.push_back((uint32_t)v);
			p = end;
		}
		std::string out = "ok";
		int found = 0;
		for (const StateRegion& r : adapter_.Layout().Regions()) {
			for (size_t at = 0; at + 4 <= r.size && found < 64; at += 4) {
				const uint32_t v = *reinterpret_cast<const uint32_t*>(r.ptr + at);
				if (std::find(values.begin(), values.end(), v) == values.end()) continue;
				char item[96];
				snprintf(item, sizeof(item), " %s+0x%zX=%u", r.name.c_str(), at, v);
				out += item;
				found++;
			}
		}
		return out;
	});
	// "peek <region name or size> <offset> <length>": live bytes, as 32-bit words.
	RegisterCommand("peek", [this](const std::string& args) {
		char key[64] = {};
		size_t offset = 0, length = 0;
		if (sscanf_s(args.c_str(), "%63s %zi %zi", key, (unsigned)sizeof(key), &offset, &length) != 3) {
			return std::string("error usage: peek <region name or size> <offset> <length>");
		}
		const StateRegion* r = adapter_.Layout().FindByNameOrSize(key);
		if (!r || offset >= r->size) return std::string("error no such region");
		std::string out = "ok";
		char word[16];
		for (size_t at = offset; at + 4 <= r->size && at < offset + length; at += 4) {
			snprintf(word, sizeof(word), " %08x", *reinterpret_cast<const uint32_t*>(r->ptr + at));
			out += word;
		}
		return out;
	});
}

} // namespace sfve::mod
