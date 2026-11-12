#pragma once

// SFV's pick screen inside Ember's shell (ember::ShellGame::DrawSelection),
// laid out as SF4 Ember's fighter select (the library's FighterSelector and
// AppearanceGallery, which are SF4's and not built for SFV) over SFV's
// catalog, pictures and words. Its pages, on the screen's own navigation
// (docs/design/beta8/select.md):
// - "home", the board: the game's slanted select tiles (CharaSelectIconLeft)
//   in the cells SFV's own select screen puts them in (SfvSelectGrid's
//   table, data/select-grid.json; the right side's tiles mirrored), the
//   Random tile between the two sides, and beside them the
//   art pane: the focused fighter in the look choosing them would store
//   (the fighter's remembered look), its names and colour chips. A fighter
//   this player cannot play keeps its place, dimmed, and the pane says why
//   (NOT OWNED). Select stores the fighter and opens its look page, or,
//   opened for the roster (the room's Fighter), closes again.
// - "look": value rows for the fighter, the costume, the colour, the costume
//   code, the V-Skill, the V-Trigger, the stage (EMBER_LIBRARY.md section
//   40) and Done, with the pick's preview beside them (SfvPickPreview). Left
//   and Right step a row and keep the change at once; Select on the Fighter
//   row goes to the board, on Costume or Colour to their cards, on V-Skill or
//   V-Trigger to the loadout cards, and on Stage to the stage cards; the
//   costume code (and a room's size) open their choices.
// - "costumes" and "colors": a card per costume, in the colour worn, and a
//   card per colour of the costume chosen; a costume opens its colours, a
//   colour stores the costume and the colour together. What this player
//   cannot ready is dimmed and cannot be chosen.
// - "loadout": a card per V-Skill and V-Trigger choice the fighter has: the
//   captured picture of the move, the game's I or II mark, the game's name
//   for the move and what it does (SfvLoadoutNotes). Select stores that
//   choice and the page stays.
// - "stage": Random and every stage this player is offered as cards; Select
//   or a click stores it. A stage the opponent lacks is dimmed and refused
//   with the runtime's reason; only P1 chooses (out of a room, or as P1 in
//   one), and anyone else is told so. A click on the look page's stage card
//   opens this page too.
// The screen opens where the shell asks (its SelectionStart; Opening), and
// opened for one change it closes once that change is made, or on Back from
// the page it opened on. Back from the board hands Close to the shell, as an
// embedded screen does (GameMenu's EmbeddedReturn), and so does Done.
//
// The dummy's screen (Training select's Dummy row) edits the dummy's own
// pick (SfvPick's CurrentDummy): no Random card, no stage, and no memory of
// looks.
//
// The shell's other way to change the pick, the table page's Ultra and
// Appearance rows and the post-match prompt's Ultra, is StepTableSelection.
//
// Render thread, inside the shell's frame.

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

namespace sfve::adapter {

class SfvSelectionScreen {
public:
	// `dummy`: the screen edits Training's dummy (SfvPick's CurrentDummy),
	// with no stage and no Random, as SF4 Ember's dummy selector has none;
	// otherwise the player's pick.
	explicit SfvSelectionScreen(bool dummy = false) : dummy_(dummy) {}

	// One frame. `editable`: the pick may change now (the runtime's
	// canEditSelection); `stageEditable`: and so may the stage this player
	// offers (out of a room, or as P1). `error` says why the pick cannot be
	// readied, if it cannot. `unavailableStages` (the runtime's): the stages
	// the stage row cannot offer (SfvCatalog's StageOptions). Writes a
	// changed pick back to SfvPick.
	// `tinyAllowed`: the room's Tiny fighters allowed rule is on, so the
	// screen shows the Size row (EMBER_LIBRARY.md section 59).
	// `fighterKept`: an Arcade room keeps this player, its last winner, on
	// their fighter (room::KeepsFighter): the board chooses no other.
	// `bannedStages`: the room's Banned stages (room::Snapshot::bannedStages,
	// ascending): the stage page lists them tagged and dimmed, with the reason,
	// and none can be chosen; Random stays choosable.
	void Draw(bool editable, bool stageEditable, const std::string& error, const std::vector<std::int64_t>& unavailableStages,
		bool tinyAllowed = false, bool fighterKept = false, const std::vector<std::int64_t>& bannedStages = {});

	// The page shown and its focus, for the shell's status.
	const std::string& Page() const { return menu_.navigation.Screen(); }
	const std::string& Focus() const { return menu_.navigation.Focus(); }

