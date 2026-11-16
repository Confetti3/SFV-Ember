#include "mod/FighterData.hxx"

#include "mod/BalanceVersionRules.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

namespace sfve::mod {

namespace {

namespace f = sfv::fighter;

// The key's fighter code (an MSVC std::wstring at kKeyCode), ASCII; empty
// when it cannot be read or is not a code.
std::string KeyCode(const uint8_t* key)
{
	uint64_t length = 0, capacity = 0;
	if (!TryRead(key + f::kKeyCodeLength, &length) || !TryRead(key + f::kKeyCodeCapacity, &capacity)) return {};
	if (length == 0 || length > match_data::kCodeBytes || capacity < length) return {};
	const wchar_t* text = reinterpret_cast<const wchar_t*>(key + f::kKeyCode);
	if (capacity >= f::kKeyCodeInline && !TryRead(key + f::kKeyCode, &text)) return {};
	std::string code;
	for (uint64_t i = 0; i < length; i++) {
		wchar_t c = 0;
		if (!TryRead(text + i, &c) || c <= 0x20 || c >= 0x7F) return {};
		code += (char)c;
	}
	return code;
}

} // namespace

match_data::Fighter ReadFighterData(const uint8_t* actionPart, int side)
{
	match_data::Fighter fighter;
	fighter.side = (int8_t)side;
	const uint8_t* key = nullptr;
	int32_t asked = 0, alternate = 0;
	if (!actionPart || !TryRead(actionPart + f::kResourceKey, &key) || !key) return fighter;
	fighter.code = KeyCode(key);
	if (fighter.code.empty() || !TryRead(key + f::kKeyVersion, &asked) || !TryRead(key + f::kKeyAlternate, &alternate)) return fighter;
	const std::wstring code(fighter.code.begin(), fighter.code.end());
	// The key holds the version as the battle asked for it; the game's
	// lookup resolves it against this copy's list (rva 0x5518C0), so the
	// version loaded is that resolution (a -1 key loads the newest here).
	const auto versions = FighterBalanceVersions(code);
	if (versions) fighter.version = ResolveVersion(*versions, asked).value_or(match_data::kUnknownVersion);
	// The whole list, so a copy whose list ends early shows it in its own log.
	Log("fighter data: %s (P%d) asked balance version %d of this copy's %s", fighter.code.c_str(), side + 1, asked,
		VersionListText(versions).c_str());
	// The move data the battle bound: the same lookup, version and set the
	// action part's resources came from (rva 0x2F67B0).
	if (const auto hash = FighterMoveDataHash(code, asked, alternate != 0)) {
		fighter.dataKnown = true;
		fighter.data = *hash;
	}
	return fighter;
}

} // namespace sfve::mod
