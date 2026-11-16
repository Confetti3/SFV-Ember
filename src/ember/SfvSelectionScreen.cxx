#include "ember/SfvSelectionScreen.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvEleven.hxx"
#include "ember/SfvPickDraw.hxx"
#include "ember/SfvPickPages.hxx"
#include "ember/SfvPickPreview.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelection.hxx"
#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "ui/RowId.hxx"
#include "ui/SelectionLabels.hxx"
#include "ui/Theme.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace sfve::adapter {

namespace {

using sf4e::ui::MenuAction;
using sf4e::ui::MenuEntry;
using sf4e::ui::MenuNavigation;
using sf4e::ui::SelectionStart;
namespace loc = sf4e::loc;
namespace v2 = sf4e::ui::v2;

using pickdraw::BoardFrame;
using pickdraw::CardGrid;
using pickdraw::DrawBoard;
using pickdraw::DrawCardGrid;
using pickdraw::DrawLoadoutCards;
using pickdraw::kBannerWidth;
using pickdraw::kColourCardShape;
using pickdraw::kLocked;
using pickdraw::kStageCardShape;

using pickpages::kBoard;
using pickpages::kColour;
using pickpages::kColourCard;
using pickpages::kColours;
using pickpages::kCostume;
using pickpages::kCostumeCard;
using pickpages::kCostumes;
using pickpages::kDone;
using pickpages::kElevenCard;
using pickpages::kFighter;
using pickpages::kFighterCard;
using pickpages::kLoadout;
using pickpages::kLook;
using pickpages::kRandomCard;
using pickpages::kStage;
using pickpages::kStageBanned;
using pickpages::kStageCard;
using pickpages::kStageNotShared;
using pickpages::kStagePage;
using pickpages::OpenChoice;
using pickpages::Opening;
using pickpages::StageAnswer;

using pickpages::Apply;
using pickpages::BoardRows;
using pickpages::CardColour;
using pickpages::CardId;
using pickpages::ChooseLoadout;
using pickpages::ChooseStage;
using pickpages::ColourCards;
using pickpages::Contains;
using pickpages::CostumeCards;
using pickpages::EntryOf;
using pickpages::FighterLocked;
using pickpages::LinkRandom;
using pickpages::LoadoutCards;
using pickpages::LookRows;
using pickpages::OpeningFor;
using pickpages::OptionId;
using pickpages::PartOfRow;
using pickpages::PartPrefix;
using pickpages::PreviewOf;
using pickpages::Previewed;
using pickpages::Roster;
using pickpages::SavedCard;
using pickpages::StageCards;

} // namespace

void SfvSelectionScreen::Open(const Opening& opening)
{
	auto& nav = menu_.navigation;
	nav.Home();
	for (size_t i = 0; i < opening.pages.size(); i++) {
		if (i) nav.Push(opening.pages[i].first);
		nav.Prefer(opening.pages[i].second);
	}
	startPage_ = opening.pages.size() > 1 ? opening.pages.back().first : std::string();
	closesOnChoice_ = opening.closesOnChoice;
}

void SfvSelectionScreen::FitGrid()
{
	if (gridFitted_ || !ember::Usable(ember::Catalog())) return;
	gridFitted_ = true;
	grid_ = grid::EmbeddedSelectGrid();
	if (grid_.columns <= 0) {
		// The table is checked by the tests; a build whose table does not
		// parse still shows every fighter, eight across.
		mod::Log("pick screen: the select grid table does not parse; the board is the roster, eight across");
		grid_ = {};
		grid_.columns = 8;
	}
	std::vector<std::string> roster;
	for (int id : Roster()) roster.push_back(FighterCode(id));
	const std::vector<std::string> added = grid::FitToRoster(grid_, roster);
	if (!added.empty()) {
		std::string codes;
		for (const std::string& code : added) codes += (codes.empty() ? "" : " ") + code;
		mod::Log("pick screen: the select grid table lacks %s; appended after its last cell", codes.c_str());
	}
	// Random after the last cell where the table gives it none.
	if (!grid_.random) {
		int last = -1;
		for (const grid::Entry& entry : grid_.fighters) last = (std::max)(last, entry.cell.row * grid_.columns + entry.cell.column);
		grid_.random = grid::Cell{(last + 1) % grid_.columns, (last + 1) / grid_.columns};
	}
}

