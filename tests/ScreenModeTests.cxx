// SFV Ember's Screen mode (mod/ScreenMode.hxx, ember/SfvScreenMode.hxx,
// overlay/BorderlessWindow.hxx, docs/design/beta9/screen.md): the window
// options the launcher passes for each choice, the saved choice, and the
// rules Borderless applies to the game's window.

#include <doctest/doctest.h>

#include "ember/SfvScreenMode.hxx"
#include "mod/ScreenMode.hxx"
#include "overlay/BorderlessWindow.hxx"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using sfve::screen::Mode;
using Options = std::vector<std::string>;

std::filesystem::path FreshFolder(const char* name)
{
	const std::filesystem::path folder =
		std::filesystem::temp_directory_path() / (std::string("sfve-screen-") + name + "-" + std::to_string(GetCurrentProcessId()));
	std::filesystem::remove_all(folder);
	return folder;
}

void WriteFile(const std::filesystem::path& folder, const std::string& text)
{
	std::filesystem::create_directories(folder);
	std::ofstream(folder / "interface.json", std::ios::binary) << text;
}

nlohmann::json ReadFile(const std::filesystem::path& folder)
{
	std::ifstream in(folder / "interface.json", std::ios::binary);
	return nlohmann::json::parse(in);
}

} // namespace

TEST_CASE("the screen mode names, numbers and parsing agree")
{
	// The numbers are the library's row (ui::kScreenModes): 0 Game's setting,
	// 1 Window, 2 Borderless.
	CHECK(sfve::screen::kModes == 3);
	for (int number = 0; number < sfve::screen::kModes; number++) {
		const auto mode = sfve::screen::FromNumber(number);
		REQUIRE(mode.has_value());
		CHECK(static_cast<int>(*mode) == number);
		CHECK(sfve::screen::Parse(sfve::screen::Name(*mode)) == mode);
	}
	CHECK_FALSE(sfve::screen::FromNumber(-1).has_value());
	CHECK_FALSE(sfve::screen::FromNumber(3).has_value());
	CHECK_FALSE(sfve::screen::Parse("fullscreen").has_value());
	CHECK_FALSE(sfve::screen::Parse("").has_value());
	CHECK_FALSE(sfve::screen::Parse("Borderless").has_value()); // the file's words are lower case
}

TEST_CASE("the launcher passes the window options of the chosen mode")
{
	// Game's setting passes none: the game uses its own saved Screen Mode.
	CHECK(sfve::screen::PlanFor(Mode::Game, {}).options.empty());
	CHECK(sfve::screen::PlanFor(Mode::Window, {}).options == Options{"-windowed"});
	// Borderless is a window the mod then fits over the monitor.
	CHECK(sfve::screen::PlanFor(Mode::Borderless, {}).options == Options{"-windowed", "-sfve_screen=borderless"});
	CHECK_FALSE(sfve::screen::PlanFor(Mode::Borderless, {}).testOwnsWindow);
	// A test's other options say nothing of the window.
	const Options others = {"-sfve_background=1", "-sfve_pads=0", "-sfve_mode=booth"};
	CHECK(sfve::screen::PlanFor(Mode::Borderless, others).options == Options{"-windowed", "-sfve_screen=borderless"});
	CHECK(sfve::screen::PlanFor(Mode::Game, others).options.empty());
}

TEST_CASE("a test's own window option wins over every mode")
{
	// tools/sfvelib.py passes -windowed -ResX=N -ResY=N: the harnesses keep
	// their windowed copies at their sizes whatever the player chose.
	for (const Mode mode : {Mode::Game, Mode::Window, Mode::Borderless}) {
		for (const Options& test : {Options{"-windowed", "-ResX=960", "-ResY=540"}, Options{"-windowed"}, Options{"-ResX=960"},
				 Options{"-ResY=540"}, Options{"-fullscreen"}, Options{"-sfve_screen=borderless"}, Options{"-sfve_background=1", "-windowed"}}) {
			const sfve::screen::Plan plan = sfve::screen::PlanFor(mode, test);
			CHECK(plan.options.empty());
			CHECK(plan.testOwnsWindow);
		}
	}
	// Only whole options count: -windowedx is nobody's.
	CHECK_FALSE(sfve::screen::PlanFor(Mode::Window, {"-windowedx"}).testOwnsWindow);
}

