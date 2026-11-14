#include "ember/DesyncReport.hxx"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <system_error>

namespace sfve::adapter::desync {

namespace {

std::string Hex16(std::uint64_t value)
{
	char text[17];
	snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
	return text;
}

// "I" or "II" for a choice number, as SFV's select screen numbers them.
const char* Numeral(int number)
{
	return number == 0 ? "I" : number == 1 ? "II" : "?";
}

std::string FighterLine(const char* side, const FighterFacts& fighter)
{
	if (fighter.code.empty()) return std::string(side) + ": not known";
	return std::string(side) + ": " + fighter.code + ", costume " + std::to_string(fighter.costume) + ", colour " +
		std::to_string(fighter.colour) + ", V-Skill " + Numeral(fighter.vSkill) + ", V-Trigger " +
		Numeral(fighter.vTrigger);
}

// At most `characters` UTF-8 characters of `text`, never a partial one.
std::string FitCharacters(std::string text, std::size_t characters)
{
	std::size_t seen = 0;
	for (std::size_t i = 0; i < text.size(); ++i) {
		if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue; // a continuation byte
		if (seen++ == characters) {
			text.resize(i);
			break;
		}
	}
	return text;
}

// The last `bytes` bytes of `text` or fewer, starting at a line's start.
std::string_view LastLines(std::string_view text, std::size_t bytes)
{
	if (text.size() <= bytes) return text;
	text.remove_prefix(text.size() - bytes);
	const std::size_t newline = text.find('\n');
	return newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
}

bool HexDigits(const std::string& text, std::size_t count)
{
	return text.size() == count && std::all_of(text.begin(), text.end(), [](char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
	});
}

// The intake's refusal code, when its answer has a short one.
std::string ReasonCode(const std::string& body)
{
	const nlohmann::json answer = nlohmann::json::parse(body, nullptr, false);
	if (!answer.is_object() || !answer.contains("reason") || !answer["reason"].is_string()) return {};
	const std::string reason = answer["reason"].get<std::string>();
	const bool shortCode = !reason.empty() && reason.size() <= 40 && std::all_of(reason.begin(), reason.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
	});
	return shortCode ? reason : std::string();
}

} // namespace

std::string Title(bool watcher, std::string_view buildId, std::string_view key)
{
	std::string text = std::string(watcher ? "SFV desync seen by a watcher: build " : "SFV desync: build ") +
		std::string(buildId.substr(0, 12));
	if (!key.empty()) text += " match " + std::string(key);
	return text;
}

std::string StartRepairTitle(bool watcher, std::string_view buildId, std::string_view key)
{
	std::string text = std::string(watcher ? "SFV start state repaired for a watcher: build " : "SFV start state repaired: build ") +
		std::string(buildId.substr(0, 12));
	if (!key.empty()) text += " match " + std::string(key);
	return text;
}

std::string MatchKey(const Facts& facts)
{
	if (!facts.roomEpoch) return {};
	return Hex16(facts.roomEpoch) + "." + std::to_string(facts.table) + "." + std::to_string(facts.generation);
}

std::string PairKey(const Facts& facts)
{
	const std::uint64_t low = (std::min)(facts.local, facts.remote), high = (std::max)(facts.local, facts.remote);
	return std::to_string(facts.frame) + ":" + Hex16(low) + "-" + Hex16(high);
}