	// What Draw shows and does, without drawing, so the pick screen's
	// journeys can be walked without a frame. The rows for `pick`, with the
	// stage row (`withStage`, the player's screen; the dummy's has none)
	// listing `stageOptions`. `withSize`: the Size row (Normal or Tiny) after
	// the loadout's rows, for a room that allows tiny fighters.
	static std::vector<sf4e::ui::MenuEntry> Rows(const PlayerPick& pick, bool editable, bool withStage, bool stageEditable,
		const std::vector<StageOption>& stageOptions, bool withSize = false);
	// The pick with the row's change made; false when the action changes
	// nothing. A stage is one of `stageOptions`' PlayableIds.
	static bool Apply(const sf4e::ui::MenuAction& action, PlayerPick& pick, const std::vector<StageOption>& stageOptions);
	// What the pane shows for the row `rowId`.
	static PreviewFocus FocusOf(const std::string& rowId);
	// The pick as the pane shows it while the option `optionId` of the row's
	// open choice has the focus: that fighter or costume as choosing it would
	// store the pick (SfvCatalog's FitLookWithin), a colour, a part's choice
	// or a stage. A fighter or costume this player cannot ready is shown as it
	// is, fitted to the spec alone, and so is a colour or a stage they cannot
	// choose. The pick itself for no option, or one that is not the row's.
	static PlayerPick Previewed(const PlayerPick& pick, const std::string& rowId, const std::string& optionId);

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
	static PickPreview PreviewOf(const PlayerPick& pick, const std::string& focus, const std::optional<OpenChoice>& choice,
		const std::vector<sf4e::ui::MenuEntry>& rows, const std::vector<StageOption>& stageOptions);

	// The pages' ids.
	static constexpr const char* kBoard = "home";
	static constexpr const char* kLook = "look";
	static constexpr const char* kCostumes = "costumes";
	static constexpr const char* kColours = "colors";
	static constexpr const char* kLoadout = "loadout";
	static constexpr const char* kStagePage = "stage";

	// The look page's rows: Rows, with the rows that open a page made to
	// open it (Select activates them; Left and Right still step those that
	// step): Fighter (the board), Costume and Colour (their cards), V-Skill
	// and V-Trigger (the loadout cards, even where the fighter has one choice,
	// to read it), and Stage (the stage cards, for P2 too, to see who
	// chooses). `random`: the fighter row reads as selection.random_fighter
	// names it ("Random" and the fighter).
	static std::vector<sf4e::ui::MenuEntry> LookRows(const PlayerPick& pick, bool random, bool editable, bool withStage,
		bool stageEditable, const std::vector<StageOption>& stageOptions, bool withSize = false, bool eleven = false);

	// The board's cards: "fighter-<id>" for every fighter of the roster, in
	// the catalog's order (the grid places them), then, with `withRandom`,
	// "random-fighter" and the game's ELEVEN's card "eleven-fighter" (in its
	// own cell, SfvSelectGrid's `eleven`; `eleven`: Eleven is the stored pick,
	// and `random` is set as well). One this player cannot play, or any other
	// than the stored one while `fighterKept`, cannot be chosen; the stored
	// one can always be selected, which opens its look page.
	static std::vector<sf4e::ui::MenuEntry> BoardRows(const PlayerPick& pick, bool random, bool editable, bool fighterKept,
		bool withRandom, bool eleven = false);
	// The pick choosing the board's card `cardId` would store, for the art
	// pane: the fighter in their remembered look (`remembered`, the player's
	// screen) or in the pick's look (LookFor); the pick itself for Random's
	// card or any other id.
	static PlayerPick BoardPick(const PlayerPick& pick, const std::string& cardId, bool remembered);
	// Where the board puts a cell of the grid table across, in tile pitches
	// from row 0's column 0: its column, its row's offset and, right of
	// Random's column, the room the Random tile takes beyond its own column,
	// as the game's own select screen places them.
	static float BoardColumn(const grid::SelectGrid& table, grid::Cell cell);
	// The board's moves across the Random tile, as the game's cursor makes
	// them: Random's Left and Right are the tiles beside it on its own row,
	// and their Right and Left (and those of the tiles beside its foot on the
	// last row) are Random. Nothing without the Random card among `rows` (the
	// dummy's board).
	static void LinkRandom(std::vector<sf4e::ui::MenuEntry>& rows, const grid::SelectGrid& table);

