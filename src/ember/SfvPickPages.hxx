#pragma once

// What SFV's pick screen (SfvSelectionScreen) shows and does, apart from
// drawing: each page's rows and cards, what Left, Right and Select do to the
// pick, what the art pane previews, and where the screen opens. Free
// functions in sfve::adapter::pickpages with no ImGui call in them, so the
// pick screen's journeys can be walked without a frame (the tests). The pages
// are described at the top of SfvSelectionScreen.hxx
// (docs/design/beta8/select.md).
//
// Render thread when the screen calls them; the tests call them from any.

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickPreview.hxx"
#include "ember/SfvSelectGrid.hxx"

#include "ui/ApplicationShell.hxx"
#include "ui/GameMenu.hxx"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sfve::adapter::pickpages {

// The pages' ids.
constexpr const char* kBoard = "home";
constexpr const char* kLook = "look";
constexpr const char* kCostumes = "costumes";
constexpr const char* kColours = "colors";
constexpr const char* kLoadout = "loadout";
constexpr const char* kStagePage = "stage";

// The rows' ids, and the options' ids of the fighter and stage choices.
constexpr const char* kFighter = "fighter";
constexpr const char* kCostume = "costume";
constexpr const char* kColour = "color";
// The game's costume codes (picks::kLoadCodes, EMBER_LIBRARY.md section 55).
constexpr const char* kCostumeCode = "costume-code";
constexpr const char* kVSkill = "vskill";
constexpr const char* kVTrigger = "vtrigger";
// The model size, Normal or Tiny (picks::kSizeNormal, kSizeTiny): a room that
// allows tiny fighters only (EMBER_LIBRARY.md section 59).
constexpr const char* kSize = "size";
constexpr const char* kStage = "stage";
constexpr const char* kDone = "done";

// The cards' ids: a prefix and the card's number (RowKey reads it back).
constexpr const char* kFighterCard = "fighter-";
constexpr const char* kRandomCard = "random-fighter";
constexpr const char* kElevenCard = "eleven-fighter";
constexpr const char* kCostumeCard = "costume-";
constexpr const char* kColourCard = "color-";
constexpr const char* kStageCard = "stage-";

// Why a stage the opponent lacks cannot be chosen: the runtime's own words,
// on the stage row's option and in the pane.
constexpr const char* kStageNotShared = "runtime.ready.stage_not_shared";
constexpr const char* kStageBanned = "selection.stage_banned"; // the room's Banned stages rule

// The loadout's rows, one per part, in SFV's own order.
struct LoadoutRow {
	const char* id;
	picks::LoadoutPart part;
};
constexpr LoadoutRow kLoadoutRows[] = {{kVSkill, picks::LoadoutPart::VSkill}, {kVTrigger, picks::LoadoutPart::VTrigger}};

// --- Ids and the lists the pages share ----------------------------------------

// An option's id ("#<number>") and the number it names; none for another id.
std::string OptionId(int id);
std::optional<int> OptionOf(const std::string& option);
// A card's id: its prefix and number.
std::string CardId(const char* prefix, int number);
bool Contains(const std::vector<int>& ids, int id);
// The roster in the game's select-screen order (the catalog's rosterOrder).
std::vector<int> Roster();
// A fighter this copy's player cannot play (the catalog's fighterLocked): not
// installed here, or (with Ember's ownership gate on) not theirs or with no
// look of theirs.
bool FighterLocked(int fighter);
// The row with this id, or none.
const sf4e::ui::MenuEntry* EntryOf(const std::vector<sf4e::ui::MenuEntry>& entries, const std::string& id);
// A stage card's caption: the stage's name, tagged where the room bans it.
std::string StageCardLabel(const StageOption& option);
// The card on the board that is the stored pick: Eleven's while Eleven is
// the pick, Random's while Random is, else the fighter's.
std::string SavedCard(const PlayerPick& pick, bool random, bool eleven = false);
// The colour a costume's card shows it in, and its colour cards open on: the
// colour worn where the costume has it and this player can ready it, else
// its first they can, else its first (sf4e::ui::GalleryColor).
int CardColour(const sf4e::selection::Pick& pick, int costume);

