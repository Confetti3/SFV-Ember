#include "mod/BalanceVersions.hxx"

#include "mod/GameContent.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <string.h>

#include <utility>

namespace sfve::mod {

namespace {

namespace jump = sfv::jump;

using ResolveFn = void (*)(uint8_t* playerRef);
ResolveFn o_resolve;

// Game thread only.
std::vector<LaunchVersion> s_armed;

// The player record a player reference names (jump::kRefRecords and
// kRefRecord), or null.
uint8_t* RecordOf(uint8_t* playerRef)
{
	uint8_t* records = nullptr;
	uint32_t index = jump::kSettingRecords;
	if (!TryRead(playerRef + jump::kRefRecords, &records) || !TryRead(playerRef + jump::kRefRecord, &index)) return nullptr;
	return records && index < jump::kSettingRecords ? records + (size_t)jump::kSettingRecordSize * index : nullptr;
}

std::string RecordCode(const uint8_t* record)
{
	char code[jump::kSettingCodeSize + 1] = {};
	memcpy(code, record + jump::kSettingCode, jump::kSettingCodeSize);
	return code;
}

const LaunchVersion* ArmedFor(const std::string& code)
{
	for (const LaunchVersion& armed : s_armed) {
		if (SameCode(std::wstring(code.begin(), code.end()), armed.fighter)) return &armed;
	}
	return nullptr;
}

void h_ResolvePlayerVersion(uint8_t* playerRef)
{
	uint8_t* record = s_armed.empty() ? nullptr : RecordOf(playerRef);
	if (!record) return o_resolve(playerRef);
	const std::string code = RecordCode(record);
	const LaunchVersion* armed = ArmedFor(code);
	if (!armed) {
		o_resolve(playerRef);
		Log("versions: player record %s is no fighter the launch named; the game resolved balance version %d", code.c_str(),
			Field<int32_t>(record, jump::kSettingVersion));
		return;
	}
	int32_t& version = Field<int32_t>(record, jump::kSettingVersion);
	const int32_t before = version;
	version = armed->version;
	o_resolve(playerRef);
	if (version != armed->version) {
		Log("versions: warning: %s's balance version %d was resolved to %d (the record held %d)", code.c_str(), armed->version,
			version, before);
		return;
	}
	Log("versions: %s loads balance version %d (the record held %d)", code.c_str(), version, before);
}

} // namespace

void ArmLaunchVersions(std::vector<LaunchVersion> versions)
{
	s_armed = std::move(versions);
}

void DisarmLaunchVersions()
{
	s_armed.clear();
}

void AddBalanceVersionHook(HookTransaction& hooks, uintptr_t imageBase)
{
	hooks.Add(imageBase, sfv::kResolvePlayerVersion, (void*)h_ResolvePlayerVersion, (void**)&o_resolve);
}

} // namespace sfve::mod