void SfvSelectionScreen::Draw(bool editable, bool stageEditable, const std::string& error,
	const std::vector<std::int64_t>& unavailableStages, bool tinyAllowed, bool fighterKept,
	const std::vector<std::int64_t>& bannedStages)
{
	auto& nav = menu_.navigation;
	PlayerPick pick = dummy_ ? CurrentDummy() : CurrentPick();
	const bool random = !dummy_ && PickRandom();
	const bool eleven = random && PickEleven();
	const bool withStage = !dummy_, stageMine = stageEditable && withStage;
	// The shell says where Back goes and which of its shortcuts this screen
	// hands back; one it has just opened starts where the shell asked.
	const sf4e::ui::EmbeddedReturn& embedded = sf4e::ui::EmbeddedReturnContext();
	if (sf4e::ui::TakeEmbeddedFresh()) {
		openOn_ = embedded.openOn;
		Open(OpeningFor(openOn_, pick, random, dummy_, eleven));
		costume_ = pick.pick.costume;
		stageRefusal_.clear();
	}
	FitGrid();
	const std::vector<StageOption> stageOptions = StageOptions(unavailableStages, bannedStages);
	// A page this screen does not have (the stage page on the dummy's) is
	// the board.
	if (nav.Screen() == kStagePage && !withStage) nav.Home();
	const std::string screen = nav.Screen();
	if (screen != kStagePage) stageRefusal_.clear();
	menu_.rootName = loc::T(dummy_ ? "training.dummy" : "screen.selection");
	menu_.exitName = embedded.exitName;
	menu_.shortcutHints = embedded.shortcutHints;
	menu_.customDetail = false;
	menu_.paneWidth = 0;
	const std::string fighterName = ember::Catalog().fighterName(pick.pick.fighter);
	std::string title = menu_.rootName;
	std::vector<MenuEntry> rows;
	int columns = 1;
	sf4e::ui::GameMenu::Detail detail;
	sf4e::ui::GameMenu::Body body;
	// A focus the pad moved since the body last drew scrolls into view.
	const auto moved = [this](const MenuNavigation& navigation) {
		const bool changed = navigation.Focus() != bodyFocus_;
		bodyFocus_ = navigation.Focus();
		return changed;
	};
	const BoardFrame frame{grid_, pick, random, !dummy_, fighterKept, eleven};
	const char* status = editable ? "selection.status_editable" : "selection.locked_detail";
	stageCardClicked_ = false;
	if (screen == kLook) {
		title = eleven ? ElevenTitle(fighterName) : random ? loc::Tf("selection.random_fighter", fighterName) : fighterName;
		rows = LookRows(pick, random, editable, withStage, stageMine, stageOptions, tinyAllowed && withStage, eleven);
		if (editable) status = "selection.status_adjust";
		// The pane beside the rows is the pick's preview, as wide as the
		// fighter's banner; its stage card, shown while the stage row has the
		// focus, opens the stage cards when clicked.
		menu_.customDetail = true;
		menu_.paneWidth = kBannerWidth;
		detail = [this, &pick, &rows, &stageOptions, withStage](const std::string& focus) {
			std::optional<OpenChoice> choice;
			if (menu_.navigation.Choosing()) choice = OpenChoice{menu_.navigation.DialogId(), menu_.navigation.ChoiceIndex()};
			const ImVec2 at = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
			const v2::Box card =
				DrawPickPreviewIn(PreviewOf(pick, focus, choice, rows, stageOptions, !dummy_), {at, ImVec2(at.x + room.x, at.y + room.y)});
			if (!withStage || !(card.W() > 0 && card.H() > 0) || menu_.navigation.Choosing()) return;
			ImGui::SetCursorScreenPos(card.min);
			if (ImGui::InvisibleButton("##stage-card", ImVec2(card.W(), card.H()))) stageCardClicked_ = true;
		};
	} else if (screen == kCostumes || screen == kColours) {
		const bool costumes = screen == kCostumes;
		title = loc::T(costumes ? "selection.costume_gallery_title" : "selection.color_gallery_title");
		rows = costumes ? CostumeCards(pick, editable) : ColourCards(pick, costume_, editable);
		if (editable && costumes) status = "selection.status_browse";
		const int fighter = pick.pick.fighter, costume = costume_;
		CardGrid cards{costumes ? kCostumes : kColours, costumes ? sf4e::ui::FigureCard : kColourCardShape, {}, {}, {}};
		if (costumes) {
			// Each costume in the colour its picture is of (the package holds
			// each costume in its first colour), named by the costume alone.
			cards.picture = [&pick](ImDrawList* draw, const MenuEntry& e, v2::Box box, float alpha) {
				sf4e::selection::Pick look = pick.pick;
				look.costume = (int)e.key;
				look.color = PictureColour(look.fighter, look.costume);
				DrawFigure(draw, look, box, alpha);
			};
			cards.saved = [&pick](const MenuEntry& e) { return (int)e.key == pick.pick.costume; };
			cards.preview = [&pick](const MenuEntry& e) {
				PickPreview preview;
				preview.pick = Previewed(pick, kCostume, OptionId((int)e.key));
				if (!e.enabled) preview.note = e.detail;
				return preview;
			};
		} else {
			// Each colour as the game's costume menu shows it: its two-tone
			// swatch chip, as large a square as the card holds (its edge
			// alone while the swatches load); one this player cannot ready
			// dimmed and struck through, as the pane's chips are. The pane
			// beside the cards shows the costume in its picture's colour,
			// tagged with the focused colour.
			cards.picture = [fighter, costume](ImDrawList* draw, const MenuEntry& e, v2::Box box, float alpha) {
				if (!sf4e::ui::HasPrefix(e.id, kColourCard)) return;
				const auto& colors = v2::Colors();
				const float s = sf4e::ui::Scale();
				const float side = std::floor((std::min)(box.W(), box.H()) * .8f);
				const ImVec2 corner(std::floor(box.min.x + (box.W() - side) * .5f), std::floor(box.min.y + (box.H() - side) * .5f));
				const v2::Box chip{corner, ImVec2(corner.x + side, corner.y + side)};
				DrawSwatch(draw, fighter, costume, (int)e.key, chip, alpha);
				draw->AddRect(chip.min, chip.max, v2::color::Alpha(colors.border, alpha), 0, 0, (std::max)(1.f, s));
				if (!e.enabled) draw->AddLine(ImVec2(chip.min.x + s, chip.max.y - s), ImVec2(chip.max.x - s, chip.min.y + s), colors.muted, s);
			};
			cards.saved = [&pick, costume](const MenuEntry& e) {
				return sf4e::ui::HasPrefix(e.id, kColourCard) && costume == pick.pick.costume && (int)e.key == pick.pick.color;
			};
			cards.preview = [&pick, costume](const MenuEntry& e) {
				PlayerPick shown = pick;
				shown.pick.costume = costume;
				PickPreview preview;
				preview.pick = Previewed(shown, kColour, OptionId((int)e.key));
				if (!e.enabled) preview.note = e.detail;
				return preview;
			};
		}
		body = [cards, moved](const std::vector<MenuEntry>& entries, MenuNavigation& navigation, MenuAction& action, float height,
				   const sf4e::ui::MenuVisualFeedback&) { DrawCardGrid(entries, navigation, action, height, moved(navigation), cards); };
	} else if (screen == kLoadout) {
		title = loc::T("selection.ultra_combo_title");
		rows = LoadoutCards(pick, editable);
		body = [&pick, moved](const std::vector<MenuEntry>& entries, MenuNavigation& navigation, MenuAction& action, float height,
				   const sf4e::ui::MenuVisualFeedback&) { DrawLoadoutCards(entries, navigation, action, height, moved(navigation), pick); };
	} else if (screen == kStagePage) {
		title = loc::T("selection.stage_title");
		rows = StageCards(pick, editable, stageMine, stageOptions);
		if (editable && !stageMine) status = "selection.only_p1_stage";
		const auto lacking = [&stageOptions](int stage) {
			return std::any_of(stageOptions.begin(), stageOptions.end(),
				[stage](const StageOption& option) { return option.id == stage && option.offer == StageOffer::OpponentLacks; });
		};
		const auto banned = [&stageOptions](int stage) {
			return std::any_of(stageOptions.begin(), stageOptions.end(),
				[stage](const StageOption& option) { return option.id == stage && option.offer == StageOffer::Banned; });
		};
		CardGrid cards{kStagePage, kStageCardShape, {}, {}, {}};
		cards.picture = [lacking, banned](ImDrawList* draw, const MenuEntry& e, v2::Box box, float) {
			DrawFitted(draw, StageImage((int)e.key), box, lacking((int)e.key) || banned((int)e.key) ? kLocked : 1.f, false);
		};
		cards.saved = [&pick](const MenuEntry& e) { return (int)e.key == pick.stage; };
		cards.preview = [&pick, lacking, banned, stageMine](const MenuEntry& e) {
			PickPreview preview;
			preview.pick = pick;
			preview.pick.stage = (int)e.key;
			preview.focus = PreviewFocus::Stage;
			preview.stageShared = !lacking((int)e.key);
			preview.note = banned((int)e.key) ? std::string(loc::T(kStageBanned)) :
				!preview.stageShared ? std::string(loc::T(kStageNotShared)) :
				!stageMine ? std::string(loc::T("selection.only_p1_stage")) : std::string();
			return preview;
		};
		body = [cards, moved](const std::vector<MenuEntry>& entries, MenuNavigation& navigation, MenuAction& action, float height,
				   const sf4e::ui::MenuVisualFeedback&) { DrawCardGrid(entries, navigation, action, height, moved(navigation), cards); };
	} else {
		rows = BoardRows(pick, random, editable, fighterKept, !dummy_, eleven);
		// A table with no cell for ELEVEN draws no card, so none is offered.
		if (!grid_.eleven) {
			rows.erase(std::remove_if(rows.begin(), rows.end(), [](const MenuEntry& row) { return row.id == kElevenCard; }), rows.end());
		}
		LinkRandom(rows, grid_);
		columns = (std::max)(1, grid_.columns);
		if (editable) status = fighterKept ? "selection.winner_keeps_fighter" : "selection.status_browse";
		// A focus from no card of the board (a first visit) starts on the stored card.
		if (!EntryOf(rows, nav.Focus())) nav.Prefer(SavedCard(pick, random, eleven));
		body = [&frame](const std::vector<MenuEntry>& entries, MenuNavigation& navigation, MenuAction& action, float height,
				   const sf4e::ui::MenuVisualFeedback&) { DrawBoard(entries, navigation, action, height, frame); };
	}
	const bool refused = error.empty() && !stageRefusal_.empty();
	const std::string statusLine = !error.empty() ? error : refused ? stageRefusal_ : std::string(loc::T(status));
	const MenuAction action = menu_.Draw(title.c_str(), rows, statusLine.c_str(), detail, columns, {}, body, false, 100,
		error.empty() && !refused ? sf4e::ui::Tone::Neutral : sf4e::ui::Tone::Error);
	Answer(action, screen, pick, random, eleven, editable, stageMine, fighterKept, stageOptions);
}