std::vector<ScriptFacts> ScriptsAt(const Facts& facts)
{
	std::vector<ScriptFacts> out;
	struct Placed {
		const StateRegionFacts* region;
		std::size_t at;
	};
	std::vector<Placed> placed;
	std::size_t at = 0;
	for (const StateRegionFacts& r : facts.stateRegions) {
		placed.push_back({&r, at});
		at += static_cast<std::size_t>(r.size);
	}
	// A state the regions do not account for is not read.
	if (facts.state.empty() || at != facts.state.size()) return out;
	const auto byName = [&](const std::string& name) -> const Placed* {
		for (const Placed& p : placed) {
			if (p.region->name == name) return &p;
		}
		return nullptr;
	};
	const auto byAddress = [&](std::uint64_t address) -> const Placed* {
		for (const Placed& p : placed) {
			if (p.region->address == address) return &p;
		}
		return nullptr;
	};
	const auto read = [&](const Placed& p, std::size_t offset, void* target, std::size_t bytes) {
		if (offset + bytes > p.region->size) return false;
		std::memcpy(target, facts.state.data() + p.at + offset, bytes);
		return true;
	};
	for (int unit = 0; unit < 2; ++unit) {
		const std::string unitName = "CharaUnit[" + std::to_string(unit) + "]";
		const Placed* action = byName(unitName + ".CharaActionPart+0xD8");
		if (!action) continue;
		ScriptFacts f;
		f.unit = unitName;
		// The ActionUnit run (+0xF8): +256 is the player slot, 8 into the run.
		if (const Placed* slot = byName(unitName + "+0xF8")) {
			std::int32_t side = -1;
			if (read(*slot, 8, &side, 4)) f.side = side;
		}
		// The action part's run starts at part +0xD8; the facing is at +0x3E90.
		std::int32_t facing = 0;
		if (read(*action, 0x3E90 - 0xD8, &facing, 4)) f.facing = facing;
		// The script player now playing: a pointer to a timeline object whose
		// registered run starts 0x30 into it; its script number is at object
		// +0x48 and its frame (16.16) at +0x4C (tools/stateregion.py --fighters).
		std::uint64_t player = 0;
		if (read(*action, 0, &player, 8)) {
			if (const Placed* timeline = byAddress(player + 0x30)) {
				std::int32_t script = -1, frame = 0;
				if (read(*timeline, 0x48 - 0x30, &script, 4) && read(*timeline, 0x4C - 0x30, &frame, 4)) {
					f.script = script;
					f.frame = frame / 65536.0;
				}
			}
		}
		out.push_back(std::move(f));
	}
	return out;
}

namespace {

std::string ScriptLine(const ScriptFacts& f)
{
	const std::string who = f.side == 0 ? "P1" : f.side == 1 ? "P2" : f.unit;
	if (f.script < 0) return who + " no script found";
	char frame[32];
	snprintf(frame, sizeof(frame), "%.2f", f.frame);
	return who + " script " + std::to_string(f.script) + " frame " + frame + (f.facing == 1 ? ", mirrored" : "");
}

std::string ScriptsText(const Facts& facts)
{
	std::string text;
	for (const ScriptFacts& f : ScriptsAt(facts)) text += (text.empty() ? "" : "; ") + ScriptLine(f);
	return text;
}

} // namespace

std::string Comment(const Facts& facts, std::string_view buildId)
{
	const bool repair = !facts.startRepair.empty();
	const std::string key = MatchKey(facts);
	std::string text = (repair ? StartRepairTitle(facts.watcher, buildId, key) : Title(facts.watcher, buildId, key)) + "\n";
	if (facts.roomEpoch) {
		text += "Room " + Hex16(facts.roomEpoch) + ", table " + std::to_string(facts.table) + ", match " +
			std::to_string(facts.generation);
	} else {
		text += "Room not known";
	}
	text += "; games ended in this battle: " + std::to_string(facts.gamesEnded) + "\n";
	text += std::string("Pair key: ") + (key.empty() ? "match not known" : "match " + key) + ", mismatch " + PairKey(facts) + "\n";
	if (facts.watcher) {
		text += "This copy: a watcher (match slot " + std::to_string(facts.slot) +
			(repair ? ")\n" : "); its mismatch is a diagnostic only, the fight went on\n");
	} else if (facts.slot == 0 || facts.slot == 1) {
		text += std::string("This copy: ") + (facts.slot == 0 ? "P1" : "P2") + " (match slot " +
			std::to_string(facts.slot) + ")\n";
	} else {
		text += "This copy: a fighter, side not known\n";
	}
	if (facts.picksKnown) {
		text += FighterLine("P1", facts.fighters[0]) + "\n" + FighterLine("P2", facts.fighters[1]) + "\n";
	} else {
		text += "Picks not known\n";
	}
	text += "Stage: " + (facts.stage.empty() ? std::string("not known") : facts.stage) + "\n";
	if (repair) {
		text += "Start state before frame 0: " + Hex16(facts.local) + " here, " + Hex16(facts.remote) + " at P1. " +
			facts.startRepair + "\n";
	} else {
		text += "Checkpoint " + std::to_string(facts.frame) + ": " + Hex16(facts.local) + " here, " + Hex16(facts.remote) +
			(facts.watcher ? " at a fighter" : " at the other player") + "; confirmed to " +
			std::to_string(facts.confirmed) + "\n";
		text += facts.previousMatch >= 0 ? "Previous matching checkpoint: " + std::to_string(facts.previousMatch) + "\n" :
			std::string("Previous matching checkpoint: none (the first checkpoint already differs)\n");
		const std::string scripts = ScriptsText(facts);
		if (!scripts.empty()) text += "Scripts at the checkpoint: " + scripts + "\n";
	}
	text += "Input delay " + std::to_string(facts.delay) + "; " + std::to_string(facts.rollbacks) +
		" rollbacks, the longest " + std::to_string(facts.longestRollback) + " frames\n";
	if (facts.recent.known) {
		text += "Last " + std::to_string(kRecentFrames) + " frames (" + std::to_string(facts.recent.from) + " to " +
			std::to_string(facts.recent.to) + "): " + std::to_string(facts.recent.count) + " rollbacks, the longest " +
			std::to_string(facts.recent.longest) + " frames\n";
	}
	if (!facts.socketReads.empty()) text += "Socket reads: " + facts.socketReads + "\n";
	if (!repair) {
		text += facts.inputs.known ? "Inputs of frames " + std::to_string(facts.inputs.first) + " to " +
			std::to_string(facts.inputs.end) + " are in sfv_ember.log\n" : std::string("Inputs: not recorded\n");
	}
	text += "Layout " + Hex16(facts.layoutId) + ", " + std::to_string(facts.regions.size()) +
		" regions at the checkpoint; match build " + facts.matchBuild + "\n";
	text += "The regions' hashes are in sfv_ember.log; pair this report with the other player's by its match key.";
	return FitCharacters(std::move(text), kCommentCharacters);
}