// The loadout part a row stands for; none for any other row.
std::optional<picks::LoadoutPart> PartOfRow(const std::string& id);
// A loadout card's id prefix ("vskill-"), and the part and choice a card id
// names; none for another id.
std::string PartPrefix(picks::LoadoutPart part);
struct PartChoice {
	picks::LoadoutPart part;
	int choice;
};
std::optional<PartChoice> PartCardOf(const std::string& id);
// The part's choice numbers the fighter has: 0, and 1 where it has two.
std::vector<int> PartNumbers(std::int64_t fighter, picks::LoadoutPart part);
// The pick with the part's choice `number` stored; true when the loadout
// changed. Only a choice the fighter has is taken.
bool SetPart(picks::LoadoutPart part, int number, sf4e::selection::Pick& pick);

// --- The look page and what the pane previews -----------------------------------

// What Draw shows and does, without drawing. The rows for `pick`, with the
// stage row (`withStage`, the player's screen; the dummy's has none)
// listing `stageOptions`. `withSize`: the Size row (Normal or Tiny) after
// the loadout's rows, for a room that allows tiny fighters.
std::vector<sf4e::ui::MenuEntry> Rows(const PlayerPick& pick, bool editable, bool withStage, bool stageEditable,
	const std::vector<StageOption>& stageOptions, bool withSize = false);
// The pick with the row's change made; false when the action changes
// nothing. A stage is one of `stageOptions`' PlayableIds. A new fighter is
// in its own look (LookFor, `remembered` as it takes it).
bool Apply(const sf4e::ui::MenuAction& action, PlayerPick& pick, const std::vector<StageOption>& stageOptions,
	bool remembered = true);
// What the pane shows for the row `rowId`.
PreviewFocus FocusOf(const std::string& rowId);
// The pick as the pane shows it while the option `optionId` of the row's
// open choice has the focus: that fighter or costume as choosing it would
// store the pick (SfvCatalog's FitLookWithin), a colour, a part's choice
// or a stage. A fighter or costume this player cannot ready is shown as it
// is, fitted to the spec alone, and so is a colour or a stage they cannot
// choose. The pick itself for no option, or one that is not the row's.
PlayerPick Previewed(const PlayerPick& pick, const std::string& rowId, const std::string& optionId, bool remembered = true);

// A choice open on the screen: its row, and the place of its focused
// option in the row's choices.
struct OpenChoice {
	std::string row;
	std::size_t option = 0;
};
// The pane's contents for the focused row `focus`, or with `choice` open,
// for its focused option among this frame's `rows`: the pick previewed
// (Previewed), and as its note why a fighter, costume or colour option
// cannot be chosen (its detail) or that the stage shown is one the
// opponent lacks (`stageOptions`' OpponentLacks), with the stage row
// focused or its choice open.
PickPreview PreviewOf(const PlayerPick& pick, const std::string& focus, const std::optional<OpenChoice>& choice,
	const std::vector<sf4e::ui::MenuEntry>& rows, const std::vector<StageOption>& stageOptions, bool remembered = true);

// The look page's rows: Rows, with the rows that open a page made to
// open it (Select activates them; Left and Right still step those that
// step): Fighter (the board), Costume and Colour (their cards), V-Skill
// and V-Trigger (the loadout cards, even where the fighter has one choice,
// to read it), and Stage (the stage cards, for P2 too, to see who
// chooses). `random`: the fighter row reads as selection.random_fighter
// names it ("Random" and the fighter).
std::vector<sf4e::ui::MenuEntry> LookRows(const PlayerPick& pick, bool random, bool editable, bool withStage, bool stageEditable,
	const std::vector<StageOption>& stageOptions, bool withSize = false, bool eleven = false);

// --- The board and the cards ------------------------------------------------------

// The board's cards: "fighter-<id>" for every fighter of the roster, in
// the catalog's order (the grid places them), then, with `withRandom`,
// "random-fighter" and the game's ELEVEN's card "eleven-fighter" (in its
// own cell, SfvSelectGrid's `eleven`; `eleven`: Eleven is the stored pick,
// and `random` is set as well). One this player cannot play, or any other
// than the stored one while `fighterKept`, cannot be chosen; the stored
// one can always be selected, which opens its look page.
std::vector<sf4e::ui::MenuEntry> BoardRows(const PlayerPick& pick, bool random, bool editable, bool fighterKept, bool withRandom,
	bool eleven = false);
