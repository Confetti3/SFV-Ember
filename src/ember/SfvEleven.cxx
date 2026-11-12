#include "ember/SfvEleven.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPickSpace.hxx"

#include "common/GameCatalog.hxx"

#include <atomic>
#include <random>

namespace sfve::adapter {

namespace {

std::atomic<std::uint64_t> s_salt{0};

// splitmix64's finalizer: every input bit reaches every output bit.
std::uint64_t Mix(std::uint64_t value)
{
	value += 0x9E3779B97F4A7C15ull;
	value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
	value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
	return value ^ (value >> 31);
}

// The choice number of a part drawn from `seed`: one of the fighter's
// `choices` (1 or 2), as likely as another; 0 (I) for a fighter with none.
int DrawChoice(std::uint64_t seed, picks::LoadoutPart part, int choices)
{
	if (choices <= 1) return 0;
	return (int)(Mix(seed ^ ((std::uint64_t)part + 1) * 0xD6E8FEB86659FD93ull) % (std::uint64_t)choices);
}

} // namespace

std::uint64_t ElevenSalt()
{
	std::uint64_t salt = s_salt.load();
	if (salt != 0) return salt;
	std::random_device source;
	do {
		salt = ((std::uint64_t)source() << 32) | source();
	} while (salt == 0);
	// Two threads asking first keep one salt.
	std::uint64_t expected = 0;
	return s_salt.compare_exchange_strong(expected, salt) ? salt : expected;
}

void SetElevenSaltForTest(std::uint64_t salt)
{
	s_salt = salt;
}

std::uint64_t ElevenSeed(std::uint64_t salt, std::uint64_t roll)
{
	return Mix(salt ^ Mix(roll));
}

std::string ElevenTitle(const std::string& fighter)
{
	// The separator Random's "Random - {0}" title puts between its words (U+00B7).
	return ElevenName() + " \u00B7 " + fighter;
}

sf4e::selection::Pick ElevenPick(int fighter, std::uint64_t seed)
{
	sf4e::selection::Pick pick;
	pick.fighter = fighter;
	if (!ember::ValidFighter(fighter)) return pick;
	// The game's Eleven look, as the spec numbers it; a fighter without that
	// costume or colour is fitted below, as any pick is.
	if (const std::optional<ElevenSpec> look = ElevenSpecOf()) {
		pick.costume = look->costume;
		pick.color = look->colour;
	}
	picks::Loadout loadout;
	loadout.choice[picks::VSkill] = (std::uint8_t)DrawChoice(seed, picks::VSkill, PartChoices(fighter, picks::VSkill));
	loadout.choice[picks::VTrigger] = (std::uint8_t)DrawChoice(seed, picks::VTrigger, PartChoices(fighter, picks::VTrigger));
	picks::SetLoadout(pick, loadout);
	FitLookWithin(pick);
	return pick;
}

} // namespace sfve::adapter
