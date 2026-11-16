#include "mod/NetplayDevCommands.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <stdio.h>
#include <stdlib.h>

namespace sfve::mod {

void NetplayDevCommands::Register(std::mutex& mutex, const std::unique_ptr<RollbackSession>& session, FramePacer& pacer)
{
	RegisterCommand("freeze", [this, &mutex](const std::string& args) {
		std::lock_guard lock(mutex);
		freezeFrame_ = args.empty() ? -1 : atoi(args.c_str());
		return std::string("ok");
	});
	// "riftgain N": the pacer's rift gain as N/120 (tuning; FramePacer).
	RegisterCommand("riftgain", [&mutex, &pacer](const std::string& args) {
		std::lock_guard lock(mutex);
		pacer.SetRiftGain(atof(args.c_str()) / 120.0);
		return std::string("ok");
	});
	// "skipafter <n>": this copy presses an attack n frames into each decided
	// match's scenes (-1, or nothing given: never), for runroom's -skipAfter.
	RegisterCommand("skipafter", [this, &mutex](const std::string& args) {
		std::lock_guard lock(mutex);
		skipAfter_ = args.empty() ? -1 : atoi(args.c_str());
		return std::string("ok");
	});
	// "vital <P1 percent> <P2 percent>": each fighter's vital set to that share
	// of its maximum at every round's first fight frame (0 or 100: left as
	// the round gives it), for runroom's KO endings. Not rollback-proof: runs
	// that use it play without rollbacks.
	RegisterCommand("vital", [this, &mutex](const std::string& args) {
		int p1 = 0, p2 = 0;
		if (sscanf_s(args.c_str(), "%d %d", &p1, &p2) != 2 || p1 < 0 || p1 > 100 || p2 < 0 || p2 > 100) {
			return std::string("error usage: vital <P1 percent> <P2 percent> (0 or 100: as the round gives it)");
		}
		std::lock_guard lock(mutex);
		vitalPercent_[0] = p1;
		vitalPercent_[1] = p2;
		return std::string("ok");
	});
	RegisterCommand("dump", [this, &mutex, &session](const std::string& args) {
		std::lock_guard lock(mutex);
		dumpFrame_ = atoi(args.c_str());
		dumpBytes_.clear();
		// Its save keeps its copy even where no load can reach it.
		if (session) session->KeepSave(dumpFrame_);
		return std::string("ok");
	});
	RegisterCommand("perturb", [this, &mutex](const std::string& args) {
		char region[64] = {};
		Perturbation p;
		if (sscanf_s(args.c_str(), "%d %63s %zi %d", &p.frame, region, (unsigned)sizeof(region), &p.offset, &p.add) != 4 ||
			p.frame < 0) {
			return std::string("error usage: perturb <frame> <region name or size> <offset> <add>");
		}
		p.region = region;
		std::lock_guard lock(mutex);
		perturb_ = p;
		return std::string("ok");
	});
	RegisterCommand("hash", [&mutex, &session](const std::string& args) {
		std::lock_guard lock(mutex);
		uint64_t hash = 0;
		const int frame = atoi(args.c_str());
		if (!session || !session->HashAt(frame, &hash)) return std::string("error frame not kept");
		char text[32];
		snprintf(text, sizeof(text), "ok %016llx", (unsigned long long)hash);
		return std::string(text);
	});
	RegisterCommand("checkpoint", [&mutex, &session](const std::string& args) {
		std::lock_guard lock(mutex);
		if (!session) return std::string("error no session");
		const int atOrBefore = args.empty() ? session->LastConfirmedFrame() : atoi(args.c_str());
		const int frame = session->LatestCheckpoint(atOrBefore);
		std::vector<RegionHash> hashes;
		if (frame < 0 || !session->RegionHashesAt(frame, &hashes)) return std::string("error no checkpoint");
		// size:hash per region, in the canonical order peers compare.
		std::string out = "ok " + std::to_string(frame);
		char item[48];
		for (const RegionHash& h : hashes) {
			snprintf(item, sizeof(item), " %llu:%016llx", (unsigned long long)h.size, (unsigned long long)h.hash);
			out += item;
		}
		return out;
	});
}

// Layout: per region {uint64 size, uint64 address}, then its bytes. Written
// once the frame is confirmed, so two copies' dumps of a frame are final. The
// session may free the frame's save before then, so its latest save (a
// rollback replays and saves it again) is copied while it is kept.
void NetplayDevCommands::DumpIfRequested(RollbackSession* session, int instance)
{
	if (dumpFrame_ < 0) return;
	session->KeepSave(dumpFrame_); // a session started since the request keeps it too
	if (const uint8_t* saved = session->SavedState(dumpFrame_)) {
		dumpBytes_.assign(saved, saved + session->Layout().TotalSize());
	}
	// With input delay a frame's inputs can be confirmed before it is played.
	if (session->Frame() < dumpFrame_ || session->LastConfirmedFrame() < dumpFrame_) return;
	if (dumpBytes_.empty()) {
		Log("netplay: frame %d was not kept; nothing dumped", dumpFrame_);
		dumpFrame_ = -1;
		return;
	}
	const uint8_t* bytes = dumpBytes_.data();
	const std::wstring path = LogFolder() + L"\\state-" + std::to_wstring(instance) + L"-" +
		std::to_wstring(dumpFrame_) + L".bin";
	FILE* f = nullptr;
	if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
	for (const StateRegion& r : session->Layout().Regions()) {
		const uint64_t header[2] = {r.size, reinterpret_cast<uint64_t>(r.ptr)};
		fwrite(header, sizeof(header), 1, f);
		fwrite(bytes, 1, r.size, f);
		bytes += r.size;
	}
	fclose(f);
	Log("netplay: dumped frame %d to %ls", dumpFrame_, path.c_str());
	dumpFrame_ = -1;
	dumpBytes_.clear();
}

void NetplayDevCommands::GiveVital(SfvAdapter& adapter, int frame)
{
	namespace fighter = sfv::fighter;
	uint8_t* anyUnit = adapter.AnyUnit();
	if ((vitalPercent_[0] <= 0 && vitalPercent_[1] <= 0) || !anyUnit) return;
	for (uint32_t index = 0; index < 2; index++) {
		uint8_t* unit = GameFindUnit(anyUnit, sfv::unit::kCharaUnitType, index);
		uint8_t* status = unit ? GameFindPart(unit, fighter::kStatusPartType) : nullptr;
		int32_t side = -1, vitalMax = 0;
		if (!status || !TryRead(unit + fighter::kSide, &side) || side < 0 || side > 1 ||
			!TryRead(status + fighter::kVitalMax, &vitalMax)) {
			Log("netplay: vital: fighter %u not found at frame %d", index, frame);
			continue;
		}
		const int percent = vitalPercent_[side];
		if (percent <= 0 || percent >= 100) continue;
		const int32_t vital = static_cast<int32_t>(static_cast<int64_t>(vitalMax) * percent / 100);
		const bool ok = TryWrite(status + fighter::kVital, vital);
		Log("netplay: vital: P%d's set to %d%% (%d of %d, 16.16) at frame %d%s", side + 1, percent, vital, vitalMax, frame,
			ok ? "" : ": the write failed");
	}
}

// The live state is the played frame's, already saved: a rollback to that
// frame or earlier undoes the change, so it is meant for a frame no rollback
// crosses (before the players' inputs start).
void NetplayDevCommands::PerturbIfDue(RollbackSession* session)
{
	if (perturb_.frame < 0 || session->Frame() < perturb_.frame) return;
	const StateRegion* r = session->Layout().FindByNameOrSize(perturb_.region);
	if (r && perturb_.offset + 4 <= r->size) {
		int32_t& value = *reinterpret_cast<int32_t*>(r->ptr + perturb_.offset);
		value += perturb_.add;
		Log("netplay: perturbed %s+0x%zX at frame %d (now %d)", r->name.c_str(), perturb_.offset, session->Frame(), value);
	} else {
		Log("netplay: nothing to perturb at %s+0x%zX", perturb_.region.c_str(), perturb_.offset);
	}
	perturb_.frame = -1;
}

void NetplayDevCommands::PerturbStart(SfvAdapter& adapter, const std::string& spec)
{
	if (spec.empty()) return;
	char region[96] = {};
	size_t offset = 0;
	int32_t add = 0;
	// "<region>,<offset>,<add>": a region name has no comma.
	const std::string& text = spec;
	const size_t first = text.find(','), second = text.find(',', first + 1);
	if (first == std::string::npos || second == std::string::npos || first >= sizeof(region)) {
		Log("netplay: -sfve_start_perturb=%s does not read as <region>,<offset>,<add>", text.c_str());
		return;
	}
	text.copy(region, first);
	offset = strtoull(text.c_str() + first + 1, nullptr, 0);
	add = (int32_t)strtol(text.c_str() + second + 1, nullptr, 0);
	const StateRegion* r = adapter.Layout().FindByNameOrSize(region);
	if (!r || offset + 4 > r->size) {
		Log("netplay: nothing to perturb at %s+0x%zX before frame 0", region, offset);
		return;
	}
	int32_t& value = *reinterpret_cast<int32_t*>(r->ptr + offset);
	value += add;
	Log("netplay: perturbed %s+0x%zX before frame 0 (now %d), for the start state's agreement to repair", r->name.c_str(),
		offset, value);
}

} // namespace sfve::mod
