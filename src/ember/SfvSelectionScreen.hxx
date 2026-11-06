#pragma once

// SFV's pick screen inside Ember's shell (ember::ShellGame::DrawSelection):
// value rows of Ember's GameMenu over SFV's catalog, one each for the
// fighter, the costume, the colour and the stage, rather than SF4's select
// grid (FighterSelector), which is SF4's art and roster. The rows name
// everything as the game does (SfvCatalog), and the pane beside them shows
// the game's own pictures of the pick (SfvArt): the fighter's select-screen
// banner, the costume and colour by name with the colour's swatch, and the
// stage's picture while the stage row has the focus.
//
// Left and Right step a row and keep the change at once; Select on the
// fighter or the stage opens every choice. Back from the screen hands Close
// to the shell, as an embedded screen does (GameMenu's EmbeddedReturn), and
// so does Done. When the shell opened the screen for one change (the room's
// Fighter or Stage row), the screen starts on that row.
//
// Render thread, inside the shell's frame.

#include "ember/SfvPick.hxx"

#include "ui/GameMenu.hxx"

#include <string>
#include <vector>

namespace sfve::adapter {

class SfvSelectionScreen {
public:
	// One frame. `editable`: the pick may change now (the runtime's
	// canEditSelection); `stageEditable`: and so may the stage this player
	// offers (out of a room, or as P1). `error` says why the pick cannot be
	// readied, if it cannot. Writes a changed pick back to SfvPick.
	void Draw(bool editable, bool stageEditable, const std::string& error);

private:
	std::vector<sf4e::ui::MenuEntry> Rows(const PlayerPick& pick, bool editable, bool stageEditable) const;
	// The pick with the row's change made; false when the action changes nothing.
	static bool Apply(const sf4e::ui::MenuAction& action, PlayerPick& pick);
	// The pane beside the rows, for the focused row.
	static void DrawPreview(const PlayerPick& pick, const std::string& focus);

	sf4e::ui::GameMenu menu_;
	// The row the shell opened the screen for (its Fighter or Stage row), or
	// empty: a choice made on that row closes the screen again.
	std::string openOn_;
};

} // namespace sfve::adapter
