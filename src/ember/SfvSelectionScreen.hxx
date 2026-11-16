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
// pick (SfvPick's CurrentDummy): no Random card and no stage; each fighter
// has the look the dummy last had it in, as the player's pick does.
//
// The shell's other way to change the pick, the table page's Ultra and
// Appearance rows and the post-match prompt's Ultra, is StepTableSelection
// (SfvPickPages). What each page lists and does is SfvPickPages, its drawing
// SfvPickDraw; this class wires them to the menu and the pick.
//
// Render thread, inside the shell's frame.

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickPages.hxx"
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

private:
	// Shows the opening's pages, each on its focus.
	void Open(const pickpages::Opening& opening);
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

} // namespace sfve::adapter
