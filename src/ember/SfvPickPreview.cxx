#include "ember/SfvPickPreview.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPictureSpec.hxx"
#include "ember/SfvSwatchStrip.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "ui/GameMenu.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/SelectionLabels.hxx"
#include "ui/Theme.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

namespace loc = sf4e::loc;
namespace v2 = sf4e::ui::v2;
using sf4e::ui::SelectionImage;

// What cannot be readied is drawn at this alpha, as the choice list dims it.
constexpr float kLocked = .4f;
// The shapes (height over width) frames keep while their pictures load: the
// banner's (CharaSelectIcon_l, 380x258), a stage card's (StageSelectIcon_l,
// 674x238), the shop costume picture's (420x190), and a fighter's figure's
// (the captured cutout's 256x384, which the large HUD figure's frame borrows
// until it loads).
constexpr float kBannerShape = 1 / pictures::kBannerAspect;
constexpr float kStageCardShape = 1 / pictures::kStageCardAspect;
constexpr float kShopCostumeShape = 1 / pictures::kShopCostumeAspect;
constexpr float kFigureShape = 384.f / 256.f;
// A swatch chip: its first colour fills this much of it from the top, the
// second the rest, as the game's costume menu splits its swatch.
constexpr float kSwatchSplit = .6f;
// The V marks' frame: as wide as this many line heights, so a mark, Ember's
// plate or nothing (while the mark loads) all leave the words in one place.
constexpr float kMarkWidth = 1.5f;

ImU32 Rgb(std::uint32_t rgb, float alpha)
{
	return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (int)(alpha * 255 + .5f));
}

ImU32 White(float alpha)
{
	return IM_COL32(255, 255, 255, (int)(alpha * 255 + .5f));
}

float Whole(float pixels)
{
	return std::floor(pixels + .5f);
}

SelectionImage Missing()
{
	SelectionImage image;
	image.missing = true;
	return image;
}

// A picture `width` wide at its own shape from `at`, at `alpha`, or an empty
// frame of the shape it is expected to have while it loads; where that is
// taller than `tallest`, that tall and narrower, centred. The height it
// takes; none where the game has no picture or there is no room for one.
// `drawn`, when given, receives the box it took.
float DrawPicture(ImDrawList* draw, const SelectionImage& image, ImVec2 at, float width, float expectedShape, float tallest,
	float alpha = 1, v2::Box* drawn = nullptr)
{
	if (image.missing) return 0;
	const float s = sf4e::ui::Scale();
	const float shape = image.width > 0 && image.height > 0 ? (float)image.height / image.width : expectedShape;
	const float natural = std::floor(width * shape), height = (std::min)(natural, std::floor(tallest));
	if (!(height > 0)) return 0;
	const float wide = height < natural ? std::floor(height / shape) : width;
	const ImVec2 min(at.x + std::floor((width - wide) * .5f), at.y), max(min.x + wide, at.y + height);
	if (image.texture) draw->AddImage(image.texture, min, max, image.uvMin, image.uvMax, White(alpha));
	else draw->AddRect(min, max, v2::Colors().border, 0, 0, s);
	if (drawn) *drawn = {min, max};
	return height;
}

// A picture fitted whole in `box` at its own shape, centred across and, with
// `bottom`, standing on the box's foot (a figure), else centred down; at
// `alpha`. While it loads, the box's edge. False where the game has none.
bool FitIn(ImDrawList* draw, const SelectionImage& image, v2::Box box, float alpha, bool bottom)
{
	if (image.missing) return false;
	if (!(box.W() > 0 && box.H() > 0)) return true;
	if (!image.texture || image.width <= 0 || image.height <= 0) {
		draw->AddRect(box.min, box.max, v2::color::Alpha(v2::Colors().border, .6f), 0, 0, sf4e::ui::Scale());
		return true;
	}
	const float factor = (std::min)(box.W() / image.width, box.H() / image.height);
	const float w = std::floor(image.width * factor), h = std::floor(image.height * factor);
	const float x = std::floor(box.min.x + (box.W() - w) * .5f);
	const float y = bottom ? box.max.y - h : std::floor(box.min.y + (box.H() - h) * .5f);
	draw->AddImage(image.texture, ImVec2(x, y), ImVec2(x + w, y + h), image.uvMin, image.uvMax, White(alpha));
	return true;
}