// The pick choosing the board's card `cardId` would store, for the art
// pane: the fighter in their remembered look (`remembered`, the player's
// screen) or in the pick's look (LookFor); the pick itself for Random's
// card or any other id.
PlayerPick BoardPick(const PlayerPick& pick, const std::string& cardId, bool remembered);
// Where the board puts a cell of the grid table across, in tile pitches
// from row 0's column 0: its column, its row's offset and, right of
// Random's column, the room the Random tile takes beyond its own column,
// as the game's own select screen places them.
float BoardColumn(const grid::SelectGrid& table, grid::Cell cell);
// The board's moves across the Random tile, as the game's cursor makes
// them: Random's Left and Right are the tiles beside it on its own row,
// and their Right and Left (and those of the tiles beside its foot on the
// last row) are Random. Nothing without the Random card among `rows` (the
// dummy's board).
void LinkRandom(std::vector<sf4e::ui::MenuEntry>& rows, const grid::SelectGrid& table);

// The costume cards: "costume-<n>" for every costume of the fighter, in
// the game's order, each shown in the colour worn where it has it and
// this player can ready it (else its first they can); one with no colour
// they can ready cannot be chosen, with NOT OWNED's words.
std::vector<sf4e::ui::MenuEntry> CostumeCards(const PlayerPick& pick, bool editable);
// The colour cards of `costume`: "color-<n>" for each colour of it; one
// this player cannot ready cannot be chosen.
std::vector<sf4e::ui::MenuEntry> ColourCards(const PlayerPick& pick, int costume, bool editable);
// The loadout cards: "vskill-<n>" then "vtrigger-<n>" for each choice the
// fighter has (n is the choice number, 0 for I), labelled in the game's
// words, each card's detail its note (SfvLoadoutNotes).
std::vector<sf4e::ui::MenuEntry> LoadoutCards(const PlayerPick& pick, bool editable);
// The stage cards: "stage-<id>" for Random first, then for every stage of
// `stageOptions`. One the opponent lacks cannot be chosen, with the
// runtime's reason; none can while the stage is not this player's to
// choose (`stageEditable` false), each saying only P1 chooses.
std::vector<sf4e::ui::MenuEntry> StageCards(const PlayerPick& pick, bool editable, bool stageEditable,
	const std::vector<StageOption>& stageOptions);

// What Select on a stage card did.
enum class StageAnswer {
	Stored,     // the pick's stage is now that card's
	Unchanged,  // it already was
	Locked,     // the pick cannot change now
	OnlyP1,     // the stage is not this player's to choose
	NotShared,  // the opponent lacks it
	Banned,     // the room bans it
	NotOffered, // no card of the page (not a stage this player is offered)
};
StageAnswer ChooseStage(const std::string& cardId, PlayerPick& pick, bool editable, bool stageEditable,
	const std::vector<StageOption>& stageOptions);
// What Select on a loadout card did to the pick: true when the part's
// choice changed. A card for a choice the fighter lacks changes nothing.
bool ChooseLoadout(const std::string& cardId, PlayerPick& pick);

// Where the screen opens for each way the shell opens it: the pages
// stacked from the board, each with its focus, and whether the choice it
// was opened for closes it again. The board's focus is the stored card
// (Random's while Random is the pick). The dummy's screen has no stage
// page and opens on its board for Stage.
struct Opening {
	std::vector<std::pair<std::string, std::string>> pages; // page, focus; the board first
	bool closesOnChoice = false;
};
Opening OpeningFor(sf4e::ui::SelectionStart start, const PlayerPick& pick, bool random, bool dummy, bool eleven = false);

} // namespace sfve::adapter::pickpages

namespace sfve::adapter {

// The table page's Ultra and Appearance rows, and the post-match prompt's
// Ultra, step this player's stored pick (SfvPick) by `step.delta`: the Ultra
// through the fighter's own loadouts in Ultra number order (LoadoutIndices),
// while the runtime lets it change (canChangeUltra, which holds over the WIN
// until the prompt is answered); the colour through the costume's colours
// this player can ready, while the pick may change (canEditSelection). False,
// and nothing stored, for a field that cannot change now.
bool StepTableSelection(const sf4e::ui::SelectionStep& step, bool canEditSelection, bool canChangeUltra);

} // namespace sfve::adapter
