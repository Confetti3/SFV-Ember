// Who keeps the keyboard from SFV while Ember's shell is hidden (SfvShell's
// KeepsKeyFromGame): a replay's controls while it plays, and every key press
// at SFV's own main menu while a room is joined, as the pads are kept there.

#include <doctest/doctest.h>

#include "ember/SfvShell.hxx"

#include <windows.h>

#include <initializer_list>

using sfve::adapter::KeepsKeyFromGame;
using sfve::adapter::KeyboardHolders;

TEST_CASE("the game has every key while nothing of Ember's holds the keyboard")
{
	const KeyboardHolders none;
	for (const unsigned key : {unsigned(VK_RETURN), unsigned(VK_ESCAPE), unsigned(VK_UP), unsigned(VK_F1), unsigned('A')}) {
		CHECK_FALSE(KeepsKeyFromGame(none, WM_KEYDOWN, key));
		CHECK_FALSE(KeepsKeyFromGame(none, WM_KEYUP, key));
	}
}

TEST_CASE("in a room the game's main menu gets no key press, and the releases still reach it")
{
	KeyboardHolders locked;
	locked.menuLocked = true;
	// The keys SFV's menus move and choose with, and the ones a player has set
	// for the fight: none goes down at the locked menu.
	for (const unsigned key : {unsigned(VK_RETURN), unsigned(VK_ESCAPE), unsigned(VK_UP), unsigned(VK_DOWN), unsigned(VK_LEFT),
		     unsigned(VK_RIGHT), unsigned(VK_SPACE), unsigned('U'), unsigned('J')}) {
		CHECK(KeepsKeyFromGame(locked, WM_KEYDOWN, key));
		// A key held from before the lock is let go in the game too.
		CHECK_FALSE(KeepsKeyFromGame(locked, WM_KEYUP, key));
	}
	// Alt's system keys are not plain key messages: Alt+F4 still closes the game.
	CHECK_FALSE(KeepsKeyFromGame(locked, WM_SYSKEYDOWN, VK_F4));
}

TEST_CASE("a replay's controls are Ember's while it plays, pressed and released")
{
	KeyboardHolders playback;
	playback.playback = true;
	for (const unsigned key : {unsigned(VK_F1), unsigned(VK_F2), unsigned(VK_F3), unsigned(VK_F4), unsigned(VK_ESCAPE),
		     unsigned(VK_OEM_COMMA), unsigned(VK_LEFT), unsigned(VK_RIGHT), unsigned(VK_HOME), unsigned('L')}) {
		CHECK(KeepsKeyFromGame(playback, WM_KEYDOWN, key));
		CHECK(KeepsKeyFromGame(playback, WM_KEYUP, key));
	}
	// Any other key is the game's.
	CHECK_FALSE(KeepsKeyFromGame(playback, WM_KEYDOWN, VK_F5));
	CHECK_FALSE(KeepsKeyFromGame(playback, WM_KEYDOWN, VK_RETURN));
	CHECK_FALSE(KeepsKeyFromGame(playback, WM_SYSKEYDOWN, VK_F4));
	// Nothing plays: the controls are the game's keys again.
	CHECK_FALSE(KeepsKeyFromGame(KeyboardHolders{}, WM_KEYDOWN, VK_F1));
}
