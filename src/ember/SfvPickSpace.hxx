#pragma once

// SFV's pick space: which fighters, costumes, colours and stages exist, as
// the game's spec data lists them (mod/GameContent's pick space, without
// names, installs or ownership), and SFV's rules over it. Every copy of the
// same game data has the same pick space, and the build a room checks carries
// its fingerprint, so two programs that hold it judge a pick alike: the game
// (SfvCatalog, which adds this copy's installs and ownership) and SFV's room
// host (roomhost/SfvHostCatalog, which holds it as data exported from the
// game). EMBER_LIBRARY.md sections 15 and 24.
//
// No game code: the room host builds it on Linux with g++ alone (C++17).

#include "common/SelectionPick.hxx"

#include <cstdint>
#include <string>
#include <vector>

namespace sfve::picks {

// The game build a room's matches are played on: the exe's build (BuildCheck's
// verified one) and the mod's match version. A room's build id adds the pick
// space's Fingerprint.
constexpr const char* kMatchBuild = "sfv-ember/13173604/0.2";

struct Costume {
	int id = 0;               // the game's costume number
	std::vector<int> colours; // the game's colour numbers for it, in its order
};

struct Fighter {
	std::string code;               // "RYU"
	std::vector<Costume> costumes;  // in the game's order
};

struct PickSpace {
	std::vector<Fighter> fighters;   // ids are positions here
	std::vector<std::string> stages; // codes; ids are positions here
};

// The table rules SFV plays: the room's round counts and times that
// VersusLaunch's PlayableRounds and RoomClockFrames accept, in the room's
// order (9999 s is the game's no time limit, and what makes a Practice room
// playable: ember::PlayableRoundTime(room::PracticeRoundTime)).
// sfve_ember_tests checks them against those two.
constexpr std::uint8_t kRoundCounts[] = {1, 3, 5, 7};
constexpr std::uint16_t kRoundTimes[] = {30, 60, 99, 300, 9999};

// Why the lists make no pick space, or empty when they do: no list is empty,
// the roster fits a room (ember::MaxFighters), every fighter has a costume and
// every costume a colour (the fitting's first of each), and every number fits
// the wire's byte (SelectionPick's ToNative).
std::string Problem(const PickSpace& space);

// 16 hex digits of a hash of the pick space: the fighters' codes, each
// fighter's costume numbers and each costume's colour numbers, and the
// stages' codes, all in order. Equal pick spaces give equal fingerprints.
std::string Fingerprint(const PickSpace& space);

// kMatchBuild, "/", and the Fingerprint: the build id a room's members share.
std::string BuildId(const PickSpace& space);

const Fighter* FighterAt(const PickSpace& space, std::int64_t fighter);
const Costume* CostumeAt(const PickSpace& space, std::int64_t fighter, int costume);
bool HasColour(const Costume& costume, int colour);

// A costume of the fighter, in one of that costume's colours, with no Ultra
// (SFV has one choice, 0) and no handicap (SFV's launch has none).
bool ValidPick(const PickSpace& space, const sf4e::selection::Pick& pick);
bool ValidStage(const PickSpace& space, std::int64_t stage);

// Each part of the pick SFV refuses becomes the game's first of its kind, in
// this order so each fits the one before: fighter 0, the fighter's first
// costume, that costume's first colour; no Ultra and no handicap. Only the
// pick space decides, so every copy fits a pick alike. True when it changed.
bool FitPick(const PickSpace& space, sf4e::selection::Pick& pick);

// One past the highest costume and colour number of any fighter (the numbers
// a room shows: GameCatalog's costumeLimit and colorLimit).
int CostumeLimit(const PickSpace& space);
int ColourLimit(const PickSpace& space);
// One past the fighter's highest costume number (GameCatalog's costumeCount):
// SFV's numbers have gaps, and every costume the spec has reads as known.
int CostumeCount(const PickSpace& space, int fighter);

// The pick space as the room host's data: {"build", "fighters": [{"code",
// "costumes": [{"id", "colours"}]}], "stages"}, with the build it was
// exported from.
std::string ToJson(const PickSpace& space, const std::string& build);
// The other way. False, with the reason in `error`, for text that is not
// such a document.
bool FromJson(const std::string& text, PickSpace& space, std::string& build, std::string& error);

} // namespace sfve::picks