std::string RegionLines(const Facts& facts)
{
	std::string text;
	char line[96];
	for (const RegionFacts& region : facts.regions) {
		snprintf(line, sizeof(line), ", %llu bytes, hash %016llx\n", static_cast<unsigned long long>(region.size),
			static_cast<unsigned long long>(region.hash));
		text += "desync: checkpoint " + std::to_string(facts.frame) + " region " + region.name + line;
	}
	return text;
}

std::string DetailLines(const Facts& facts)
{
	const std::string key = MatchKey(facts);
	std::string text = "Details of this copy's own view (the other player's report has its own):\n";
	text += std::string("pair key: ") + (key.empty() ? "match not known" : "match " + key) + ", mismatch " + PairKey(facts) + "\n";
	text += "this copy: " + std::string(facts.watcher ? "a watcher" : facts.slot == 0 ? "P1" : facts.slot == 1 ? "P2" : "a fighter") +
		" (match slot " + std::to_string(facts.slot) + "), input delay " + std::to_string(facts.delay) + "\n";
	if (facts.picksKnown) {
		text += FighterLine("P1", facts.fighters[0]) + "\n" + FighterLine("P2", facts.fighters[1]) + "\n";
	}
	text += "stage: " + (facts.stage.empty() ? std::string("not known") : facts.stage) + "\n";
	text += "checkpoint " + std::to_string(facts.frame) + ", found at session frame " +
		(facts.now >= 0 ? std::to_string(facts.now) : std::string("?")) + ", confirmed to " + std::to_string(facts.confirmed) +
		"; previous matching checkpoint " + (facts.previousMatch >= 0 ? std::to_string(facts.previousMatch) : std::string("none")) + "\n";
	const std::string scripts = ScriptsText(facts);
	text += "scripts at the checkpoint: " + (scripts.empty() ? std::string("not read (the state was not kept)") : scripts) + "\n";
	text += "rollbacks, whole battle: " + std::to_string(facts.rollbacks) + ", the longest " +
		std::to_string(facts.longestRollback) + " frames\n";
	if (facts.recent.known) {
		text += "rollbacks, frames " + std::to_string(facts.recent.from) + " to " + std::to_string(facts.recent.to) + ": " +
			std::to_string(facts.recent.count) + ", the longest " + std::to_string(facts.recent.longest) + " frames\n";
	} else {
		text += "rollbacks, last " + std::to_string(kRecentFrames) + " frames: not counted\n";
	}
	text += "pace: " + (facts.pace.empty() ? std::string("not read") : facts.pace) + "\n";
	text += "socket reads: " + (facts.socketReads.empty() ? std::string("not read") : facts.socketReads) + "\n";
	text += "pose holds: " + (facts.poseHolds.empty() ? std::string("not read") : facts.poseHolds) + "\n";
	if (facts.socketMissesKnown) {
		text += "socket read misses (the live value was given), frames " + std::to_string(facts.frame - kRecentFrames) + " to " +
			std::to_string(facts.frame) + ", the latest " + std::to_string(kMostSocketMisses) + " kept: " +
			std::to_string(facts.socketMisses.size()) + "\n";
		char line[160];
		for (const SocketMissFacts& miss : facts.socketMisses) {
			snprintf(line, sizeof(line), "miss %d unit %d %.31s from rva 0x%X: %.40s\n", miss.frame, miss.unit, miss.socket.c_str(),
				static_cast<unsigned>(miss.from), miss.reason.c_str());
			text += line;
		}
	}
	if (!facts.inputs.known) {
		text += "inputs: not recorded (the session's frame hook is not wired, or the frames are gone)\n";
	} else {
		text += "inputs, frames " + std::to_string(facts.inputs.first) + " to " + std::to_string(facts.inputs.end) +
			" (end excluded), one line per run: first frame, frames, P1 and P2 bits in hex, flags (1 held, 2 fight):\n";
		char line[96];
		for (const InputRun& run : facts.inputs.runs) {
			snprintf(line, sizeof(line), "input %d +%d P1 %08x P2 %08x f%u\n", run.first, run.frames,
				static_cast<unsigned>(run.p1), static_cast<unsigned>(run.p2), static_cast<unsigned>(run.flags));
			text += line;
		}
	}
	return text;
}