void SfvSelectionScreen::Answer(const MenuAction& action, const std::string& screen, PlayerPick pick, bool random, bool eleven,
	bool editable, bool stageMine, bool fighterKept, const std::vector<StageOption>& stageOptions)
{
	auto& nav = menu_.navigation;
	const auto close = [] { sf4e::ui::ForwardMenuAction({MenuAction::Close}); };
	if (action.kind == MenuAction::Close || action.kind == MenuAction::Shortcut) sf4e::ui::ForwardMenuAction(action);
	// Opened for one change, Back from the page it opened on goes back to
	// where it came from.
	if (action.kind == MenuAction::Returned && !startPage_.empty() && screen == startPage_) close();
	// A fighter's look page, on its costume.
	const auto look = [&nav] {
		nav.Push(kLook);
		nav.Prefer(kCostume);
	};
	bool store = false;
	if (action.kind == MenuAction::Activate) {
		if (screen == kBoard) {
			if (action.id == kRandomCard && !dummy_) {
				// Chosen, Random rolls its fighter (the runtime's
				// RandomFighterPick) and the board stays; stored, it opens the
				// rolled fighter's page.
				// From Eleven, Random is a choice of its own.
				if ((!random || eleven) && editable && !fighterKept) {
					SetPickRandom(true);
					if (closesOnChoice_ && startPage_.empty()) close();
				} else if (random && !eleven) look();
			} else if (action.id == kElevenCard && !dummy_) {
				// Chosen, Eleven rolls its fighter as Random does, in the look
				// the game's Eleven wears; stored, it opens the rolled
				// fighter's page.
				if (!eleven && editable && !fighterKept) {
					SetPickEleven(true);
					if (closesOnChoice_ && startPage_.empty()) close();
				} else if (eleven) look();
			} else if (const std::optional<int> fighter = sf4e::ui::RowKey<int>(action.id, kFighterCard)) {
				const bool choosable = editable && !fighterKept && ember::ValidFighter(*fighter) && !FighterLocked(*fighter);
				if (choosable) {
					// The player's fighter is chosen as every path chooses it
					// (ChooseFighter); the dummy has no memory, and no Random.
					if (dummy_) {
						pick.pick = LookFor(pick.pick, *fighter, false);
						store = true;
					} else {
						ChooseFighter(*fighter);
					}
					if (closesOnChoice_ && startPage_.empty()) close();
					else look();
				} else if (*fighter == pick.pick.fighter) look();
			}
		} else if (screen == kLook) {
			if (action.id == kDone) close();
			else if (action.id == kFighter) {
				nav.Home();
				nav.Prefer(SavedCard(pick, random, eleven));
			} else if (action.id == kCostume) {
				costume_ = pick.pick.costume;
				nav.Push(kCostumes);
				nav.Prefer(CardId(kCostumeCard, costume_));
			} else if (action.id == kColour) {
				costume_ = pick.pick.costume;
				nav.Push(kColours);
				nav.Prefer(CardId(kColourCard, pick.pick.color));
			} else if (const std::optional<picks::LoadoutPart> part = PartOfRow(action.id)) {
				nav.Push(kLoadout);
				nav.Prefer(CardId(PartPrefix(*part).c_str(), picks::LoadoutOf(pick.pick).choice[*part]));
			} else if (action.id == kStage) {
				nav.Push(kStagePage);
				nav.Prefer(CardId(kStageCard, pick.stage));
			}
		} else if (screen == kCostumes) {
			if (const std::optional<int> costume = sf4e::ui::RowKey<int>(action.id, kCostumeCard)) {
				costume_ = *costume;
				nav.Push(kColours);
				nav.Prefer(CardId(kColourCard, CardColour(pick.pick, costume_)));
			}
		} else if (screen == kColours) {
			const std::optional<int> colour = sf4e::ui::RowKey<int>(action.id, kColourCard);
			if (colour && editable && Contains(ColourIds(pick.pick.fighter, costume_, true), *colour)) {
				// A colour stores its costume with it; back to the look page,
				// or, opened for the look, back where it came from.
				pick.pick.costume = costume_;
				pick.pick.color = *colour;
				store = true;
				while (nav.Screen() == kColours || nav.Screen() == kCostumes) nav.Return();
				if (closesOnChoice_ && startPage_ == kCostumes) close();
			}
		} else if (screen == kLoadout) {
			if (editable && ChooseLoadout(action.id, pick)) store = true;
		} else if (screen == kStagePage) {
			switch (ChooseStage(action.id, pick, editable, stageMine, stageOptions)) {
			case StageAnswer::Stored:
				store = true;
				[[fallthrough]];
			case StageAnswer::Unchanged:
				stageRefusal_.clear();
				// Opened for this one change, the pick is it; from the look
				// page the stage page stays.
				if (closesOnChoice_ && startPage_ == kStagePage) close();
				break;
			case StageAnswer::Locked: stageRefusal_ = loc::T("selection.locked_detail"); break;
			case StageAnswer::OnlyP1: stageRefusal_ = loc::T("selection.only_p1_stage"); break;
			case StageAnswer::NotShared: stageRefusal_ = loc::T(kStageNotShared); break;
			case StageAnswer::Banned: stageRefusal_ = loc::T(kStageBanned); break;
			case StageAnswer::NotOffered: break;
			}
		}
	} else if (screen == kLook && editable && (action.kind == MenuAction::Adjust || action.kind == MenuAction::Chosen)) {
		// The value rows step and choose in place; the stage only where it is
		// this player's.
		if ((action.id != kStage || stageMine) && Apply(action, pick, stageOptions, !dummy_)) {
			// A fighter stepped or chosen here is chosen as on the board:
			// Random and Eleven off, the fighter in its own look.
			if (action.id == kFighter && !dummy_) ChooseFighter(pick.pick.fighter);
			else store = true;
		}
	}
	// The look page's stage card, clicked.
	if (stageCardClicked_ && screen == kLook && nav.Screen() == kLook) {
		nav.Push(kStagePage);
		nav.Prefer(CardId(kStageCard, pick.stage));
	}
	stageCardClicked_ = false;
	if (!store) return;
	if (dummy_) SetDummy(pick);
	else SetPick(pick);
}

} // namespace sfve::adapter
