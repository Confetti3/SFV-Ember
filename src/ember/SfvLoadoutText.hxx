#pragma once

// SFV's words for a loadout (SfvPickSpace's Loadout: a V-Skill and a
// V-Trigger) in the game's language: its row labels ID_SYS_VSE_0002
// (V-Skill) and ID_SYS_VSE_0001 (V-Trigger), and its choice patterns
// ID_SYS_VSE_0004 and _0005 (choice I and II, with the move's name as their
// argument "common1"), which its select and VS screens build (rva 0x6D3130,
// 0x76AFD0, 0x72E960). Ember reads them once at the main menu
// (ReadLoadoutWords), as it reads the league names (EMBER_LIBRARY.md section
// 28); until then, and for a key the game has no text for, the words are the
// game's English ones. The game's own two labels differ in case ("V-Skill",
// "V-TRIGGER"), so Ember sets both in its rows' case (RowCase): "V-Skill" and
// "V-Trigger", as "Fighter", "Costume" and "Color" read beside them, and as
// Ember's own text writes them. Nothing changes case when the game's words
// arrive. The move names are the catalog's (SfvCatalog's MoveName).
//
// The library has no words of its own for these, and SFV may overlay only
// the library's own messages, so the pick screen's rows and the shared
// screens' one combined value (the Ultra hooks in SfvSelection.cxx) both
// take these. EMBER_LIBRARY.md section 40.

#include "ember/SfvPickSpace.hxx"

#include <cstdint>
#include <string>

namespace sfve::adapter {

// Game thread at the main menu: reads the game's words once, logs them, and
// publishes them for every thread. Later calls do nothing.
void ReadLoadoutWords();

// The game's label for the part, in Ember's case ("V-Skill", "V-Trigger" in
// English).
const char* PartLabel(picks::LoadoutPart part);
// A label in the case of Ember's rows: each word's first letter a capital,
// the rest small ("V-TRIGGER" reads "V-Trigger"). A label with any letter
// outside ASCII (Japanese's, an accented capital) is kept as written: its
// case is not Ember's to guess.
std::string RowCase(std::string label);
// A choice as the game's select screen words it: its pattern for that
// number with the move's name (FormatChoice), the numeral picks::Numeral.
std::string ChoiceText(int number, const std::string& move);
// The pattern with "{common1}" made `move`; without that argument, the
// pattern (or the numeral, for no pattern) and the move after it. Where
// there is no move, the numeral alone: the pattern would show its argument's
// place empty. A pattern with any other argument left once "{common1}" is
// filled is not shown either: the numeral and the move are, and the pattern
// is logged once.
std::string FormatChoice(const std::string& pattern, const char* numeral, const std::string& move);

// The loadout at an Ultra number (picks::LoadoutAt) as the shared screens
// name it, "V-Skill II · V-Trigger I", and Home's short form, "II/I"; empty
// for a number that is no loadout. Any thread; each text lives for the
// process's life.
const char* LoadoutLabel(int index);
const char* ShortLoadoutLabel(int index);
// The fighter's moves for the loadout, "Mind's Eye · Kakko Fuden"; a part
// the game gave no name is its label and numeral ("V-Trigger II").
std::string LoadoutMoves(std::int64_t fighter, picks::Loadout loadout);
// The table page's value: LoadoutLabel, then the moves where the game named
// any: "V-Skill II · V-Trigger I: Mind's Eye · Kakko Fuden".
std::string LoadoutName(std::int64_t fighter, picks::Loadout loadout);

} // namespace sfve::adapter
