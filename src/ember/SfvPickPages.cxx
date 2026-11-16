#include "ember/SfvPickPages.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvEleven.hxx"
#include "ember/SfvLoadoutNotes.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelection.hxx"

#include "platform/Utf8.hxx"
#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "ui/AppearanceGrid.hxx"
#include "ui/MenuRows.hxx"
#include "ui/RowId.hxx"
#include "ui/SelectionLabels.hxx"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sfve::adapter::pickpages {

namespace {

using sf4e::ui::MenuEntry;
using sf4e::ui::SelectionStart;
namespace loc = sf4e::loc;

// A cell right of Random's column stands this many pitches further right:
// the room the Random tile takes beyond its own column (0.66 on the game's
// first row, 0.73 on its second).
constexpr float kRandomGap = .69f;

} // namespace

// A stage card's caption: the stage's name, tagged where the room bans it.
std::string StageCardLabel(const StageOption& option)
{
	std::string label = sf4e::ui::StageLabel(option.id);
	if (option.offer == StageOffer::Banned) label += " (" + std::string(loc::T("room.banned_stages.tag")) + ")";
	return label;
}

std::string OptionId(int id)
{
	return "#" + std::to_string(id);
}

std::optional<int> OptionOf(const std::string& option)
{
	if (option.size() < 2 || option[0] != '#') return std::nullopt;
	return std::atoi(option.c_str() + 1);
}

std::string CardId(const char* prefix, int number)
{
	return prefix + std::to_string(number);
}