// The captured cutout of the look (the package's), through the menu's art:
// SelectionArt::Character with its fallbacks to the costume's first colour
// and the fighter's first look, or with `exact` SelectionArt::Appearance,
// that look alone. Missing without the art.
SelectionImage Cutout(const sf4e::selection::Pick& look, float drawnHeight, bool exact)
{
	sf4e::ui::SelectionArt* art = sf4e::ui::MenuArt();
	if (!art || !ember::ValidFighter(look.fighter)) return Missing();
	if (exact) return art->Appearance(look.fighter, look.costume, look.color);
	return art->CharacterFor(look.fighter, look.costume, look.color, drawnHeight);
}

bool Locked(const sf4e::selection::Pick& look)
{
	return FighterNotOwned(look.fighter) || CostumeNotOwned(look.fighter, look.costume) ||
		ColourNotOwned(look.fighter, look.costume, look.color);
}

// The look's picture: the cutout, the shop's picture of the costume where
// SfvPictureSpec allows it and the shop has one, the large HUD figure, or
// the banner, the first the game or the package has; dimmed where this
// player does not own the look (NOT OWNED).
float DrawLookPicture(ImDrawList* draw, const PlayerPick& shown, ImVec2 at, float width, float tallest)
{
	const int fighter = shown.pick.fighter;
	const float alpha = Locked(shown.pick) ? kLocked : 1.f;
	const SelectionImage cutout = Cutout(shown.pick, tallest, false);
	if (!cutout.missing) return DrawPicture(draw, cutout, at, width, kFigureShape, tallest, alpha);
	const SelectionImage costume = CostumeImage(fighter, shown.pick.costume);
	if (!costume.missing) return DrawPicture(draw, costume, at, width, kShopCostumeShape, tallest, alpha);
	const SelectionImage large = FighterImage(fighter, FighterPicture::Large);
	if (!large.missing) return DrawPicture(draw, large, at, width, kFigureShape, tallest, alpha);
	return DrawPicture(draw, FighterImage(fighter, FighterPicture::Banner), at, width, kBannerShape, tallest, alpha);
}

// The game's EX colours' prefix ("EX01"), as ColourName writes it.
constexpr const char* kExCaption = "EX";

// The costume's colours as chips (SfvSwatchStrip) at the pane's width, with
// the sizes they are laid out by. No chips where the game has no swatch for
// any of its colours.
struct Strip {
	StripMetrics metrics;
	StripLayout layout;
	float barGap = 0, bar = 0; // the chosen chip's gold bar: its distance under the chip, and its height
};

Strip LayOutStrip(const PlayerPick& shown, float width)
{
	const float s = sf4e::ui::Scale();
	Strip strip;
	const CostumePalette palette = CostumeSwatches(shown.pick.fighter, shown.pick.costume);
	const bool anySwatch = std::any_of(palette.colours.begin(), palette.colours.end(),
		[](const StripColour& colour) { return colour.swatch.has_value(); });
	if (palette.read && !anySwatch) return strip;
	StripMetrics& metrics = strip.metrics;
	metrics.width = width;
	metrics.chip = Whole(ImGui::GetTextLineHeight());
	metrics.gap = Whole(4 * s);
	strip.barGap = (std::max)(1.f, Whole(2 * s));
	strip.bar = (std::max)(1.f, Whole(2 * s));
	metrics.pitch = metrics.chip + strip.barGap + strip.bar + metrics.gap;
	metrics.exIndent = Whole(ImGui::CalcTextSize(kExCaption).x) + 2 * metrics.gap;
	strip.layout = LayOutSwatchStrip(palette.colours, shown.pick.color, metrics);
	return strip;
}

