#include "mod/EntitlementGrant.hxx"

#include "mod/GameContent.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <algorithm>
#include <mutex>
#include <optional>

namespace sfve::mod {

namespace cc = sfv::codecache;

bool AccountGrants::Grants(int32_t kind, uint64_t key) const
{
	if (kind < 0 || kind >= cc::kKinds) return false;
	const std::vector<uint64_t>& granted = keys[(size_t)kind];
	return std::binary_search(granted.begin(), granted.end(), key);
}

namespace {

// More entries than any kind can hold (the EntitlementSpec table has one
// row per code): a count past it is not a count.
constexpr int32_t kMaxEntries = 1 << 20;

// One kind's entries in the cache: none when the map has no pair for it,
// as the game's reader then finds none.
struct KindEntries {
	uint8_t* data = nullptr;
	int32_t count = 0;
};

// The pair of `kind`, found as the game's reader finds it (rva 0x54F680:
// the bucket, then the chain), with every index checked against the pairs'
// count and the chain's length bounded by it; why not, in `error`.
std::optional<KindEntries> EntriesOf(uint8_t* spec, int32_t kind, std::string& error)
{
	const int32_t bucketCount = Field<int32_t>(spec, cc::kMapBucketCount);
	if (bucketCount == 0) return KindEntries{}; // an empty map
	if (bucketCount < 0 || (bucketCount & (bucketCount - 1)) != 0) {
		error = "its hash has " + std::to_string(bucketCount) + " buckets, not a power of two";
		return std::nullopt;
	}
	const int32_t* buckets = Field<int32_t*>(spec, cc::kMapBuckets);
	if (!buckets) {
		if (bucketCount != 1) {
			error = "its hash has " + std::to_string(bucketCount) + " buckets and no allocation for them";
			return std::nullopt;
		}
		buckets = reinterpret_cast<const int32_t*>(spec + cc::kMapInlineBucket);
	}
	uint8_t* pairs = Field<uint8_t*>(spec, cc::kMap);
	const int32_t pairCount = Field<int32_t>(spec, cc::kMapCount);
	int32_t index = buckets[kind & (bucketCount - 1)];
	for (int32_t steps = 0; index != -1; steps++) {
		if (!pairs || index < 0 || index >= pairCount || steps >= pairCount) {
			error = "kind " + std::to_string(kind) + "'s chain leaves its " + std::to_string(pairCount) + " pairs";
			return std::nullopt;
		}
		uint8_t* pair = pairs + (size_t)cc::kPairStride * (size_t)index;
		if (Field<int32_t>(pair, cc::kPairKind) == kind) {
			KindEntries entries{Field<uint8_t*>(pair, cc::kPairEntries), Field<int32_t>(pair, cc::kPairEntryCount)};
			if (entries.count < 0 || entries.count > kMaxEntries || (entries.count > 0 && !entries.data)) {
				error = "kind " + std::to_string(kind) + " has " + std::to_string(entries.count) + " entries" +
					(entries.data ? "" : " and no data");
				return std::nullopt;
			}
			return entries;
		}
		index = Field<int32_t>(pair, cc::kPairNext);
	}
	return KindEntries{};
}

uint8_t* EntryAt(const KindEntries& entries, int32_t i)
{
	return entries.data + (size_t)cc::kEntryStride * (size_t)i;
}

// Every kind the refreshes read, or none when any cannot be read.
std::optional<std::array<KindEntries, cc::kKinds>> KindsOf(uint8_t* spec, std::string& error)
{
	std::array<KindEntries, cc::kKinds> kinds;
	for (int32_t kind = 0; kind < cc::kKinds; kind++) {
		std::optional<KindEntries> entries = EntriesOf(spec, kind, error);
		if (!entries) return std::nullopt;
		kinds[(size_t)kind] = *entries;
	}
	return kinds;
}

} // namespace

std::string ClearCodeCacheMarks(uint8_t* spec)
{
	std::string error;
	const std::optional<std::array<KindEntries, cc::kKinds>> kinds = KindsOf(spec, error);
	if (!kinds) return error;
	for (const KindEntries& entries : *kinds) {
		for (int32_t i = 0; i < entries.count; i++) {
			uint8_t* entry = EntryAt(entries, i);
			if (Field<void*>(entry, cc::kEntryRow)) Field<uint8_t>(entry, cc::kEntryGranted) = 0;
		}
	}
	return {};
}

GrantPass ReadAndGrantCodeCache(uint8_t* spec, bool grantAll)
{
	GrantPass pass;
	const std::optional<std::array<KindEntries, cc::kKinds>> read = KindsOf(spec, pass.error);
	if (!read) return pass;
	const std::array<KindEntries, cc::kKinds>& kinds = *read;
	// Every kind read: the account's answer first, then the marks.
	for (int32_t kind = 0; kind < cc::kKinds; kind++) {
		const KindEntries& entries = kinds[(size_t)kind];
		std::vector<uint64_t>& granted = pass.account.keys[(size_t)kind];
		for (int32_t i = 0; i < entries.count; i++) {
			uint8_t* entry = EntryAt(entries, i);
			if (!Field<void*>(entry, cc::kEntryRow)) continue;
			pass.codes[(size_t)kind]++;
			if (Field<uint8_t>(entry, cc::kEntryGranted) == 0) continue;
			pass.accountGranted[(size_t)kind]++;
			granted.push_back(Field<uint64_t>(entry, cc::kEntryKey));
		}
		std::sort(granted.begin(), granted.end());
	}
	pass.account.captured = true;
	if (!grantAll) return pass;
	for (const KindEntries& entries : kinds) {
		for (int32_t i = 0; i < entries.count; i++) {
			uint8_t* entry = EntryAt(entries, i);
			if (!Field<void*>(entry, cc::kEntryRow) || Field<uint8_t>(entry, cc::kEntryGranted) != 0) continue;
			Field<uint8_t>(entry, cc::kEntryGranted) = 1;
			pass.marked++;
		}
	}
	return pass;
}

namespace {

using GrantPassFn = void (*)(uint8_t* spec);

GrantPassFn o_grantPass;
bool s_grantAll = false;
std::mutex s_mutex;
AccountGrants s_account;
// The last pass's line for the main menu's: what the hook did.
std::string s_passSummary = "no refresh has run through the hook";

// The spec's grant pass (step 8's first call, rva 0x57CE10): the game asks
// the account for every code, then the hook reads that answer and, granting,
// marks every code granted before the refreshes read them. Granting, it first
// takes back any marks of an earlier refresh's, so the answer it reads is
// this pass's (ClearCodeCacheMarks); a cache it cannot read is left to the
// game, and the read after says so.
void h_GrantPass(uint8_t* spec)
{
	if (s_grantAll) ClearCodeCacheMarks(spec);
	o_grantPass(spec);
	const GrantPass pass = ReadAndGrantCodeCache(spec, s_grantAll);
	std::string summary;
	if (!pass.error.empty()) {
		summary = "the spec's code cache is not laid out as Ember knows (" + pass.error +
			"), so nothing was granted and the game shows what the account's cache holds";
		Log("entitlements: %s", summary.c_str());
	} else {
		summary = "the account grants " + std::to_string(pass.accountGranted[0]) + " of " + std::to_string(pass.codes[0]) +
			" fighters' codes, " + std::to_string(pass.accountGranted[1]) + " of " + std::to_string(pass.codes[1]) +
			" costumes', " + std::to_string(pass.accountGranted[2]) + " of " + std::to_string(pass.codes[2]) + " colours', " +
			std::to_string(pass.accountGranted[3]) + " of " + std::to_string(pass.codes[3]) + " stages'; " +
			(s_grantAll ? std::to_string(pass.marked) + " more marked granted" : std::string("none marked (grant off)"));
		Log("entitlements: the spec's refresh: %s", summary.c_str());
	}
	std::lock_guard<std::mutex> lock(s_mutex);
	s_account = pass.account;
	s_passSummary = std::move(summary);
}

// The one line at the main menu: the game's answer now, beside the
// account's own, once the content tables can be read.
void LogAtMainMenu()
{
	RunEachGameFrame([] {
		if (SceneStateKind(CurrentSceneState()) != sfv::scene::kMainMenuVtable) return true;
		std::optional<GrantCounts> counts = CountGrants();
		if (!counts) return true; // the tables or the spec's refresh are not there yet
		std::string summary;
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			summary = s_passSummary;
		}
		if (!counts->error.empty()) {
			Log("entitlements: grant %s; the counts cannot be read: %s (%s)", s_grantAll ? "on" : "off (-sfve_grant_all=0)",
				counts->error.c_str(), summary.c_str());
			return false;
		}
		Log("entitlements: grant %s: the game grants %d of %d fighters, %d of %d costumes, %d of %d colours, %d of %d "
			"installed stages; the account's own cache: %d fighters, %d costumes, %d colours, %d stages (%s)",
			s_grantAll ? "on" : "off (-sfve_grant_all=0)", counts->fightersGranted, counts->fighters, counts->costumesGranted,
			counts->costumes, counts->coloursGranted, counts->colours, counts->stagesGranted, counts->stagesInstalled,
			counts->fightersEntitled, counts->costumesEntitled, counts->coloursEntitled, counts->stagesEntitled, summary.c_str());
		return false;
	});
}

} // namespace

bool InstallEntitlementGrant(uintptr_t imageBase, bool grantAll)
{
	s_grantAll = grantAll;
	const bool hooked = HookCodeSite(imageBase, sfv::kSpecGrantPass, (void*)h_GrantPass, (void**)&o_grantPass);
	if (!hooked) {
		Log("entitlements: the spec's grant pass could not be hooked; the game grants what the account's cache holds");
		return false;
	}
	Log("entitlements: %s", grantAll ? "the game grants every player everything it has installed"
	                                 : "grant off (-sfve_grant_all=0): the game grants what the account's cache holds");
	LogAtMainMenu();
	return true;
}

AccountGrants AccountGrantsNow()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	return s_account;
}

} // namespace sfve::mod