bool Contains(const std::vector<int>& ids, int id)
{
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// The roster in the game's select-screen order (the catalog's rosterOrder).
std::vector<int> Roster()
{
	const ember::GameCatalog& catalog = ember::Catalog();
	return std::vector<int>(catalog.rosterOrder, catalog.rosterOrder + catalog.fighterCount);
}

bool FighterLocked(int fighter)
{
	return ember::ValidFighter(fighter) && ember::Catalog().fighterLocked(fighter, {});
}

// The loadout part a row stands for; none for any other row.
std::optional<picks::LoadoutPart> PartOfRow(const std::string& id)
{
	for (const LoadoutRow& row : kLoadoutRows) {
		if (id == row.id) return row.part;
	}
	return std::nullopt;
}

// A loadout card's id prefix ("vskill-"), and the part and choice a card id
// names; none for another id.
std::string PartPrefix(picks::LoadoutPart part)
{
	return std::string(part == picks::LoadoutPart::VSkill ? kVSkill : kVTrigger) + "-";
}

std::optional<PartChoice> PartCardOf(const std::string& id)
{
	for (const LoadoutRow& row : kLoadoutRows) {
		if (const std::optional<int> choice = sf4e::ui::RowKey<int>(id, PartPrefix(row.part))) return PartChoice{row.part, *choice};
	}
	return std::nullopt;
}

// The part's choice numbers the fighter has: 0, and 1 where it has two.
std::vector<int> PartNumbers(std::int64_t fighter, picks::LoadoutPart part)
{
	std::vector<int> numbers;
	for (int number = 0; number < PartChoices(fighter, part); number++) numbers.push_back(number);
	return numbers;
}

// The pick with the part's choice `number` stored; true when the loadout
// changed. Only a choice the fighter has is taken.
bool SetPart(picks::LoadoutPart part, int number, sf4e::selection::Pick& pick)
{
	const picks::Loadout before = picks::LoadoutOf(pick);
	if (!Contains(PartNumbers(pick.fighter, part), number)) return false;
	picks::Loadout after = before;
	after.choice[part] = (std::uint8_t)number;
	picks::SetLoadout(pick, after);
	return after != before;
}

// The colour a costume's card shows it in, and its colour cards open on: the
// colour worn where the costume has it and this player can ready it, else
// its first they can, else its first (sf4e::ui::GalleryColor).
int CardColour(const sf4e::selection::Pick& pick, int costume)
{
	const std::vector<int> ready = ColourIds(pick.fighter, costume, true);
	return sf4e::ui::GalleryColor(ready.empty() ? ColourIds(pick.fighter, costume) : ready, pick.color);
}

// The card on the board that is the stored pick: Eleven's while Eleven is
// the pick, Random's while Random is, else the fighter's.
std::string SavedCard(const PlayerPick& pick, bool random, bool eleven)
{
	if (eleven) return kElevenCard;
	return random ? std::string(kRandomCard) : CardId(kFighterCard, pick.pick.fighter);
}

const MenuEntry* EntryOf(const std::vector<MenuEntry>& entries, const std::string& id)
{
	const auto entry = std::find_if(entries.begin(), entries.end(), [&id](const MenuEntry& e) { return e.id == id; });
	return entry == entries.end() ? nullptr : &*entry;
}

std::vector<MenuEntry> BoardRows(const PlayerPick& pick, bool random, bool editable, bool fighterKept,
	bool withRandom, bool eleven)
{
	const ember::GameCatalog& catalog = ember::Catalog();
	const char* saves = editable ? "selection.select_saves" : "selection.locked_detail";
	std::vector<MenuEntry> rows;
	// Every fighter keeps their place, as the game's grid does: one this
	// player cannot play is shown and cannot be chosen. The stored fighter
	// can always be selected: it opens their look page.
	for (int id : Roster()) {
		const bool locked = catalog.fighterLocked(id, {});
		const bool choosable = editable && !locked && !fighterKept;
		const char* detail = locked ? "selection.not_owned" : fighterKept && id != pick.pick.fighter ? "selection.winner_keeps_fighter" : saves;
		MenuEntry row = sf4e::ui::Row(CardId(kFighterCard, id), catalog.fighterName(id), loc::T(detail), choosable || id == pick.pick.fighter);
		row.key = (std::uint64_t)id;
		if (choosable) row.hint = loc::T("menu.hint.save_fighter");
		rows.push_back(std::move(row));
	}
	// Random, in its own cell; stored, it opens the rolled fighter's page.
	if (withRandom) {
		const bool choosable = editable && !fighterKept;
		MenuEntry row = sf4e::ui::Row(kRandomCard, loc::T("selection.random_card"),
			loc::T(fighterKept ? "selection.winner_keeps_fighter" : editable ? "selection.random_card.detail" : "selection.locked_detail"),
			choosable || (random && !eleven));
		if (choosable) row.hint = loc::T("menu.hint.save_fighter");
		rows.push_back(std::move(row));
		// The game's ELEVEN, in its own cell: Random's pick in the Eleven
		// look (SfvEleven); stored, it opens the rolled fighter's page.
		MenuEntry card = sf4e::ui::Row(kElevenCard, ElevenName(),
			loc::T(fighterKept ? "selection.winner_keeps_fighter" : editable ? "selection.random_card.detail" : "selection.locked_detail"),
			choosable || eleven);
		if (choosable) card.hint = loc::T("menu.hint.save_fighter");
		rows.push_back(std::move(card));
	}
	return rows;
}

float BoardColumn(const grid::SelectGrid& table, grid::Cell cell)
{
	const bool pastRandom = table.random && cell.column > table.random->column;
	return (float)cell.column + grid::RowOffset(table, cell.row) + (pastRandom ? kRandomGap : 0.f);
}

void LinkRandom(std::vector<MenuEntry>& rows, const grid::SelectGrid& table)
{
	const auto random = std::find_if(rows.begin(), rows.end(), [](const MenuEntry& row) { return row.id == kRandomCard; });
	if (!table.random || random == rows.end()) return;
	const auto card = [&table](grid::Cell cell) {
		const std::optional<std::string> code = grid::CodeAt(table, cell);
		if (!code || *code == grid::kRandomCellCode) return std::string();
		const std::optional<int> id = FighterId(sf4e::platform::Utf8ToWide(code->c_str()));
		return id ? CardId(kFighterCard, *id) : std::string();
	};
	const auto link = [&rows](const std::string& id, std::string MenuEntry::*side) {
		for (MenuEntry& row : rows) {
			if (!id.empty() && row.id == id) row.*side = kRandomCard;
		}
	};
	const int column = table.random->column, top = table.random->row, last = (std::max)(top, grid::RowCount(table) - 1);
	// On Random's row, and on the last row, where a tile may stand in
	// Random's own column (the game's second row has one more on the left).
	random->left = card({column - 1, top});
	random->right = card({column + 1, top});
	link(random->left, &MenuEntry::right);
	link(random->right, &MenuEntry::left);
	if (last != top) {
		const std::string below = card({column, last});
		link(below.empty() ? card({column - 1, last}) : below, &MenuEntry::right);
		link(card({column + 1, last}), &MenuEntry::left);
	}
}

PlayerPick BoardPick(const PlayerPick& pick, const std::string& cardId, Whose whose)
{
	PlayerPick shown = pick;
	const std::optional<int> fighter = sf4e::ui::RowKey<int>(cardId, kFighterCard);
	if (fighter && ember::ValidFighter(*fighter) && *fighter != pick.pick.fighter) shown.pick = LookFor(pick.pick, *fighter, whose);
	return shown;
}

std::vector<MenuEntry> CostumeCards(const PlayerPick& pick, bool editable)
{
	const int fighter = pick.pick.fighter;
	const std::vector<int> ready = CostumeIds(fighter, true);
	std::vector<MenuEntry> rows;
	for (int costume : CostumeIds(fighter, false)) {
		const bool can = Contains(ready, costume);
		MenuEntry row = sf4e::ui::Row(CardId(kCostumeCard, costume), CostumeName(fighter, costume),
			!can ? std::string(loc::T("selection.not_owned")) : editable ? std::string() : std::string(loc::T("selection.locked_detail")),
			editable && can);
		row.key = (std::uint64_t)costume;
		rows.push_back(std::move(row));
	}
	return rows;
}

std::vector<MenuEntry> ColourCards(const PlayerPick& pick, int costume, bool editable)
{
	const int fighter = pick.pick.fighter;
	const std::vector<int> ready = ColourIds(fighter, costume, true);
	const std::string costumeName = CostumeName(fighter, costume);
	std::vector<MenuEntry> rows;
	for (int colour : ColourIds(fighter, costume)) {
		const bool can = Contains(ready, colour);
		MenuEntry row = sf4e::ui::Row(CardId(kColourCard, colour), ColourName(colour),
			!can ? std::string(loc::T("selection.not_owned")) : editable ? costumeName : std::string(loc::T("selection.locked_detail")),
			editable && can);
		row.key = (std::uint64_t)colour;
		if (row.enabled) row.hint = loc::T("menu.hint.save_color");
		rows.push_back(std::move(row));
	}
	if (rows.empty()) rows.push_back(sf4e::ui::Row("no-colors", loc::T("selection.no_colors"), costumeName, false));
	return rows;
}

std::vector<MenuEntry> LoadoutCards(const PlayerPick& pick, bool editable)
{
	const int fighter = pick.pick.fighter;
	std::vector<MenuEntry> rows;
	for (const picks::LoadoutPart part : picks::kLoadoutParts) {
		for (int choice : PartNumbers(fighter, part)) {
			MenuEntry row = sf4e::ui::Row(CardId(PartPrefix(part).c_str(), choice), ChoiceText(choice, MoveName(fighter, part, choice)),
				LoadoutNote(fighter, part, choice), editable);
			row.key = (std::uint64_t)LoadoutPictureNumber(part, choice);
			// The note is in the player's language (SfvLoadoutNotes).
			row.detailText = sf4e::ui::DetailText::Name;
			if (editable) row.hint = loc::T("menu.hint.save_ultra");
			rows.push_back(std::move(row));
		}
	}
	return rows;
}

std::vector<MenuEntry> StageCards(const PlayerPick& pick, bool editable, bool stageEditable,
	const std::vector<StageOption>& stageOptions)
{
	const bool choosable = editable && stageEditable;
	const char* refused = !editable ? "selection.locked_detail" : !stageEditable ? "selection.only_p1_stage" : nullptr;
	std::vector<MenuEntry> rows;
	for (const StageOption& option : stageOptions) {
		const bool lacking = option.offer == StageOffer::OpponentLacks, banned = option.offer == StageOffer::Banned;
		const char* detail = banned ? kStageBanned : lacking ? kStageNotShared : refused ? refused :
			option.id == picks::kRandomStage ? "selection.random_stage.detail" : nullptr;
		MenuEntry row = sf4e::ui::Row(CardId(kStageCard, option.id), StageCardLabel(option),
			detail ? std::string(loc::T(detail)) : std::string(), choosable && !lacking && !banned);
		row.key = (std::uint64_t)option.id;
		if (row.enabled) row.hint = loc::T("menu.hint.save_stage");
		rows.push_back(std::move(row));
	}
	(void)pick;
	return rows;
}

StageAnswer ChooseStage(const std::string& cardId, PlayerPick& pick, bool editable,
	bool stageEditable, const std::vector<StageOption>& stageOptions)
{
	const std::optional<int> stage = sf4e::ui::RowKey<int>(cardId, kStageCard);
	const auto option = std::find_if(stageOptions.begin(), stageOptions.end(),
		[&stage](const StageOption& candidate) { return stage && candidate.id == *stage; });
	if (option == stageOptions.end()) return StageAnswer::NotOffered;
	if (!editable) return StageAnswer::Locked;
	if (!stageEditable) return StageAnswer::OnlyP1;
	if (option->offer == StageOffer::Banned) return StageAnswer::Banned;
	if (option->offer == StageOffer::OpponentLacks) return StageAnswer::NotShared;
	if (pick.stage == option->id) return StageAnswer::Unchanged;
	pick.stage = option->id;
	return StageAnswer::Stored;
}

bool ChooseLoadout(const std::string& cardId, PlayerPick& pick)
{
	const std::optional<PartChoice> card = PartCardOf(cardId);
	return card && SetPart(card->part, card->choice, pick.pick);
}

Opening OpeningFor(SelectionStart start, const PlayerPick& pick, bool random, bool dummy,
	bool eleven)
{
	Opening opening;
	opening.pages.push_back({kBoard, SavedCard(pick, random && !dummy, eleven && !dummy)});
	const std::string vSkill = CardId(PartPrefix(picks::LoadoutPart::VSkill).c_str(), picks::LoadoutOf(pick.pick).choice[picks::VSkill]);
	switch (start) {
	case SelectionStart::Board:
		break;
	case SelectionStart::Roster:
		// The room's Fighter: a fighter (or Random) chosen on the board is it.
		opening.closesOnChoice = true;
		break;
	case SelectionStart::LookUltra:
		// A winner who keeps the fighter: their look page, on the loadout.
		opening.pages.push_back({kLook, kVSkill});
		break;
	case SelectionStart::Options:
		// SFV's fighters have no options of SF4's kind: the look page.
		opening.pages.push_back({kLook, kCostume});
		break;
	case SelectionStart::Costumes:
		opening.pages.push_back({kLook, kCostume});
		opening.pages.push_back({kCostumes, CardId(kCostumeCard, pick.pick.costume)});
		opening.closesOnChoice = true;
		break;
	case SelectionStart::Ultras:
		opening.pages.push_back({kLook, kVSkill});
		opening.pages.push_back({kLoadout, vSkill});
		break;
	case SelectionStart::Stage:
		if (dummy) break;
		opening.pages.push_back({kLook, kStage});
		opening.pages.push_back({kStagePage, CardId(kStageCard, pick.stage)});
		opening.closesOnChoice = true;
		break;
	}
	return opening;
}

} // namespace sfve::adapter::pickpages
