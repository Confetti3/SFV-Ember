#include "ember/DesyncReport.hxx"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <cstdio>
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

std::string Title(bool watcher, std::string_view buildId)
{
	return std::string(watcher ? "SFV desync seen by a watcher: build " : "SFV desync: build ") +
		std::string(buildId.substr(0, 12));
}

std::string Comment(const Facts& facts, std::string_view buildId)
{
	std::string text = Title(facts.watcher, buildId) + "\n";
	if (facts.roomEpoch) {
		text += "Room " + Hex16(facts.roomEpoch) + ", table " + std::to_string(facts.table) + ", match " +
			std::to_string(facts.generation);
	} else {
		text += "Room not known";
	}
	text += "; games ended in this battle: " + std::to_string(facts.gamesEnded) + "\n";
	if (facts.watcher) {
		text += "This copy: a watcher (match slot " + std::to_string(facts.slot) +
			"); its mismatch is a diagnostic only, the fight went on\n";
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
	text += "Checkpoint " + std::to_string(facts.frame) + ": " + Hex16(facts.local) + " here, " + Hex16(facts.remote) +
		(facts.watcher ? " at a fighter" : " at the other player") + "; confirmed to " +
		std::to_string(facts.confirmed) + "\n";
	text += "Input delay " + std::to_string(facts.delay) + "; " + std::to_string(facts.rollbacks) +
		" rollbacks, the longest " + std::to_string(facts.longestRollback) + " frames\n";
	text += "Layout " + Hex16(facts.layoutId) + ", " + std::to_string(facts.regions.size()) +
		" regions at the checkpoint; match build " + facts.matchBuild + "\n";
	text += "The regions' hashes are in sfv_ember.log; pair this report with the other player's by room, table and match.";
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

std::string Record(std::string_view comment, std::string_view regionLines, std::string_view redactedTail)
{
	std::string text(comment);
	text += "\n\n";
	text.append(regionLines);
	text += "\nThe last lines of sfv_ember.log before the desync (redacted):\n";
	if (text.size() >= kLogBytes) {
		// Never with SFV's few hundred regions. The list's last lines go,
		// whole lines only.
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
		{"comment", spooled.comment}, {"region_lines", spooled.regionLines}, {"log_tail", spooled.logTail}};
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
	if (!HexDigits(spooled.buildId, 64) || spooled.comment.empty() ||
		FitCharacters(spooled.comment, kCommentCharacters).size() != spooled.comment.size()) {
		return std::nullopt;
	}
	return spooled;
}

Outcome Judge(const sf4e::HttpResponse& answer, bool withLog)
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
	} else if (status == 408 || status == 429 || status >= 500) {
		outcome.verdict = Verdict::Later;
	} else {
		outcome.verdict = Verdict::Refused;
	}
	return outcome;
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