	// The costume cards: "costume-<n>" for every costume of the fighter, in
	// the game's order, each shown in the colour worn where it has it and
	// this player can ready it (else its first they can); one with no colour
	// they can ready cannot be chosen, with NOT OWNED's words.
	static std::vector<sf4e::ui::MenuEntry> CostumeCards(const PlayerPick& pick, bool editable);
	// The colour cards of `costume`: "color-<n>" for each colour of it; one
	// this player cannot ready cannot be chosen.
	static std::vector<sf4e::ui::MenuEntry> ColourCards(const PlayerPick& pick, int costume, bool editable);
	// The loadout cards: "vskill-<n>" then "vtrigger-<n>" for each choice the
	// fighter has (n is the choice number, 0 for I), labelled in the game's
	// words, each card's detail its note (SfvLoadoutNotes).
	static std::vector<sf4e::ui::MenuEntry> LoadoutCards(const PlayerPick& pick, bool editable);
	// The stage cards: "stage-<id>" for Random first, then for every stage of
	// `stageOptions`. One the opponent lacks cannot be chosen, with the
	// runtime's reason; none can while the stage is not this player's to
	// choose (`stageEditable` false), each saying only P1 chooses.
	static std::vector<sf4e::ui::MenuEntry> StageCards(const PlayerPick& pick, bool editable, bool stageEditable,
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
	static StageAnswer ChooseStage(const std::string& cardId, PlayerPick& pick, bool editable, bool stageEditable,
		const std::vector<StageOption>& stageOptions);
	// What Select on a loadout card did to the pick: true when the part's
	// choice changed. A card for a choice the fighter lacks changes nothing.
	static bool ChooseLoadout(const std::string& cardId, PlayerPick& pick);

	// Where the screen opens for each way the shell opens it: the pages
	// stacked from the board, each with its focus, and whether the choice it
	// was opened for closes it again. The board's focus is the stored card
	// (Random's while Random is the pick). The dummy's screen has no stage
	// page and opens on its board for Stage.
	struct Opening {
		std::vector<std::pair<std::string, std::string>> pages; // page, focus; the board first
		bool closesOnChoice = false;
	};
	static Opening OpeningFor(sf4e::ui::SelectionStart start, const PlayerPick& pick, bool random, bool dummy, bool eleven = false);

private:
	// Shows the opening's pages, each on its focus.
	void Open(const Opening& opening);
	// The board's table from the embedded one, brought to the catalog's
	// roster (a fighter it lacks appended, and logged) once the catalog is
	// filled.
	void FitGrid();
	// What the frame's action on `screen` does: pages opened, the pick
	// (`pick`, as the frame showed it) stored, the screen closed.
	void Answer(const sf4e::ui::MenuAction& action, const std::string& screen, PlayerPick pick, bool random, bool eleven,
		bool editable, bool stageMine, bool fighterKept, const std::vector<StageOption>& stageOptions);
	bool dummy_ = false;
	sf4e::ui::GameMenu menu_;
	// Where the shell opened the screen.
	sf4e::ui::SelectionStart openOn_ = sf4e::ui::SelectionStart::Board;
	// The page it opened on for one change, and whether the choice made there
	// closes it; empty for none.
	std::string startPage_;
	bool closesOnChoice_ = false;
	// The costume whose colours the colour cards show: the one chosen on the
	// costume cards, stored only with a colour.
	int costume_ = 0;
	// The board's table, brought to the catalog's roster once it is filled.
	grid::SelectGrid grid_;
	bool gridFitted_ = false;
	// The focus the bodies last drew, so a focus the pad moved scrolls into view.
	std::string bodyFocus_;
	// Why the last Select on a stage card was refused, shown in the status
	// line while the stage page stays open; empty for none.
	std::string stageRefusal_;
	// The look page's stage card was clicked this frame.
	bool stageCardClicked_ = false;
};

// The table page's Ultra and Appearance rows, and the post-match prompt's
// Ultra, step this player's stored pick (SfvPick) by `step.delta`: the Ultra
// through the fighter's own loadouts in Ultra number order (LoadoutIndices),
// while the runtime lets it change (canChangeUltra, which holds over the WIN
// until the prompt is answered); the colour through the costume's colours
// this player can ready, while the pick may change (canEditSelection). False,
// and nothing stored, for a field that cannot change now.
bool StepTableSelection(const sf4e::ui::ShellAction::SelectionStep& step, bool canEditSelection, bool canChangeUltra);

} // namespace sfve::adapter
