#pragma once

// The fighters, their costumes and colours, and the stages the game offers,
// read from its own tables for the overlay's pickers and Ember's catalog.
// Codes are the game's ("RYU", "S01"); costume and colour numbers are the
// game's own (what a battle's setup takes, and what the native costume
// settings menu lists).
//
// Two kinds of fact, kept apart because two copies of the game must agree on
// the first and need not on the second:
// - The pick space: which fighters, costumes, colours and stages exist. It
//   comes from the game's spec data (DA_Spec) and fighter table
//   (DA_CharaEntry), never from what this copy has installed or what its
//   account's entitlement cache holds: every fighter, costume and colour with
//   a CharaSpec row (sfv::spec, found with the game's own lookups,
//   sfv::kSpecFighterSet and the others), and every stage with a StageSpec
//   row (sfv::spec::kStages). Each is a number or a code: `id` and `code`
//   below, all that SfvCatalog's PickSpaceOf takes. EMBER_LIBRARY.md section
//   41.
// - This copy's own: whether its install has a fighter's, a costume's or a
//   stage's package (`installed`), what its account's entitlement cache
//   grants (`entitled`: the account's own answer, kept apart from the game's
//   now that SFV grants every player everything, EntitlementGrant and
//   EMBER_LIBRARY.md section 57), what the game itself grants (`granted`),
//   what this copy's player may pick (`owned`: the game's answer,
//   sfv::kOwnedFighters and the rest, limited to what the account's own
//   cache would let them pick, LimitToAccount, for fighters; under
//   kContentOwnership for looks and stages), and the names, in the game's
//   language.
//
// Stage names are the stage's data asset's FText (sfv::kStageName); fighter
// names the fighter's PersonalData FText; costume names the spec's (where it
// has none, the game's own "Costume<n>"). EMBER_LIBRARY.md section 15.
//
// How many V-Skills and V-Triggers a fighter has, and their names, come from
// the game's name assets (sfv::choices), as its select and VS screens count
// and name them; the count is part of the pick space (section 40).
//
// The rules over the lists that read no game memory (the codes' comparison,
// ColourOf and ColourNumbers, ApplyContentOwnership and its names,
// CountContent, FitChoices and ChoicesText, MarkUnowned and MarkUninstalled)
// are ContentRules.cxx's; GameContent.cxx reads the game.