void DrawStrip(ImDrawList* draw, const Strip& strip, ImVec2 at)
{
	const auto& colors = v2::Colors();
	const float s = sf4e::ui::Scale();
	const StripMetrics& metrics = strip.metrics;
	if (strip.layout.exRow >= 0) {
		const float y = at.y + strip.layout.exRow + (metrics.chip - ImGui::GetTextLineHeight()) * .5f;
		draw->AddText(ImVec2(at.x, y), colors.muted, kExCaption);
	}
	for (const Chip& chip : strip.layout.chips) {
		const float alpha = chip.dimmed ? kLocked : 1.f;
		const ImVec2 min(at.x + chip.x, at.y + chip.y), max(min.x + metrics.chip, min.y + metrics.chip);
		// While the swatches load, the chip is its edge alone.
		if (chip.swatch) {
			const float split = Whole(min.y + metrics.chip * kSwatchSplit);
			draw->AddRectFilled(min, ImVec2(max.x, split), Rgb(chip.swatch->first, alpha));
			draw->AddRectFilled(ImVec2(min.x, split), max, Rgb(chip.swatch->second, alpha));
		}
		draw->AddRect(min, max, chip.chosen ? colors.text : v2::color::Alpha(colors.border, alpha), 0, 0, s);
		// Not this player's to ready: struck through, corner to corner.
		if (chip.dimmed) draw->AddLine(ImVec2(min.x + s, max.y - s), ImVec2(max.x - s, min.y + s), colors.muted, s);
		// The chosen colour: Band's focus mark, a gold bar, under it.
		if (chip.chosen) {
			const float top = max.y + strip.barGap;
			draw->AddRectFilled(ImVec2(min.x, top), ImVec2(max.x, top + strip.bar), colors.accent);
		}
	}
}

// The look's words under its picture: the fighter's and the costume's names,
// the colour chips (`strip`) and the colour's label. With no draw list it
// only measures, as v2::Paragraph does. The height it takes.
float DrawLookWords(ImDrawList* draw, const PlayerPick& shown, const Strip& strip, ImVec2 at)
{
	const auto& colors = v2::Colors();
	const float line = ImGui::GetTextLineHeightWithSpacing(), top = at.y;
	const int fighter = shown.pick.fighter;
	const auto text = [&](ImU32 tone, const std::string& words) {
		if (draw) draw->AddText(at, tone, words.c_str());
		at.y += line;
	};
	text(colors.text, ember::Catalog().fighterName(fighter));
	text(colors.muted, CostumeName(fighter, shown.pick.costume));
	if (draw) DrawStrip(draw, strip, at);
	at.y += strip.layout.height;
	text(colors.muted, std::string(loc::T("selection.color_row")) + " " + ColourName(shown.pick.color));
	return at.y - top;
}

// One line per loadout part: its mark, then its label and its choice in the
// game's words. `focused`: the part whose line is brighter, if any. With no
// draw list it only measures (and asks for no mark). The height it takes.
float DrawLoadout(ImDrawList* draw, const PlayerPick& shown, std::optional<picks::LoadoutPart> focused, ImVec2 at)
{
	const auto& colors = v2::Colors();
	const float s = sf4e::ui::Scale(), line = ImGui::GetTextLineHeightWithSpacing();
	const float height = Whole(ImGui::GetTextLineHeight()), width = Whole(height * kMarkWidth);
	const picks::Loadout loadout = picks::LoadoutOf(shown.pick);
	const float top = at.y;
	for (const picks::LoadoutPart part : picks::kLoadoutParts) {
		if (draw) {
			const int number = loadout.choice[part];
			const ImVec2 mark(std::floor(at.x), std::floor(at.y));
			DrawLoadoutMark(draw, part, number, {mark, ImVec2(mark.x + width, mark.y + height)});
			const std::string text =
				std::string(PartLabel(part)) + "  " + ChoiceText(number, MoveName(shown.pick.fighter, part, number));
			draw->AddText(ImVec2(at.x + width + Whole(8 * s), at.y), focused == part ? colors.text : colors.muted, text.c_str());
		}
		at.y += line;
	}
	return at.y - top;
}

// The stage's card, dimmed for one the opponent lacks, its name and, for
// Random, what Random does; the card gives way to the words in `room`. The
// box the card took, empty for none.
v2::Box DrawStage(ImDrawList* draw, const PickPreview& preview, ImVec2 at, float width, float room)
{
	const auto& colors = v2::Colors();
	const float gap = 10 * sf4e::ui::Scale(), line = ImGui::GetTextLineHeightWithSpacing();
	const int stage = preview.pick.stage;
	const bool random = stage == picks::kRandomStage;
	const std::string detail = random ? loc::T("selection.random_stage.detail") : "";
	// Measured as drawn: v2::Paragraph with no draw list only measures.
	const float words = line + (random ? v2::Paragraph(nullptr, at, width, detail, ImGui::GetFontSize(), colors.muted) : 0);
	v2::Box card{};
	at.y += DrawPicture(draw, StageImage(stage), at, width, kStageCardShape, PictureRoom(room, words, gap),
		preview.stageShared ? 1.f : kLocked, &card) + gap;
	draw->AddText(at, colors.text, sf4e::ui::StageLabel(stage).c_str());
	if (random) {
		at.y += line;
		v2::Paragraph(draw, at, width, detail, ImGui::GetFontSize(), colors.muted);
	}
	return card;
}

