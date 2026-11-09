#pragma once

// SFV's pick screen inside Ember's shell (ember::ShellGame::DrawSelection):
// value rows of Ember's GameMenu over SFV's catalog, one each for the
// fighter, the costume, the colour, the V-Skill, the V-Trigger and the stage
// (EMBER_LIBRARY.md section 40), rather than SF4's select
// grid (FighterSelector), which is SF4's art and roster. The rows name
// everything as the game does (SfvCatalog), and the pane beside them
// (SfvPickPreview) shows the game's own pictures and words for the pick, or,
// while a choice is open, for the pick with its focused option: the
// fighter's banner, the costume and every colour of it as swatch chips, the
// V-Skill and V-Trigger with the game's marks, and the stage's card while
// the stage row has the focus (EMBER_LIBRARY.md section 43). The stage row
// offers Random and the stages this copy has; one the opponent lacks is
// listed but cannot be chosen, with the runtime's reason (sections 38 and
// 43).
//
// Left and Right step a row and keep the change at once; Select on any row
// opens every choice. Every fighter, costume and colour the game has is a
// choice; one this player cannot ready is shown dimmed and cannot be chosen,
// and while it has the focus the pane says why (SF4's NOT OWNED words, for
// what the game does not let them pick). Back from the screen hands Close
// to the shell, as an embedded screen does (GameMenu's EmbeddedReturn), and
// so does Done. When the shell opened the screen for one change (the room's
// Fighter or Stage row), the screen starts on that row; for the room's Ultra
// (the loadout) it starts on the V-Skill row. A fighter with one V-Skill or
// one V-Trigger shows that row, but it cannot change.
//
// The shell's other way to change the pick, the table page's Ultra and
// Appearance rows and the post-match prompt's Ultra, is StepTableSelection.
//
// Render thread, inside the shell's frame.

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickPreview.hxx"

#include "ui/ApplicationShell.hxx"
#include "ui/GameMenu.hxx"

#include <cstdint>
#include <string>
#include <vector>

namespace sfve::adapter {

class SfvSelectionScreen {
public:
	// `dummy`: the screen edits Training's dummy (SfvPick's CurrentDummy),
	// with no stage row, as SF4 Ember's dummy selector has none; otherwise
	// the player's pick.
	explicit SfvSelectionScreen(bool dummy = false) : dummy_(dummy) {}

	// One frame. `editable`: the pick may change now (the runtime's
	// canEditSelection); `stageEditable`: and so may the stage this player
	// offers (out of a room, or as P1). `error` says why the pick cannot be
	// readied, if it cannot. `unavailableStages` (the runtime's): the stages
	// the stage row cannot offer (SfvCatalog's StageOptions). Writes a
	// changed pick back to SfvPick.
	void Draw(bool editable, bool stageEditable, const std::string& error, const std::vector<std::int64_t>& unavailableStages);

	// What Draw shows and does, without drawing, so the pick screen's
	// journeys can be walked without a frame. The rows for `pick`, with the
	// stage row (`withStage`, the player's screen; the dummy's has none)
	// listing `stageOptions`.
	static std::vector<sf4e::ui::MenuEntry> Rows(const PlayerPick& pick, bool editable, bool withStage, bool stageEditable,
		const std::vector<StageOption>& stageOptions);
	// The pick with the row's change made; false when the action changes
	// nothing. A stage is one of `stageOptions`' PlayableIds.
	static bool Apply(const sf4e::ui::MenuAction& action, PlayerPick& pick, const std::vector<StageOption>& stageOptions);
	// What the pane shows for the row `rowId`.
	static PreviewFocus FocusOf(const std::string& rowId);
	// The pick as the pane shows it while the option `optionId` of the row's
	// open choice has the focus: that fighter (in its first look this player
	// can ready, and the loadout fitted to it), costume (keeping the colour
	// where it has it), colour, part's choice or stage. Nothing is fitted to
	// what the player can ready beyond that, so an option they cannot choose
	// is shown as it is. The pick itself for no option, or one that is not
	// the row's.
	static PlayerPick Previewed(const PlayerPick& pick, const std::string& rowId, const std::string& optionId);

private:
	// The pane's contents for the focused row `focus`, or with a choice open,
	// for its focused option: the pick previewed, and the option's detail
	// (why it cannot be chosen) as its note. Called while the menu draws the
	// pane: the navigation as it stands then, the rows this frame's.
	PickPreview PreviewOf(const PlayerPick& pick, const std::string& focus, const std::vector<sf4e::ui::MenuEntry>& rows,
		const std::vector<StageOption>& stageOptions) const;

	bool dummy_ = false;
	sf4e::ui::GameMenu menu_;
	// The row the shell opened the screen for (its Fighter or Stage row), or
	// empty: a choice made on that row closes the screen again.
	std::string openOn_;
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