TEST_CASE("the saved screen mode")
{
	SUBCASE("nothing saved: Game's setting, no problem")
	{
		const std::filesystem::path folder = FreshFolder("none");
		std::filesystem::create_directories(folder); // a missing folder is an error, never a fresh profile
		std::string problem = "left over";
		CHECK(sfve::adapter::ReadScreenMode(folder, problem) == Mode::Game);
		CHECK(problem.empty());
		WriteFile(folder, "{\"backgroundArtwork\": false}");
		CHECK(sfve::adapter::ReadScreenMode(folder, problem) == Mode::Game);
		CHECK(problem.empty());
		std::filesystem::remove_all(folder);
	}
	SUBCASE("each choice reads back, and the file's other choices stay")
	{
		const std::filesystem::path folder = FreshFolder("roundtrip");
		WriteFile(folder, "{\"backgroundArtwork\": false}");
		for (const Mode mode : {Mode::Borderless, Mode::Window, Mode::Game}) {
			std::string error, problem;
			REQUIRE(sfve::adapter::WriteScreenMode(folder, mode, error));
			CHECK(sfve::adapter::ReadScreenMode(folder, problem) == mode);
			CHECK(problem.empty());
			const nlohmann::json file = ReadFile(folder);
			CHECK(file.at("screenMode") == sfve::screen::Name(mode));
			CHECK(file.at("backgroundArtwork") == false);
		}
		std::filesystem::remove_all(folder);
	}
	SUBCASE("a folder with no file yet is made and written")
	{
		const std::filesystem::path folder = FreshFolder("fresh");
		std::string error, problem;
		REQUIRE(sfve::adapter::WriteScreenMode(folder / "settings", Mode::Borderless, error));
		CHECK(sfve::adapter::ReadScreenMode(folder / "settings", problem) == Mode::Borderless);
		std::filesystem::remove_all(folder);
	}
	SUBCASE("a value that is none of the three is Game's setting, with the problem named")
	{
		const std::filesystem::path folder = FreshFolder("bad-value");
		for (const char* text : {"{\"screenMode\": \"fullscreen\"}", "{\"screenMode\": 2}", "{\"screenMode\": true}", "{\"screenMode\": null}"}) {
			WriteFile(folder, text);
			std::string problem;
			CHECK(sfve::adapter::ReadScreenMode(folder, problem) == Mode::Game);
			CHECK(problem.find("screenMode") != std::string::npos);
		}
		std::filesystem::remove_all(folder);
	}
	SUBCASE("a file that is no settings object is Game's setting, and the next save writes a new one")
	{
		const std::filesystem::path folder = FreshFolder("garbled");
		for (const char* text : {"not json", "[1, 2]", ""}) {
			WriteFile(folder, text);
			std::string problem;
			CHECK(sfve::adapter::ReadScreenMode(folder, problem) == Mode::Game);
			CHECK_FALSE(problem.empty());
		}
		WriteFile(folder, "not json");
		std::string error, problem;
		REQUIRE(sfve::adapter::WriteScreenMode(folder, Mode::Window, error));
		CHECK(sfve::adapter::ReadScreenMode(folder, problem) == Mode::Window);
		CHECK(problem.empty());
		std::filesystem::remove_all(folder);
	}
}

TEST_CASE("Borderless takes the frame off and keeps the rest")
{
	namespace fit = sfve::overlay::fit;
	const LONG_PTR windowed = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS;
	const LONG_PTR borderless = fit::BorderlessStyle(windowed);
	CHECK((borderless & WS_CAPTION) == 0);
	CHECK((borderless & WS_THICKFRAME) == 0);
	CHECK((borderless & WS_MAXIMIZEBOX) == 0);
	// Kept: what makes it a visible window that Alt+F4 and the taskbar still reach.
	CHECK((borderless & WS_VISIBLE) != 0);
	CHECK((borderless & WS_CLIPSIBLINGS) != 0);
	CHECK((borderless & WS_SYSMENU) != 0);
	CHECK((borderless & WS_MINIMIZEBOX) != 0);
	CHECK(fit::BorderlessStyle(borderless) == borderless); // idempotent
	const LONG_PTR edged = WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_APPWINDOW | WS_EX_ACCEPTFILES;
	const LONG_PTR plain = fit::BorderlessExStyle(edged);
	CHECK((plain & (WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)) == 0);
	CHECK((plain & WS_EX_APPWINDOW) != 0);
	CHECK((plain & WS_EX_ACCEPTFILES) != 0);
}

TEST_CASE("a window fits Borderless when it has no frame and is exactly the monitor")
{
	namespace fit = sfve::overlay::fit;
	const RECT monitor = {0, 0, 2560, 1440};
	const LONG_PTR framed = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	const LONG_PTR bare = fit::BorderlessStyle(framed);
	CHECK(fit::Fits(bare, 0, monitor, monitor));
	CHECK_FALSE(fit::Fits(framed, 0, monitor, monitor));                    // the title bar is back
	CHECK_FALSE(fit::Fits((bare & ~WS_CAPTION) | WS_BORDER, 0, monitor, monitor)); // a thin border is a frame too
	CHECK_FALSE(fit::Fits(bare | WS_THICKFRAME, 0, monitor, monitor));
	CHECK_FALSE(fit::Fits(bare, WS_EX_CLIENTEDGE, monitor, monitor));
	// One pixel off any side: the engine moved or resized it.
	for (const RECT& moved : {RECT{1, 0, 2560, 1440}, RECT{0, 1, 2560, 1440}, RECT{0, 0, 2559, 1440}, RECT{0, 0, 2560, 1439}}) {
		CHECK_FALSE(fit::Fits(bare, 0, moved, monitor));
	}
	// A second monitor to the left has negative coordinates, and a window is
	// compared with its own monitor's rectangle.
	const RECT left = {-1920, 120, 0, 1200};
	CHECK(fit::Fits(bare, 0, left, left));
	CHECK_FALSE(fit::Fits(bare, 0, monitor, left));
	// The system menu, minimize box and clipping flags do not count.
	CHECK(fit::Fits(bare | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, 0, monitor, monitor));
}

TEST_CASE("Borderless does not fight a window that keeps restyling itself")
{
	namespace fit = sfve::overlay::fit;
	CHECK(fit::MayApply(1000, 0, 0));        // the first time, whenever
	CHECK(fit::MayApply(0, 0, 0));
	CHECK_FALSE(fit::MayApply(1100, 1000, 1)); // not within a quarter second of the last
	CHECK(fit::MayApply(1250, 1000, 1));
	CHECK(fit::MayApply(1250, 1000, 39));
	CHECK_FALSE(fit::MayApply(100000, 1000, 40)); // and not after the limit
	CHECK_FALSE(fit::MayApply(100000, 1000, 400));
}
