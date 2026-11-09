// GameContent's rules over the lists that read no game memory: the codes'
// comparison, ownership's application, the counts, the development marks and
// the choices' fitting. GameContent.cxx reads the game's tables; everything
// here takes the lists it read, so the tests call it as the mod does.

#include "mod/GameContent.hxx"

#include "ember/SfvPickSpace.hxx"
#include "mod/Log.hxx"

#include <string.h>
#include <wchar.h>

#include <algorithm>

namespace sfve::mod {

namespace {

// How many choices Ember offers for a part the asset names `given` of: one
// to picks::kPartChoices (FitChoices).
size_t FittedCount(size_t given)
{
	return std::clamp<size_t>(given, 1, (size_t)picks::kPartChoices);
}

} // namespace

bool SameCode(std::wstring_view a, std::wstring_view b)
{
	return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

bool SameCode(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
}

const ColourContent* ColourOf(const CostumeContent& costume, int id)
{
	const auto colour = std::find_if(costume.colours.begin(), costume.colours.end(), [id](const ColourContent& c) { return c.id == id; });
	return colour == costume.colours.end() ? nullptr : &*colour;
}

std::vector<int> ColourNumbers(const CostumeContent& costume)
{
	std::vector<int> numbers;
	for (const ColourContent& colour : costume.colours) numbers.push_back(colour.id);
	return numbers;
}

void ApplyContentOwnership(ContentLists& lists, ContentOwnership policy)
{
	const bool byInstall = policy == ContentOwnership::Installed;
	lists.ownership.stages = policy;
	for (StageContent& stage : lists.stages) stage.owned = stage.installed && (byInstall || stage.entitled);
	const bool looksByInstall = byInstall && !lists.ownership.restricted;
	lists.ownership.looks = looksByInstall ? ContentOwnership::Installed : ContentOwnership::Entitlements;
	if (!looksByInstall) return;
	for (FighterContent& fighter : lists.fighters) {
		for (CostumeContent& costume : fighter.costumes) {
			costume.owned = costume.installed;
			for (ColourContent& colour : costume.colours) colour.owned = costume.installed;
		}
	}
}

const char* ContentOwnershipName(ContentOwnership policy)
{
	switch (policy) {
	case ContentOwnership::Entitlements: return "entitlements";
	case ContentOwnership::Installed: return "installed";
	}
	return "?";
}

const char* LooksRule(const OwnershipSource& source)
{
	return source.restricted ? "the battle setup's list" : ContentOwnershipName(source.looks);
}

ContentCounts CountContent(const ContentLists& lists)
{
	ContentCounts counts;
	for (const FighterContent& fighter : lists.fighters) {
		counts.fighters++;
		counts.fightersEntitled += fighter.entitled;
		counts.fightersNotOwned += !fighter.owned;
		for (const CostumeContent& costume : fighter.costumes) {
			counts.costumes++;
			counts.costumesInstalled += costume.installed;
			counts.costumesEntitled += costume.entitled;
			counts.costumesNotOwned += !costume.owned;
			for (const ColourContent& colour : costume.colours) {
				counts.colours++;
				counts.coloursNotOwned += !colour.owned;
				counts.looksNotEntitled += !(costume.entitled && colour.entitled);
			}
		}
	}
	for (const StageContent& stage : lists.stages) {
		counts.stages++;
		counts.stagesInstalled += stage.installed;
		counts.stagesNotEntitled += stage.installed && !stage.entitled;
		counts.stagesNotOwned += !stage.owned;
	}
	return counts;
}

std::vector<std::string> FitChoices(std::vector<std::string> names, const char* part, const std::string& fighter)
{
	const size_t given = names.size();
	names.resize(FittedCount(given));
	if (names.size() != given) {
		Log("content: %s has %zu %s names in the game's asset; Ember offers %zu", fighter.c_str(), given, part, names.size());
	}
	return names;
}

std::string ChoicesText(const FighterContent& fighter)
{
	const auto list = [](const char* part, const std::vector<std::string>& names) {
		std::string text = std::string(part) + " " + std::to_string(names.size()) + " (";
		for (size_t i = 0; i < names.size(); i++) text += (i ? ", \"" : "\"") + names[i] + "\"";
		return text + ")";
	};
	return list("V-Skills", fighter.vSkills) + ", " + list("V-Triggers", fighter.vTriggers);
}

int MarkUninstalled(ContentLists& lists, const std::vector<std::string>& codes)
{
	int cleared = 0;
	for (const std::string& code : codes) {
		const auto installed = [](const StageContent& stage) { return stage.installed; };
		const auto stage = std::find_if(lists.stages.begin(), lists.stages.end(),
			[&](const StageContent& s) { return s.code == code && s.installed; });
		if (stage == lists.stages.end() || std::count_if(lists.stages.begin(), lists.stages.end(), installed) < 2) continue;
		stage->installed = false;
		cleared++;
	}
	return cleared;
}

void MarkUnowned(ContentLists& lists, const std::vector<UnownedContent>& marks)
{
	for (const UnownedContent& mark : marks) {
		for (FighterContent& fighter : lists.fighters) {
			if (fighter.code != mark.fighter) continue;
			lists.ownership.markedUnowned++;
			if (mark.costume < 0) {
				fighter.owned = false;
				continue;
			}
			for (CostumeContent& costume : fighter.costumes) {
				if (costume.id != mark.costume) continue;
				if (mark.colour < 0) costume.owned = false;
				else {
					for (ColourContent& colour : costume.colours) {
						if (colour.id == mark.colour) colour.owned = false;
					}
				}
			}
		}
	}
}

} // namespace sfve::mod
