#pragma once

// SFV Ember's Screen mode (docs/design/beta9/screen.md): how the game's
// screen is shown, chosen in Settings > Interface > Look and read by the
// launcher before it starts the game. Pure rules, shared by the launcher, the
// mod's settings row and the tests.
//
// - Game's setting (the default): the launcher passes no window option, so
//   SFV uses its own saved Screen Mode (GameUserSettings.ini FullscreenMode),
//   exclusive full screen included.
// - Window: -windowed, the game's saved resolution (what every start was
//   until beta.8).
// - Borderless: -windowed, then the mod takes the frame off the game's window
//   and lays it over the whole monitor it is on (overlay/BorderlessWindow);
//   the monitor's mode is never changed. The launcher tells the mod with
//   -sfve_screen=borderless.
//
// A test's own window option (--game-option -windowed, -fullscreen, -ResX=,
// -ResY= or -sfve_screen=) wins: the setting adds nothing then, so the test
// tools get the windows they ask for whatever the player's setting is.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::screen {

// The order is the library's (ui::kScreenModes, ShellView::screenMode).
enum class Mode { Game = 0, Window = 1, Borderless = 2 };

inline constexpr int kModes = 3;

// interface.json's key (the Background artwork's file, SfvBackdrop) and its
// values.
inline constexpr const char* kChoiceKey = "screenMode";

inline const char* Name(Mode mode)
{
	switch (mode) {
	case Mode::Window: return "window";
	case Mode::Borderless: return "borderless";
	default: return "game";
	}
}

inline std::optional<Mode> Parse(std::string_view name)
{
	for (const Mode mode : {Mode::Game, Mode::Window, Mode::Borderless}) {
		if (name == Name(mode)) return mode;
	}
	return std::nullopt;
}

inline std::optional<Mode> FromNumber(int number)
{
	if (number < 0 || number >= kModes) return std::nullopt;
	return static_cast<Mode>(number);
}

// The game's options for a chosen mode, and why.
struct Plan {
	std::vector<std::string> options; // to add to the game's command line
	bool testOwnsWindow = false;      // a test's option decided; the setting added nothing
};

// Whether a test option takes the window's mode or size out of the setting's
// hands.
inline bool OwnsWindow(const std::string& option)
{
	return option == "-windowed" || option == "-fullscreen" || option.rfind("-ResX=", 0) == 0 || option.rfind("-ResY=", 0) == 0 ||
		option.rfind("-sfve_screen=", 0) == 0;
}

inline Plan PlanFor(Mode chosen, const std::vector<std::string>& testOptions)
{
	Plan plan;
	for (const std::string& option : testOptions) plan.testOwnsWindow = plan.testOwnsWindow || OwnsWindow(option);
	if (plan.testOwnsWindow) return plan;
	switch (chosen) {
	case Mode::Window: plan.options = {"-windowed"}; break;
	case Mode::Borderless: plan.options = {"-windowed", "-sfve_screen=borderless"}; break;
	case Mode::Game: break;
	}
	return plan;
}

} // namespace sfve::screen
