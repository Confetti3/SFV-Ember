#include "core/MatchData.hxx"

#include <stdio.h>
#include <string.h>

#include <algorithm>

namespace sfve::match_data {

namespace {

constexpr size_t kFighterBytes = kCodeBytes + 4 + 4 + 8;

std::string Code(const Fighter& f)
{
	return f.code.empty() ? std::string("?") : f.code;
}

// The fighter and its side: "Z41 (P1)".
std::string Who(const Fighter& f)
{
	return Code(f) + (f.side >= 0 ? " (P" + std::to_string(f.side + 1) + ")" : std::string());
}

std::string Version(int32_t version)
{
	return version == kUnknownVersion ? std::string("unknown") : std::to_string(version);
}

std::string Hex(uint64_t value)
{
	char hex[17];
	snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)value);
	return hex;
}

bool Printable(const std::string& code)
{
	return std::all_of(code.begin(), code.end(), [](char c) { return c > 0x20 && c < 0x7F; });
}

} // namespace

std::optional<Difference> Compare(const Facts& local, const Facts& reference)
{
	const size_t common = std::min(local.fighters.size(), reference.fighters.size());
	for (size_t unit = 0; unit < common; unit++) {
		const Fighter& a = local.fighters[unit];
		const Fighter& b = reference.fighters[unit];
		Difference d{Difference::What::Fighter, unit, a, b};
		if (a.code != b.code || a.side != b.side) return d;
		if (a.version != kUnknownVersion && b.version != kUnknownVersion && a.version != b.version) {
			d.what = Difference::What::Version;
			return d;
		}
		if (a.dataKnown && b.dataKnown && a.data != b.data) {
			d.what = Difference::What::Data;
			return d;
		}
	}
	if (local.fighters.size() == reference.fighters.size()) return std::nullopt;
	Difference d{Difference::What::Units, common, {}, {}};
	if (common < local.fighters.size()) d.local = local.fighters[common];
	if (common < reference.fighters.size()) d.reference = reference.fighters[common];
	return d;
}

std::string Describe(const Difference& d)
{
	const std::string unit = "fighter unit " + std::to_string(d.unit);
	switch (d.what) {
	case Difference::What::Units:
		return "game data differs: another number of fighter units (" + unit + " is " + Code(d.local) + " here and " +
			Code(d.reference) + " at the other copy)";
	case Difference::What::Fighter:
		return "game data differs: " + unit + " is " + Who(d.local) + " here and " + Who(d.reference) + " at the other copy";
	case Difference::What::Version:
		return "game data differs (fighter " + Code(d.local) + "): balance version " + Version(d.local.version) + " here, " +
			Version(d.reference.version) + " at the other copy";
	case Difference::What::Data:
		return "game data differs (fighter " + Code(d.local) + "): balance version " + Version(d.local.version) +
			" on both, move data " + Hex(d.local.data) + " here and " + Hex(d.reference.data) + " at the other copy";
	}
	return "game data differs";
}

std::string NoticeFighter(const Difference& d)
{
	return d.what == Difference::What::Units ? std::string() : Code(d.local);
}

std::string Text(const Facts& facts)
{
	if (facts.fighters.empty()) return "no fighter read";
	std::string text;
	for (const Fighter& f : facts.fighters) {
		if (!text.empty()) text += ", ";
		text += Code(f) + " v" + (f.version == kUnknownVersion ? std::string("?") : std::to_string(f.version)) + " data " +
			(f.dataKnown ? Hex(f.data) : std::string("unknown"));
	}
	return text;
}

int32_t SideVersion(const Facts& facts, int side)
{
	for (const Fighter& f : facts.fighters) {
		if (f.side == side) return f.version;
	}
	return kUnknownVersion;
}

size_t EncodedSize(const Facts& facts)
{
	return 1 + std::min(facts.fighters.size(), kMostFighters) * kFighterBytes;
}

void Encode(const Facts& facts, std::vector<uint8_t>* out)
{
	const size_t count = std::min(facts.fighters.size(), kMostFighters);
	out->push_back((uint8_t)count);
	for (size_t i = 0; i < count; i++) {
		const Fighter& f = facts.fighters[i];
		uint8_t bytes[kFighterBytes] = {};
		memcpy(bytes, f.code.data(), std::min(f.code.size(), kCodeBytes));
		memcpy(bytes + kCodeBytes, &f.version, 4);
		bytes[kCodeBytes + 4] = f.dataKnown ? 1 : 0;
		bytes[kCodeBytes + 5] = (uint8_t)f.side;
		memcpy(bytes + kCodeBytes + 8, &f.data, 8);
		out->insert(out->end(), bytes, bytes + kFighterBytes);
	}
}

bool Decode(const uint8_t* data, size_t size, Facts* out)
{
	if (!data || size < 1) return false;
	const size_t count = data[0];
	if (count > kMostFighters || size != 1 + count * kFighterBytes) return false;
	Facts facts;
	for (size_t i = 0; i < count; i++) {
		const uint8_t* bytes = data + 1 + i * kFighterBytes;
		Fighter f;
		f.code.assign(reinterpret_cast<const char*>(bytes), strnlen(reinterpret_cast<const char*>(bytes), kCodeBytes));
		if (!Printable(f.code)) return false;
		memcpy(&f.version, bytes + kCodeBytes, 4);
		if (bytes[kCodeBytes + 4] > 1) return false;
		f.dataKnown = bytes[kCodeBytes + 4] == 1;
		f.side = (int8_t)bytes[kCodeBytes + 5];
		memcpy(&f.data, bytes + kCodeBytes + 8, 8);
		facts.fighters.push_back(std::move(f));
	}
	*out = std::move(facts);
	return true;
}

} // namespace sfve::match_data