std::string TailLines(std::string_view log, bool cut, int frame, std::size_t lines)
{
	if (cut) {
		const std::size_t newline = log.find('\n');
		log = newline == std::string_view::npos ? std::string_view() : log.substr(newline + 1);
	}
	const std::string region = "desync: checkpoint " + std::to_string(frame) + " region ";
	std::deque<std::string_view> kept;
	while (!log.empty() && lines) {
		const std::size_t newline = log.find('\n');
		std::string_view line = log.substr(0, newline);
		log = newline == std::string_view::npos ? std::string_view() : log.substr(newline + 1);
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
		if (line.find(region) != std::string_view::npos) continue;
		kept.push_back(line);
		if (kept.size() > lines) kept.pop_front();
	}
	std::string text;
	for (const std::string_view line : kept) {
		text.append(line);
		text += '\n';
	}
	return text;
}

std::string Record(std::string_view comment, std::string_view regionLines, std::string_view redactedTail,
	std::string_view details)
{
	std::string text(comment);
	text += "\n\n";
	if (!details.empty()) {
		text.append(details);
		text += "\n";
	}
	text.append(regionLines);
	text += "\nThe last lines of sfv_ember.log before the desync (redacted):\n";
	if (text.size() >= kLogBytes) {
		// Never with SFV's few hundred regions and 180 frames of inputs. The
		// list's last lines go, whole lines only.
		const std::size_t end = text.rfind('\n', kLogBytes - 1);
		text.resize(end == std::string::npos ? 0 : end + 1);
		return text;
	}
	text.append(LastLines(redactedTail, kLogBytes - text.size()));
	return text;
}

