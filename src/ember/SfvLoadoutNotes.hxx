#pragma once

// The short description of a V-Skill or V-Trigger the loadout cards show, in
// the player's language: what the move does and when to use it, in our own
// words (locales/loadout/<tag>.po, one catalog per language the Ember library
// declares, embedded by CMakeLists.txt).
//
// A note's key is the fighter's code, the part and the choice:
// "<CODE>.vs<k>" for V-Skill choice k and "<CODE>.vt<k>" for V-Trigger choice
// k, k = 1 for I and 2 for II ("RYU.vs1", "Z29.vt2"); 45 fighters times 4
// notes is 180 entries per catalog. Each is one or two lines, at most
// kMaxNoteLength characters.
//
// Lookup is the catalog of the active language (loc::Tag(loc::Active())),
// then English, then empty. The catalogs are parsed once with
// loc::testing::ParsePo. The card draws the note through NoteUserText
// (ui/Theme.hxx) so CJK glyphs load on demand.
//
// No game code. Stream D fills SfvLoadoutNotes.cxx and the catalogs; this
// header is final.

#include "ember/SfvPickSpace.hxx"

#include <optional>
#include <string>
#include <string_view>

namespace sfve::adapter {

// The longest a note is, in characters (not bytes): English is written to
// kMaxNoteLength, a translation may run to kMaxTranslatedNoteLength (the
// card wraps; the test holds every language to its limit).
inline constexpr int kMaxNoteLength = 140;
inline constexpr int kMaxTranslatedNoteLength = 160;

// "RYU.vs1": the catalog key of fighter `code`'s `part` `choice` (0 is I, so
// the key's number is choice + 1). Empty for an empty code or a choice outside
// 0 and 1.
std::string LoadoutNoteKey(std::string_view fighterCode, picks::LoadoutPart part, int choice);

// The note for the key above in the active language, then English; empty when
// neither has it. Any thread.
std::string LoadoutNoteFor(std::string_view fighterCode, picks::LoadoutPart part, int choice);

// The same for the catalog's fighter id (SfvCatalog's FighterCode): what the
// loadout cards ask. Empty for an id the catalog does not have.
std::string LoadoutNote(int fighter, picks::LoadoutPart part, int choice);

// The embedded catalog text (the .po file as written) for a language tag
// ("en", "ja", "zh-Hans"); none for a tag the library does not declare. For
// the tests, which check every language's entries.
std::optional<std::string> LoadoutCatalogText(std::string_view tag);

} // namespace sfve::adapter