// The board's Random card: its mark on a banner-shaped plate, its name, what
// it does, and the fighter it rolled while it is the pick.
void DrawRandom(ImDrawList* draw, const PickPreview& preview, ImVec2 at, float width, float room)
{
	const auto& colors = v2::Colors();
	const float gap = 10 * sf4e::ui::Scale(), line = ImGui::GetTextLineHeightWithSpacing();
	const std::string detail = loc::T("selection.random_card.detail");
	const bool rolled = preview.randomSaved && ember::ValidFighter(preview.pick.pick.fighter);
	const std::string fighter =
		rolled ? loc::Tf("selection.random_fighter", ember::Catalog().fighterName(preview.pick.pick.fighter)) : std::string();
	const float words = line + v2::Paragraph(nullptr, at, width, detail, ImGui::GetFontSize(), colors.muted) + (rolled ? line : 0);
	const float natural = std::floor(width * kBannerShape), height = (std::min)(natural, std::floor(PictureRoom(room, words, gap)));
	if (height > 0) {
		const float wide = height < natural ? std::floor(height / kBannerShape) : width;
		const float left = at.x + std::floor((width - wide) * .5f);
		const v2::Box plate{ImVec2(left, at.y), ImVec2(left + wide, at.y + height)};
		draw->AddRectFilled(plate.min, plate.max, colors.frame);
		DrawRandomMark(draw, plate, preview.randomSaved ? colors.accent : colors.text);
		at.y += height + gap;
	}
	draw->AddText(at, preview.randomSaved ? colors.accent : colors.text, loc::T("selection.random_card"));
	at.y += line;
	if (rolled) {
		draw->AddText(at, colors.text, fighter.c_str());
		at.y += line;
	}
	v2::Paragraph(draw, at, width, detail, ImGui::GetFontSize(), colors.muted);
}

// The board's Eleven card: the game's own ELEVEN banner where it has one, its
// name, what it does (Random's words: a new fighter for every game), and the
// fighter it rolled while Eleven is the pick.
void DrawEleven(ImDrawList* draw, const PickPreview& preview, ImVec2 at, float width, float room)
{
	const auto& colors = v2::Colors();
	const float gap = 10 * sf4e::ui::Scale(), line = ImGui::GetTextLineHeightWithSpacing();
	const std::string detail = loc::T("selection.random_card.detail");
	const bool rolled = preview.randomSaved && ember::ValidFighter(preview.pick.pick.fighter);
	const std::string fighter = rolled ? ember::Catalog().fighterName(preview.pick.pick.fighter) : std::string();
	const float words = line + v2::Paragraph(nullptr, at, width, detail, ImGui::GetFontSize(), colors.muted) + (rolled ? line : 0);
	const float drawn = DrawPicture(draw, ElevenImage(FighterPicture::Banner), at, width, kBannerShape, PictureRoom(room, words, gap));
	if (drawn > 0) at.y += drawn + gap;
	draw->AddText(at, preview.randomSaved ? colors.accent : colors.text, ElevenName().c_str());
	at.y += line;
	if (rolled) {
		draw->AddText(at, colors.text, fighter.c_str());
		at.y += line;
	}
	v2::Paragraph(draw, at, width, detail, ImGui::GetFontSize(), colors.muted);
}

} // namespace

float PictureRoom(float room, float words, float gap)
{
	return (std::max)(0.f, room - words - gap);
}

bool DrawFitted(ImDrawList* draw, const SelectionImage& image, v2::Box box, float alpha, bool bottom)
{
	return FitIn(draw, image, box, alpha, bottom);
}

bool DrawFigure(ImDrawList* draw, const sf4e::selection::Pick& look, v2::Box box, float alpha, bool exact)
{
	if (exact) return FitIn(draw, Cutout(look, box.H(), true), box, alpha, true);
	const SelectionImage cutout = Cutout(look, box.H(), false);
	if (!cutout.missing) return FitIn(draw, cutout, box, alpha, true);
	if (FitIn(draw, FighterImage(look.fighter, FighterPicture::Large), box, alpha, true)) return true;
	return FitIn(draw, FighterImage(look.fighter, FighterPicture::Banner), box, alpha, false);
}

