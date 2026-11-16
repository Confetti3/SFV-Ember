#include "mod/BackgroundPlay.hxx"

#include <doctest/doctest.h>

using sfve::mod::SelectGameWindow;
using sfve::mod::WindowCandidate;

TEST_CASE("the game's window is the engine's, not the splash screen's, however large the splash is")
{
	// The splash is up first and, on a small monitor setting, can be the larger.
	CHECK(SelectGameWindow({{L"SplashScreenClass", true, 900000}}) == -1);
	CHECK(SelectGameWindow({{L"SplashScreenClass", true, 900000}, {L"UnrealWindow", true, 400000}}) == 1);
	CHECK(SelectGameWindow({{L"UnrealWindow", true, 400000}, {L"SplashScreenClass", true, 900000}}) == 0);
}

TEST_CASE("an engine window that is hidden is passed by, and the largest visible one wins")
{
	CHECK(SelectGameWindow({}) == -1);
	CHECK(SelectGameWindow({{L"UnrealWindow", false, 900000}}) == -1);
	CHECK(SelectGameWindow({{L"UnrealWindow", true, 100}, {L"UnrealWindow", false, 900000}, {L"UnrealWindow", true, 2000}}) == 2);
	CHECK(SelectGameWindow({{L"unrealwindow", true, 100}}) == -1); // class names are exact
}