#include "mod/ModConfig.hxx"

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::mod {

// Two of the game's codes ("RYU", L"S01") are the same as the game compares
// its FStrings: ignoring case. The one comparison for codes, narrow or wide:
// the content reads and the launch's read-back (VersusLaunch's LookMismatch)
// both ask it.
bool SameCode(std::wstring_view a, std::wstring_view b);
bool SameCode(std::string_view a, std::string_view b);

// Which looks (a costume in one of its colours) and which stages this copy's
// player may pick. The opponent's look always plays as picked, and so does
// the stage the room's P1 chose, whatever this copy's answer: only that this
// copy has it installed is asked (EMBER_LIBRARY.md sections 23, 38 and 41).
// Fighters keep the game's answer (sfv::kOwnedFighters) under either.
enum class ContentOwnership {
	// As the game's menus offer them. Looks: the game's own answer
	// (sfv::kOwnedCostumes, kOwnedColours), those the account's entitlement
	// cache grants and this copy has installed, or, while the battle setup
	// asset restricts the picks (OwnershipSource::restricted), the asset's
	// own list; on a copy whose cache lacks a costume, that costume shows as
	// not owned. Stages: those installed here whose StageSpec row's two codes
	// the cache grants (StageContent::entitled), the stages SFV Ember offered
	// before its stage universe became the spec's rows.
	Entitlements,
	// Every look of a costume this copy has installed, and every stage
	// installed here, whatever the cache holds. While the battle setup asset
	// restricts the picks, the game's answer still decides the looks.
	Installed,
};

// The one switch for that rule, applied by ApplyContentOwnership as the
// content lists are read.
constexpr ContentOwnership kContentOwnership = ContentOwnership::Entitlements;

// One colour of a costume: its number (the pick space's) and this copy's
// facts about it.
struct ColourContent {
	int id = 0; // the game's colour number: 0-99, then 100 and up for the EX colours
	// This copy's player may pick the costume in this colour, under
	// kContentOwnership. A look is theirs only with the costume's own `owned`
	// as well (SfvCatalog's LookOwned).
	bool owned = false;
	// The account's entitlement cache grants the colour's code
	// (RefColorAvailableCode): the account's own answer (EntitlementGrant's
	// AccountGrants), never the grant the game now gets. Reported, never
	// gated on directly.
	bool entitled = false;
	// The game itself grants it (sfv::spec::kEntitled): everything while
	// SFV's grant is on (EntitlementGrant). Reported only.
	bool granted = false;
	// The colour's two swatch colours (the costume settings menu's) are not
	// read with the lists: the game reads them from the costume's
	// CostumeColorPreview data asset, which it finds or loads first
	// (sfv::kColourSwatch), a load per costume that the lists' one read at
	// the main menu cannot afford for all 875. SfvArt reads them a costume a
	// frame (mod::ColourSwatch, GameArt.hxx).
};

struct CostumeContent {
	int id = 0; // the game's costume number: its place in the fighter's CostumeDatas
	// Every colour with a CharaSpec row, in the game's order: 0-99, then the
	// EX colours.
	std::vector<ColourContent> colours;
	std::string name; // UTF-8, display only
	// This copy has the costume's package (sfv::kCostumePackage, the test the
	// battle's own load makes).
	bool installed = false;
	// This copy's player may pick it, under kContentOwnership.
	bool owned = false;
	// The account's entitlement cache grants the costume's code
	// (RefCostumeAvailableCode; the account's own answer, as a colour's).
	// Reported, never gated on directly.
	bool entitled = false;
	// The game itself grants it (sfv::spec::kEntitled). Reported only.
	bool granted = false;
};

// The costume's colour `id`; null for a colour it does not have.
const ColourContent* ColourOf(const CostumeContent& costume, int id);
// The costume's colour numbers, in order: all the pick space keeps of them.
std::vector<int> ColourNumbers(const CostumeContent& costume);

struct FighterContent {
	std::string code;
	std::vector<CostumeContent> costumes; // in the game's order
	std::string name;                     // UTF-8, display only; empty where the game has none
	bool installed = false;               // this copy has the fighter's package (sfv::kFighterInstalled)
	// The game lets this copy's player pick the fighter (sfv::kOwnedFighters)
	// and so would the account's own cache (LimitToAccount).
	bool owned = false;
	// The fighter's V-Skills and V-Triggers, I first, by the game's names for
	// them (UTF-8, display only; empty where the game has none). How many
	// there are is part of the pick space; the names are not. Always one or
	// two (FitChoices); one unnamed until read.
	std::vector<std::string> vSkills{std::string()};
	std::vector<std::string> vTriggers{std::string()};
	// The account's entitlement cache grants the fighter's RefAvailableCode
	// (the account's own answer, as a colour's). Reported only.
	bool entitled = false;
	// The account's entitlement cache grants its RefVisibleCode, which the
	// game's fighter lists need as well (sfv::spec::kFighterVisible).
	bool visible = false;
	// The game itself grants its RefAvailableCode (sfv::spec::kEntitled).
	// Reported only.
	bool granted = false;
};

// Where `owned` came from, for the log and the status.
struct OwnershipSource {
	// The battle setup asset was there and its lists were read. Lists are
	// only ever given with it read: without the asset ReadContentLists gives
	// none, so the catalog is not filled and Ember's runtime waits.
	bool read = false;
	bool restricted = false;    // the asset's +96: picks limited to its own list
	int restrictionEntries = 0; // that list's length
	bool filter = false;        // a filter object installed at the asset's +280
	bool loggedIn = false;      // the online identity's status (offline: false)
	// Content the command line marks unowned for this copy
	// (-sfve_unowned, ModConfig), applied on top of the game's answer.
	int markedUnowned = 0;
	// The rule that made the looks' `owned` (ApplyContentOwnership): Installed
	// only where it applied, Entitlements (the game's answer) otherwise.
	ContentOwnership looks = ContentOwnership::Entitlements;
	// The rule that made the stages' `owned`: always the switch's, since no
	// asset restriction applies to stages.
	ContentOwnership stages = ContentOwnership::Entitlements;
};

// One stage of the universe: every stage with a StageSpec row (less Random
// and those without stage data), the same stages in the same order on every
// copy of the build, whatever it has installed or its account holds.
struct StageContent {
	std::string code;       // "S01"
	std::string name;       // UTF-8, display only; empty where the game has none
	bool installed = false; // this copy has the stage's package (sfv::spec::kStageInstalled)
	// The stage's package is here AND the account's entitlement cache grants
	// both of its StageSpec row's codes: the game's own selectable flag
	// (sfv::spec::kStageSelectable) as the account's own answer would set
	// it, whatever the game now grants.
	bool entitled = false;
	// This copy's player may choose it, under kContentOwnership: their stage
	// row, the fitting of their stored stage and the Random their copy rolls
	// as P1 take only these (SfvCatalog's StageOffered). Never asked of the
	// room's stage: one P1 chose plays on any copy that has it installed.
	bool owned = false;
	// The game's own selectable flag (sfv::spec::kStageSelectable): every
	// installed stage while SFV's grant is on. Reported only.
	bool granted = false;
};

struct ContentLists {
	std::vector<FighterContent> fighters; // in the fighter table's order, the game's select screen's
	// The universe, in the spec's order: playable ones (not random, not those
	// without stage data), installed here or not.
	std::vector<StageContent> stages;
	OwnershipSource ownership;
	// Why this exe's tables can never give Ember lists (a CharaUniqueID twice
	// in the fighter table, a spec set laid out otherwise than Ember knows, a
	// name asset past what Ember reads, nothing playable); empty otherwise.
	// With it the lists are empty and stay so: the host refuses rather than
	// waits.
	std::string error;
	// The game's own ELEVEN as the spec lists it: its costumes and colours,
	// the look the game's Eleven wears (docs/design/beta8/eleven.md). Not a
	// fighter of the universe: it has no battle setup, so it is in neither
	// `fighters`, the pick space nor the build id, and no pick ever names it.
	// Empty where the spec has no row for it.
	std::optional<FighterContent> eleven;
};

// Game thread. Empty lists until the game's content tables are loaded, the
// spec's map is built and refreshed (sfv::kSpecReady, the same on every
// copy whatever its account holds) and its battle setup asset and its
// V-Trigger and V-Skill name assets are there; nothing is read until all of
// them are, so a wait costs only those checks. Those assets are found, or
// loaded, with StaticLoadObject, so call it at the main menu (never in a
// battle). Once everything is there, empty lists with `error` for tables
// that can never make lists. This copy's installs and the names are read
// last: each asks the game's package files. `owned` is limited to the
// account's own answer (LimitToAccount), then the costumes' and stages' is
// kContentOwnership's (ApplyContentOwnership).
ContentLists ReadContentLists();

// The battle setup asset's select-grid list (its +48, the fighters
// sfv::kOwnedFighters starts from when the asset's +96 is clear): a
// TArray<FString> of fighter codes, read by rva 0x55A9B0, whose copy (rva
// 0x5156A0 through 0x4E1F30) copies 16-byte {data, count, capacity}
// elements, so an entry holds a code and nothing else: no slot, row or
// column. In the asset's order, for comparing with the game's own select
// screen (development: the pipe's `content`). Game thread, at the main menu
// (it may load the asset, as ReadContentLists does). Empty with `error` when
// the asset is not there or the list cannot be read.
struct SelectGridList {
	std::vector<std::string> codes;
	std::string error;
};
SelectGridList ReadSelectGridList();

// Makes `owned` what the game's answer would be from the account's own
// cache: SFV now grants every player everything (EntitlementGrant), so the
// game's lists (ReadOwnership) offer everything installed, and Ember's gate,
// when it is on, must still judge by the account. The rules are the spec
// refresh's (rva 0x57CE50) over `entitled`, `visible` and `installed`:
// - a colour stays owned only if `entitled`, unless the battle setup asset
//   restricts the picks (the game's colour list is then the asset's own,
//   which asks no cache);
// - a costume stays owned only if `entitled` (the game's costume list keeps
//   only those, restricted or not);
// - a fighter stays owned only if `entitled` and `visible` and one of its
//   costumes is installed, `entitled` and has an `entitled` colour (the
//   game's fighter list needs its selectable flag, which needs all three).
// Only ever takes away: what the account's cache would offer the game
// already offers. Before ApplyContentOwnership, after the installs are
// read. No game code: the tests call it.
void LimitToAccount(ContentLists& lists);

// Makes each costume's, colour's and stage's `owned` the `policy`'s: the one
// place the rule is applied. Looks start from the game's answer they hold
// when called (ReadContentLists' ReadOwnership): Entitlements keeps it;
// Installed makes a costume owned exactly when it is installed, in every
// colour, unless the battle setup asset restricts the picks
// (`ownership.restricted`), when the game's answer stays. A stage is owned
// under Entitlements when it is installed and `entitled`, under Installed
// when it is installed. Fighters are untouched. Records the rules that
// applied in `ownership.looks` and `ownership.stages`. No game code: the
// tests call it with either policy.
void ApplyContentOwnership(ContentLists& lists, ContentOwnership policy);
const char* ContentOwnershipName(ContentOwnership policy);
// The rule that decided the looks, for the log: "the battle setup's list"
// while the setup restricts the picks, else ContentOwnershipName(looks).
const char* LooksRule(const OwnershipSource& source);

// The lists counted for the log (EmberHost's lines and the pipe's
// `content`). A colour counts once per costume; one not owned is one whose
// own `owned` is clear; a look the cache lacks is one whose costume or colour
// it does not grant. A stage the cache lacks is one installed here and not
// `entitled`.
struct ContentCounts {
	int fighters = 0, fightersEntitled = 0, fightersNotOwned = 0;
	int costumes = 0, costumesInstalled = 0, costumesEntitled = 0, costumesNotOwned = 0;
	int colours = 0, coloursNotOwned = 0, looksNotEntitled = 0;
	int stages = 0, stagesInstalled = 0, stagesNotEntitled = 0, stagesNotOwned = 0;
};
ContentCounts CountContent(const ContentLists& lists);

// What the game grants beside what the account's own cache holds, for the
// grant's line at the main menu (EntitlementGrant): the universe's fighters,
// costumes and colours (as ReadContentLists reads them), the stages of the
// universe installed here, and of each how many the game grants (`granted`)
// and the account's cache grants (`entitled`).
struct GrantCounts {
	int fighters = 0, fightersGranted = 0, fightersEntitled = 0;
	int costumes = 0, costumesGranted = 0, costumesEntitled = 0;
	int colours = 0, coloursGranted = 0, coloursEntitled = 0;
	int stagesInstalled = 0, stagesGranted = 0, stagesEntitled = 0;
	std::string error; // tables that can never give lists (ContentLists::error); the counts are then zero
};
// Game thread. None until the content tables are loaded and the spec's map
// is built and refreshed (sfv::kSpecReady); reads no asset and asks no
// package but the spec's own flags.
std::optional<GrantCounts> CountGrants();

// Whether a battle on this copy loads a look as picked, by one lookup of the
// fighter: the fighter `code` names (compared as the game compares codes) is
// one a battle can load (BattleFighter) and installed here
// (sfv::kFighterInstalled); its costume `costume` has a spec row and its
// package is here (sfv::kCostumePackage, the one test the battle's load makes
// before it would silently put the costume back to 0); its colour `colour`
// has a spec row. A costume or colour below 0 is the launch's "leave it as
// it is" and is not checked; a colour is checked against costume 0 then.
// Whose entitlement it is does not enter: the opponent's pick plays as
// picked. Game thread, at the main menu.
enum class LookLoad {
	Loadable,
	NoTables,            // the fighter table or the spec is not loaded
	NotBattleFighter,    // no such fighter, or one with no battle setup of its own
	FighterNotInstalled, // its package is not here
	CostumeNotInSpec,
	CostumeNotInstalled, // its package is not here
	ColourNotInSpec,
};
LookLoad LookLoadState(std::wstring_view code, int costume, int colour);

// A fighter's V-Skill or V-Trigger names as the game's name asset gave them,
// made one or two choices (picks::kPartChoices): none becomes one unnamed
// choice, more than two the first two. A change is logged with the fighter's
// code and `part` ("V-Skill") and the count the asset gave, so a count
// Ember did not expect shows in the log rather than refusing the roster.
std::vector<std::string> FitChoices(std::vector<std::string> names, const char* part, const std::string& fighter);

// For the log: `V-Skills 2 ("A", "B"), V-Triggers 1 ("C")`.
std::string ChoicesText(const FighterContent& fighter);

// How many V-Skills and V-Triggers a fighter has, as FitChoices makes them.
struct ChoiceCounts {
	int vSkills = 0;
	int vTriggers = 0;
};
// The fighter `code` names, compared as the game compares codes (ignoring
// case): its counts from ReadContentLists' last read of the name assets, so
// the pipe's launch cannot load a choice the game would show as "noname".
// Before any such read (a launch with Ember's catalog never filled, as the
// synctest mode's) the counts are read for this fighter alone, the same way.
// None while either name asset is not there, and for a fighter the last read
// did not list. Game thread, at the main menu.
std::optional<ChoiceCounts> FighterChoiceCounts(std::wstring_view code);

// Whether a battle can load the fighter `code` names: the game's fighter
// table (`charaAsset`, the game data's CharaAsset, sfv::content::kCharaAsset)
// has an entry with that code which names a battle setup of its own
// (sfv::content::kCharaSetupData). Random has none, and neither has ELEVEN:
// the select screen turns both into another fighter before a battle loads,
// which Ember's launch does not, and a battle launched with ELV itself hangs
// at a black screen. This is the one rule for which fighters exist for
// Ember: ReadContentLists lists only these, and a launch (LookLoadState)
// takes only these, so a launch cannot reach a fighter the catalog leaves
// out. Whether this copy has the fighter's package is a separate, per-copy
// fact (FighterContent::installed). Reads only the table, no game code;
// false for a null table.
bool BattleFighter(uint8_t* charaAsset, std::wstring_view code);

// Development (-sfve_unowned, ModConfig): each mark takes a fighter, a
// costume of it or a colour of that costume from this copy's player, on top
// of `owned` (the game's answer, or kContentOwnership's for looks), so the
// gate's screens and refusals can be tried with an account that owns
// everything. Never the other way: nothing not owned becomes owned. Counted
// in `ownership.markedUnowned`.
void MarkUnowned(ContentLists& lists, const std::vector<UnownedContent>& marks);

// Development (-sfve_uninstalled, ModConfig): each code takes a stage of the
// universe from this copy's install, so one full install can play a copy
// that lacks stages. It only ever clears `installed`: the universe, and so
// the build id, stay as they are. A code outside the universe changes
// nothing, and so does one that would leave no stage installed. Returns how
// many stages it cleared.
int MarkUninstalled(ContentLists& lists, const std::vector<std::string>& codes);

// Game thread. This copy's player's voice language for the fighter `code`
// names, as the game's options hold it (sfv::kCharaVoiceLanguage, the list
// the game's own character select reads into a fighter's record):
// sfv::jump::kVoiceJapanese or kVoiceEnglish, English (the lookup's own
// default) for a fighter the list does not hold. None while the game's
// saves or their options are not there.
std::optional<int> CharaVoiceLanguage(std::wstring_view code);

// Game thread, at the main menu. A text the game's own localization data
// holds, by namespace and key (sfv::kCreateText, as the game builds its
// literals), as the game shows it in its current language, UTF-8; empty
// where the data has no such key (the game would show the key itself). Each
// call keeps its FText for the process's life, as the game keeps its own, so
// ask once per key.
std::string LocalizedText(const wchar_t* textNamespace, const wchar_t* key);

} // namespace sfve::mod