bool DrawSwatch(ImDrawList* draw, int fighter, int costume, int colour, v2::Box box, float alpha)
{
	const CostumePalette palette = CostumeSwatches(fighter, costume);
	if (!palette.read) return false;
	const auto entry = std::find_if(palette.colours.begin(), palette.colours.end(),
		[colour](const StripColour& candidate) { return candidate.colour == colour; });
	if (entry == palette.colours.end() || !entry->swatch) return false;
	const float split = Whole(box.min.y + box.H() * kSwatchSplit);
	draw->AddRectFilled(box.min, ImVec2(box.max.x, split), Rgb(entry->swatch->first, alpha));
	draw->AddRectFilled(ImVec2(box.min.x, split), box.max, Rgb(entry->swatch->second, alpha));
	return true;
}

void DrawLoadoutMark(ImDrawList* draw, picks::LoadoutPart part, int number, v2::Box box)
{
	const auto& colors = v2::Colors();
	const SelectionImage mark = LoadoutImage(part, number);
	if (mark.texture && mark.width > 0 && mark.height > 0) {
		const float tall = std::floor((std::min)(box.H(), (float)mark.height));
		const float wide = std::floor((std::min)(box.W(), mark.width * tall / mark.height));
		const ImVec2 min(std::floor(box.min.x + (box.W() - wide) * .5f), std::floor(box.min.y + (box.H() - tall) * .5f));
		draw->AddImage(mark.texture, min, ImVec2(min.x + wide, min.y + tall), mark.uvMin, mark.uvMax);
		return;
	}
	if (!mark.missing) return;
	const char* numeral = picks::Numeral(number);
	draw->AddRectFilled(box.min, box.max, colors.frame);
	const ImVec2 size = ImGui::CalcTextSize(numeral);
	draw->AddText(ImVec2(Whole(box.min.x + (box.W() - size.x) * .5f), Whole(box.min.y + (box.H() - size.y) * .5f)), colors.text,
		numeral);
}

void DrawRandomMark(ImDrawList* draw, v2::Box box, ImU32 colour)
{
	ImFont* font = sf4e::ui::HeadingFont();
	const float size = std::floor((std::min)(box.W(), box.H()) * .7f);
	if (!font || !(size > 0)) return;
	const ImVec2 text = font->CalcTextSizeA(size, FLT_MAX, 0, "?");
	draw->AddText(font, size, ImVec2(Whole(box.min.x + (box.W() - text.x) * .5f), Whole(box.min.y + (box.H() - text.y) * .5f)),
		colour, "?");
}

v2::Box DrawPickPreviewIn(const PickPreview& preview, v2::Box box)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const auto& colors = v2::Colors();
	const float gap = 10 * sf4e::ui::Scale();
	ImVec2 at = box.min;
	const float width = box.W(), bottom = box.max.y;
	if (!(width > 0)) return {};
	if (!preview.note.empty()) {
		at.y += v2::Paragraph(draw, at, width, preview.note, ImGui::GetFontSize(), colors.danger) + gap;
	}
	if (preview.focus == PreviewFocus::Stage) return DrawStage(draw, preview, at, width, bottom - at.y);
	if (preview.focus == PreviewFocus::Random) {
		DrawRandom(draw, preview, at, width, bottom - at.y);
		return {};
	}
	if (preview.focus == PreviewFocus::Eleven) {
		DrawEleven(draw, preview, at, width, bottom - at.y);
		return {};
	}
	const PlayerPick& shown = preview.pick;
	const std::optional<picks::LoadoutPart> focused =
		preview.focus == PreviewFocus::Loadout ? std::optional<picks::LoadoutPart>(preview.part) : std::nullopt;
	// Every line under the picture keeps its room: the look's words and the
	// loadout's lines are measured as they are drawn (no draw list), and the
	// picture takes what is left, as the stage's card does.
	const Strip strip = LayOutStrip(shown, width);
	const float words = DrawLookWords(nullptr, shown, strip, at) + DrawLoadout(nullptr, shown, focused, at);
	at.y += DrawLookPicture(draw, shown, at, width, PictureRoom(bottom - at.y, words, gap)) + gap;
	at.y += DrawLookWords(draw, shown, strip, at);
	DrawLoadout(draw, shown, focused, at);
	return {};
}

void DrawPickPreview(const PickPreview& preview)
{
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const ImVec2 room = ImGui::GetContentRegionAvail();
	DrawPickPreviewIn(preview, {at, ImVec2(at.x + room.x, at.y + room.y)});
}

} // namespace sfve::adapter