std::string SpoolJson(const Spooled& spooled)
{
	const nlohmann::json value = {{"schema", 1}, {"time", spooled.time}, {"app_version", spooled.appVersion},
		{"build_id", spooled.buildId}, {"source_revision", spooled.sourceRevision}, {"watcher", spooled.watcher},
		{"comment", spooled.comment}, {"region_lines", spooled.regionLines}, {"log_tail", spooled.logTail},
		{"details", spooled.details}};
	// A log line that is not UTF-8 is written with U+FFFD rather than lost.
	return value.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::optional<Spooled> ParseSpool(std::string_view text)
{
	const nlohmann::json value = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
	if (!value.is_object() || !value.contains("schema") || value["schema"] != 1) return std::nullopt;
	Spooled spooled;
	const auto string = [&](const char* key, std::string& out) {
		if (!value.contains(key) || !value[key].is_string()) return false;
		out = value[key].get<std::string>();
		return true;
	};
	if (!value.contains("time") || !value["time"].is_number_integer() || !value.contains("watcher") ||
		!value["watcher"].is_boolean() || !string("app_version", spooled.appVersion) ||
		!string("build_id", spooled.buildId) || !string("source_revision", spooled.sourceRevision) ||
		!string("comment", spooled.comment) || !string("region_lines", spooled.regionLines) ||
		!string("log_tail", spooled.logTail)) {
		return std::nullopt;
	}
	spooled.time = value["time"].get<std::int64_t>();
	spooled.watcher = value["watcher"].get<bool>();
	// Missing in a file an older build wrote; of the wrong kind it is not a
	// file this code writes.
	if (value.contains("details") && !string("details", spooled.details)) return std::nullopt;
	if (!HexDigits(spooled.buildId, 64) || spooled.comment.empty() ||
		FitCharacters(spooled.comment, kCommentCharacters).size() != spooled.comment.size()) {
		return std::nullopt;
	}
	return spooled;
}

Outcome Judge(const sf4e::HttpResponse& answer, bool withLog, bool withState)
{
	Outcome outcome;
	const int status = answer.request.statusCode;
	if (answer.request.ok) {
		const nlohmann::json body = nlohmann::json::parse(answer.body, nullptr, false);
		const std::string id = body.is_object() && body.contains("id") && body["id"].is_string() ?
			body["id"].get<std::string>() : std::string();
		outcome.verdict = Verdict::Delivered;
		if (id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
			outcome.id = id;
		} else {
			outcome.reason = "no_id";
		}
		return outcome;
	}
	if (answer.cancelled) {
		outcome.reason = "cancelled";
		return outcome;
	}
	// A 202 whose receipt broke off: the intake stored the report.
	if (status == 202) {
		outcome.verdict = Verdict::Delivered;
		outcome.reason = "incomplete";
		return outcome;
	}
	if (status == 0) {
		outcome.reason = answer.request.error == sf4e::HttpErrorKind::Timeout ? "timeout" : "no_connection";
		return outcome;
	}
	outcome.reason = ReasonCode(answer.body);
	if (outcome.reason.empty()) outcome.reason = "http_" + std::to_string(status);
	if (status == 400 && withLog && outcome.reason == "unknown_log_filename") {
		outcome.verdict = Verdict::WithoutLog;
	} else if (withState && ((status == 400 && outcome.reason == "unknown_part") || status == 413)) {
		// The state part is the newer of the two: an intake that knows
		// neither answers for the log first, and loses it first.
		outcome.verdict = Verdict::WithoutState;
	} else if (status == 408 || status == 429 || status >= 500) {
		outcome.verdict = Verdict::Later;
	} else {
		outcome.verdict = Verdict::Refused;
	}
	return outcome;
}

namespace {

struct Writer {
	std::string& out;
	void Put(std::uint64_t value, int bytes) const
	{
		for (int i = 0; i < bytes; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
	}
	void Text(const std::string& text) const
	{
		const std::size_t length = (std::min)(text.size(), std::size_t{0xFFFF});
		Put(length, 2);
		out.append(text, 0, length);
	}
};

std::uint64_t Little(const std::uint8_t* at)
{
	std::uint64_t value = 0;
	for (int i = 7; i >= 0; --i) value = (value << 8) | at[i];
	return value;
}

} // namespace

PointerCensus TakeCensus(const Facts& facts, PointerEnv& env, bool (*looksLikeAddress)(std::uint64_t), PointerCensus seed)
{
	PointerCensus census = std::move(seed);
	census.words.clear();
	census.truncated = false;
	struct Extent {
		std::uint64_t address;
		std::uint64_t size;
		std::uint32_t index;
	};
	// The saved regions and the named objects by address, to find where a
	// pointer lands.
	std::vector<Extent> regions, objects;
	std::uint64_t total = 0;
	for (std::uint32_t i = 0; i < facts.stateRegions.size(); ++i) {
		regions.push_back({facts.stateRegions[i].address, facts.stateRegions[i].size, i});
		total += facts.stateRegions[i].size;
	}
	if (total != facts.state.size()) return census;
	for (std::uint32_t i = 0; i < census.objects.size(); ++i) {
		objects.push_back({census.objects[i].address, census.objects[i].extent, i});
	}
	const auto byAddress = [](const Extent& a, const Extent& b) { return a.address < b.address; };
	std::sort(regions.begin(), regions.end(), byAddress);
	std::sort(objects.begin(), objects.end(), byAddress);
	const auto within = [](const std::vector<Extent>& sorted, std::uint64_t value) -> const Extent* {
		auto it = std::upper_bound(sorted.begin(), sorted.end(), value,
			[](std::uint64_t v, const Extent& e) { return v < e.address; });
		if (it == sorted.begin()) return nullptr;
		--it;
		return value - it->address < it->size ? &*it : nullptr;
	};

	std::size_t at = 0;
	for (std::uint32_t r = 0; r < facts.stateRegions.size(); ++r) {
		const StateRegionFacts& region = facts.stateRegions[r];
		const std::size_t base = at;
		at += static_cast<std::size_t>(region.size);
		if (!region.checked || !region.peers) continue;
		// Words on an 8-byte boundary of the real address, as the hash tests them.
		for (std::uint64_t off = (8 - (region.address & 7)) & 7; off + 8 <= region.size; off += 8) {
			const std::uint64_t value = Little(facts.state.data() + base + off);
			if (!looksLikeAddress(value)) continue;
			if (census.words.size() >= kCensusWords) {
				census.truncated = true;
				return census;
			}
			CensusWord word;
			word.region = r;
			word.offset = static_cast<std::uint32_t>(off);
			word.value = value;
			const PointerTarget target = env.Look(value);
			word.mapped = target.mapped;
			word.allocBase = target.allocBase;
			word.allocSize = target.allocSize;
			word.targetHash = target.hash;
			if (!target.mapped) {
				word.cls = PointerClass::Unmapped;
			} else if (census.imageSize && value >= census.imageBase && value - census.imageBase < census.imageSize) {
				word.cls = PointerClass::Image;
				word.reference = value - census.imageBase;
			} else if (!target.module.empty()) {
				word.cls = PointerClass::Module;
				word.reference = value - target.moduleBase;
				// Another module is named by its size only: its file name (a
				// driver, an overlay, an antivirus hook) says what this PC runs
				// and is not the report's to carry.
				const std::string label = "module, " + std::to_string(target.allocSize) + " bytes";
				const auto found = std::find(census.modules.begin(), census.modules.end(), label);
				word.refIndex = static_cast<std::uint32_t>(found - census.modules.begin());
				if (found == census.modules.end()) census.modules.push_back(label);
			} else if (const Extent* in = within(regions, value)) {
				word.cls = PointerClass::Region;
				word.reference = value - in->address;
				word.refIndex = in->index;
			} else if (const Extent* object = within(objects, value)) {
				word.cls = PointerClass::Object;
				word.reference = value - object->address;
				word.refIndex = object->index;
			} else {
				word.cls = PointerClass::Heap;
				word.reference = value - target.allocBase;
			}
			census.words.push_back(word);
		}
	}
	return census;
}

std::string StateFile(const Facts& facts, const PointerCensus* census)
{
	std::uint64_t total = 0;
	for (const StateRegionFacts& r : facts.stateRegions) total += r.size;
	if (facts.stateRegions.empty() || total != facts.state.size()) return {};
	std::string out = "SFVSTATE";
	const Writer w{out};
	w.Put(2, 4);
	w.Put(facts.stateRegions.size(), 4);
	std::size_t at = 0;
	for (const StateRegionFacts& r : facts.stateRegions) {
		w.Text(r.name);
		w.Put(r.size, 8);
		w.Put(r.address, 8);
		w.Put((r.checked ? 1u : 0u) | (r.peers ? 2u : 0u), 4);
		out.append(reinterpret_cast<const char*>(facts.state.data()) + at, static_cast<std::size_t>(r.size));
		at += static_cast<std::size_t>(r.size);
	}
	const PointerCensus none;
	const PointerCensus& c = census ? *census : none;
	w.Put(c.objects.size(), 4);
	for (const CensusObject& o : c.objects) {
		w.Text(o.name);
		w.Put(o.address, 8);
		w.Put(o.extent, 8);
	}
	w.Put(c.modules.size(), 4);
	for (const std::string& name : c.modules) w.Text(name);
	w.Put(c.imageBase, 8);
	w.Put(c.imageSize, 8);
	w.Put(c.truncated ? 1 : 0, 1);
	w.Put(c.words.size(), 4);
	for (const CensusWord& word : c.words) {
		w.Put(word.region, 4);
		w.Put(word.offset, 4);
		w.Put(word.value, 8);
		w.Put(static_cast<std::uint8_t>(word.cls), 1);
		w.Put(word.mapped ? 1 : 0, 1);
		w.Put(0, 2);
		w.Put(word.reference, 8);
		w.Put(word.refIndex, 4);
		w.Put(0, 4);
		w.Put(word.allocBase, 8);
		w.Put(word.allocSize, 8);
		w.Put(word.targetHash, 8);
	}
	return out;
}

std::int64_t SendWaitSeconds(const std::vector<std::int64_t>& sentTimes, std::int64_t now)
{
	const auto wait = [&](std::int64_t window, int allowed) -> std::int64_t {
		std::vector<std::int64_t> inside;
		for (const std::int64_t t : sentTimes) {
			// A send dated after `now` counts too: the clock went back, and
			// the cap must not read as fewer sends than there were.
			if (t > now - window) inside.push_back(t);
		}
		if (inside.size() < static_cast<std::size_t>(allowed)) return 0;
		std::sort(inside.begin(), inside.end());
		// The oldest of the newest `allowed` leaves the window, and a slot opens.
		return (std::max)(std::int64_t{1}, inside[inside.size() - static_cast<std::size_t>(allowed)] + window - now);
	};
	return (std::max)(wait(24 * 60 * 60, kSendsPerDay), wait(10 * 60, kSendsPerTenMinutes));
}

SendPlan PlanSends(const std::vector<Waiting>& waiting, std::string_view installedBuildId, std::int64_t now)
{
	SendPlan plan;
	for (std::size_t i = 0; i < waiting.size(); ++i) {
		if (now - waiting[i].time > kSpoolMaxAgeSeconds) {
			plan.stale.push_back(i);
		} else if (!installedBuildId.empty() && waiting[i].buildId != installedBuildId) {
			plan.otherBuild.push_back(i);
		} else {
			plan.send.push_back(i);
		}
	}
	// Newest first: what the installed build did last is what can be acted on;
	// equal times keep the later file first.
	std::stable_sort(plan.send.begin(), plan.send.end(), [&](std::size_t a, std::size_t b) {
		return waiting[a].time != waiting[b].time ? waiting[a].time > waiting[b].time : a > b;
	});
	return plan;
}

std::string WithPart(std::string_view body, std::string_view boundary, std::string_view name, std::string_view filename,
	std::string_view bytes, std::size_t limit)
{
	const std::string closing = "--" + std::string(boundary) + "--\r\n";
	if (body.size() < closing.size() || body.substr(body.size() - closing.size()) != closing) return {};
	std::string head = "--" + std::string(boundary) + "\r\nContent-Disposition: form-data; name=\"" + std::string(name) +
		"\"; filename=\"" + std::string(filename) + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
	std::string out(body.substr(0, body.size() - closing.size()));
	if (out.size() + head.size() + bytes.size() + 2 + closing.size() > limit) return {};
	out += head;
	out.append(bytes);
	out += "\r\n";
	out += closing;
	return out;
}

Room MakeRoom(const std::filesystem::path& folder, std::size_t limit)
{
	Room room;
	std::vector<std::filesystem::path> waiting;
	const std::wstring extension = kSpoolExtension;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
		const std::wstring name = it->path().filename().wstring();
		if (name.size() > extension.size() && name.compare(name.size() - extension.size(), extension.size(), extension) == 0) {
			waiting.push_back(it->path());
		}
	}
	std::sort(waiting.begin(), waiting.end());
	room.waiting = waiting.size();
	for (const std::filesystem::path& path : waiting) {
		if (room.waiting < limit) break;
		// DeleteFileW, not the filesystem library's remove: it fails on a file
		// the launcher holds (opened without FILE_SHARE_DELETE) and says so.
		if (DeleteFileW(path.c_str())) {
			room.waiting--;
			room.removed.push_back(path.filename().wstring());
		}
	}
	return room;
}

} // namespace sfve::adapter::desync
