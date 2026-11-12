#include "mod/RngAudit.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <shared_mutex>

namespace sfve::mod {

namespace rng_audit {

void Ranges::Set(const StateLayout& layout, uintptr_t global)
{
	Clear();
	global_ = global;
	for (size_t i = 0; i < layout.Regions().size(); i++) {
		const StateRegion& r = layout.Regions()[i];
		const uintptr_t begin = reinterpret_cast<uintptr_t>(r.ptr);
		ranges_.push_back({begin, begin + r.size, i});
		names_.push_back(r.name);
	}
	std::sort(ranges_.begin(), ranges_.end(), [](const Range& a, const Range& b) { return a.begin < b.begin; });
}

void Ranges::Clear()
{
	ranges_.clear();
	names_.clear();
	global_ = 0;
}

Ranges::Found Ranges::Of(uintptr_t stream) const
{
	Found found;
	if (global_ && stream == global_) {
		found.place = Place::Global;
		found.reseeded = true; // ReseedRandom sets the global's state by its address
		return found;
	}
	const uintptr_t state = stream + sfv::rng::kState;
	// The last region starting at or before the state; regions do not overlap.
	auto it = std::upper_bound(ranges_.begin(), ranges_.end(), state, [](uintptr_t at, const Range& r) { return at < r.begin; });
	if (it == ranges_.begin()) return found;
	--it;
	if (state + 4 > it->end) return found;
	found.place = Place::InLayout;
	found.region = it->region;
	found.reseeded = stream >= it->begin && stream + 8 <= it->end && (stream & 7) == 0;
	return found;
}

} // namespace rng_audit

namespace {

using DrawFn = uint32_t (*)(uint8_t* generator);
DrawFn o_draw;
uintptr_t s_base;
bool s_installed;

// The layout's regions, read by the draws on the battle's job threads while
// an update runs; set and cleared by the game thread between updates.
std::shared_mutex s_rangesMutex;
rng_audit::Ranges s_ranges;
std::atomic<bool> s_inUpdate{false};
std::atomic<bool> s_fighting{false};
std::atomic<uint64_t> s_draws, s_layoutDraws, s_globalDraws, s_outsideDraws, s_unreseededDraws;

// What was logged this battle, once each.
std::mutex s_seenMutex;
std::set<uintptr_t> s_seen;
constexpr size_t kMostLogged = 64;

uint32_t Rva(uintptr_t address)
{
	return address >= s_base ? (uint32_t)(address - s_base) : 0;
}

void Note(uint8_t* generator, uintptr_t caller)
{
	const uintptr_t stream = reinterpret_cast<uintptr_t>(generator);
	rng_audit::Ranges::Found found;
	std::string region;
	{
		std::shared_lock lock(s_rangesMutex);
		if (s_ranges.Empty()) return;
		found = s_ranges.Of(stream);
		if (found.place == rng_audit::Place::InLayout && !found.reseeded) region = s_ranges.Name(found.region);
	}
	s_draws++;
	switch (found.place) {
	case rng_audit::Place::Global: s_globalDraws++; return;
	case rng_audit::Place::InLayout:
		s_layoutDraws++;
		if (found.reseeded) return;
		s_unreseededDraws++;
		break;
	case rng_audit::Place::Outside: s_outsideDraws++; break;
	}
	{
		std::lock_guard lock(s_seenMutex);
		if (s_seen.size() >= kMostLogged || !s_seen.insert(stream).second) return;
	}
	uintptr_t vtable = 0;
	uint32_t state = 0;
	TryRead(generator, &vtable);
	TryRead(generator + sfv::rng::kState, &state);
	if (found.place == rng_audit::Place::Outside) {
		Log("rng audit: a draw from a generator outside the rollback state: object %p (vtable rva 0x%X, state %08x), "
			"returning to rva 0x%X%s; neither reseeded at the start nor restored by a rollback",
			(void*)generator, Rva(vtable), state, Rva(caller), s_fighting ? ", in the fight" : "");
	} else {
		Log("rng audit: a draw from a generator restored with %s but not reseeded at the start (its vtable word lies outside "
			"that region): object %p, returning to rva 0x%X%s",
			region.c_str(), (void*)generator, Rva(caller), s_fighting ? ", in the fight" : "");
	}
}

uint32_t h_Draw(uint8_t* generator)
{
	if (s_inUpdate.load(std::memory_order_acquire)) Note(generator, reinterpret_cast<uintptr_t>(_ReturnAddress()));
	return o_draw(generator);
}

} // namespace

bool InstallRngAudit(uintptr_t imageBase)
{
	s_base = imageBase;
	s_installed = HookCodeSite(imageBase, sfv::kRandomDraw, reinterpret_cast<void*>(&h_Draw), reinterpret_cast<void**>(&o_draw));
	Log("rng audit: %s", s_installed ? "every draw in a battle update is checked against the rollback state (-sfve_rng_audit)"
										: "the generator's draw NOT hooked; nothing is audited");
	return s_installed;
}

void RngAuditBind(const StateLayout& layout, uintptr_t imageBase)
{
	if (!s_installed) return;
	std::unique_lock lock(s_rangesMutex);
	s_ranges.Set(layout, imageBase + sfv::rng::kGlobal);
}

void RngAuditUpdate(bool on, bool fighting)
{
	if (!s_installed) return;
	s_fighting.store(fighting, std::memory_order_relaxed);
	s_inUpdate.store(on, std::memory_order_release);
}

void RngAuditForget()
{
	if (!s_installed) return;
	s_inUpdate.store(false, std::memory_order_release);
	{
		std::unique_lock lock(s_rangesMutex);
		if (s_ranges.Empty()) return;
		s_ranges.Clear();
	}
	size_t generators = 0;
	{
		std::lock_guard lock(s_seenMutex);
		generators = s_seen.size();
		s_seen.clear();
	}
	Log("rng audit: the battle ends: %llu draw(s) in its updates, %llu from generators in the rollback state, %llu from the "
		"global one, %llu from generators outside it and %llu from generators not reseeded (%zu such generator(s) logged)",
		(unsigned long long)s_draws.exchange(0), (unsigned long long)s_layoutDraws.exchange(0),
		(unsigned long long)s_globalDraws.exchange(0), (unsigned long long)s_outsideDraws.exchange(0),
		(unsigned long long)s_unreseededDraws.exchange(0), generators);
}

} // namespace sfve::mod
