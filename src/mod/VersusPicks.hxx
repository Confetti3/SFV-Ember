#pragma once

// The launch's pick words (VersusPicks.cxx), for the "launch" command and a
// room's setup (ParseSetup): each false for text it does not take.

#include "mod/VersusLaunch.hxx"

#include <string>

namespace sfve::mod::pick_text {

// A fighter's or stage's code: up to 8 capital letters and digits, or "-" for none.
bool ParseCode(const std::string& text, std::wstring* code);
// "CODE[:costume[:colour[:vskill[:vtrigger]]]]" or "-".
bool ParsePick(const std::string& word, FighterPick* pick);
// A digit of PlayableRounds, or "-"/nothing for the default.
bool ParseRounds(const std::string& text, int* rounds);
// Seconds (RoundClockFrames) or "inf"; "-"/nothing for the default.
bool ParseTime(const std::string& text, int* frames);

} // namespace sfve::mod::pick_text
