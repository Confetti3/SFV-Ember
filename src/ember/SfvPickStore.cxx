#include "ember/SfvPickStore.hxx"

#include "ember/PlayerName.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/ModConfig.hxx"

#include "common/GameCatalog.hxx"
#include "netplay/SettingsStore.hxx"

#include <windows.h>

#include <cstdint>
#include <string>

namespace sfve::adapter {

namespace {

constexpr const char* kSection = "sfvPicks";
// The memory is written once nothing has changed it for this long, so a
// colour stepped through is one write.
constexpr ULONGLONG kQuietMs = 1500;
// The settings can be busy (another copy writes them): the first read is
// tried again this often, this many times, and then given up.
constexpr ULONGLONG kRetryMs = 2000;
constexpr int kReadTries = 5;

struct Store {
	bool restored = false, stopped = false;
	int reads = 0;
	ULONGLONG nextRead = 0;
	std::uint64_t seen = 0;  // the revision last looked at
	std::uint64_t saved = 0; // the revision on disk
	ULONGLONG changedAt = 0;
};
Store s_store;

sf4e::netplay::SettingsStore Settings()
{
	return sf4e::netplay::SettingsStore(sf4e::netplay::SettingsStore::DefaultDirectory());
}

// Reads the saved memory into the pick. Done once it has read, or has tried
// enough times: nothing is written before, so a settings file that could not
// be read is never replaced by a memory that never saw it.
void Restore(ULONGLONG now)
{
	if (now < s_store.nextRead) return;
	nlohmann::json overlay;
	std::string error;
	if (!Settings().LoadOverlay(overlay, error)) {
		s_store.nextRead = now + kRetryMs;
		if (++s_store.reads < kReadTries) return;
		mod::Log("picks: the saved picks could not be read (%s); this run starts without them", error.c_str());
	} else {
		const auto found = overlay.find(kSection);
		const picks::PickMemory saved =
			found == overlay.end() ? picks::PickMemory{} : picks::PickMemory::FromJson(*found, FilledPickSpace());
		const bool applied = RestorePickMemory(saved);
		const std::optional<int> chosen = saved.ChosenFighter();
		mod::Log("picks: the saved picks %s%s%s", applied ? "are in" : "came after a pick was made and are left",
			chosen ? ", the fighter last chosen " : "", chosen ? FighterCode(*chosen).c_str() : "");
	}
	s_store.restored = true;
	PickMemoryNow(s_store.saved);
	s_store.seen = s_store.saved;
}

// The memory as it is now, written. A failed write is tried again after the
// quiet time.
void Save(ULONGLONG now)
{
	std::uint64_t revision = 0;
	const picks::PickMemory memory = PickMemoryNow(revision);
	std::string error;
	const nlohmann::json update = {{kSection, memory.ToJson(FilledPickSpace())}};
	if (!Settings().SaveOverlay(update, error)) {
		mod::Log("picks: the picks could not be saved (%s); tried again in a moment", error.c_str());
		s_store.changedAt = now;
		return;
	}
	s_store.saved = revision;
}

bool Pump()
{
	if (s_store.stopped) return false;
	const ULONGLONG now = GetTickCount64();
	if (!s_store.restored) {
		if (ember::Usable(ember::Catalog())) Restore(now);
		return true;
	}
	if (TestCopy(mod::Config().instance)) return true;
	std::uint64_t revision = 0;
	PickMemoryNow(revision);
	if (revision != s_store.seen) {
		s_store.seen = revision;
		s_store.changedAt = now;
	}
	if (revision != s_store.saved && now - s_store.changedAt >= kQuietMs) Save(now);
	return true;
}

} // namespace

void InstallSfvPickStore()
{
	mod::RunEachGameFrame(Pump);
}

void FlushSfvPickStore()
{
	if (s_store.stopped) return;
	s_store.stopped = true;
	if (!s_store.restored || TestCopy(mod::Config().instance)) return;
	std::uint64_t revision = 0;
	PickMemoryNow(revision);
	if (revision != s_store.saved) Save(GetTickCount64());
}

} // namespace sfve::adapter
