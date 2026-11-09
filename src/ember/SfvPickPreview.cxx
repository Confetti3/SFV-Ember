#include "ember/SfvPickPreview.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPictureSpec.hxx"
#include "ember/SfvSwatchStrip.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "ui/SelectionLabels.hxx"
#include "ui/Theme.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

namespace loc = sf4e::loc;
namespace v2 = sf4e::ui::v2;

// What cannot be readied is drawn at this alpha, as the choice list dims it.
constexpr float kLocked = .4f;
// The shapes (height over width) frames keep while their pictures load: the
// banner's (CharaSelectIcon_l, 380x258), a stage card's (StageSelectIcon_l,
// 674x238) and the shop costume picture's (420x190).
constexpr float kBannerShape = 1 / pictures::kBannerAspect;
constexpr float kStageCardShape = 1 / pictures::kStageCardAspect;
constexpr float kShopCostumeShape = 1 / pictures::kShopCostumeAspect;
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

float Whole(float pixels)
{
	return std::floor(pixels + .5f);
}

// A picture `width` wide at its own shape from `at`, at `alpha`, or an empty
// frame of the shape it is expected to have while it loads; where that is
// taller than `tallest`, that tall and narrower, centred. The height it
// takes; none where the game has no picture or there is no room for one.
float DrawPicture(ImDrawList* draw, const sf4e::ui::SelectionImage& image, ImVec2 at, float width, float expectedShape,
	float tallest, float alpha = 1)
{
	if (image.missing) return 0;
	const float s = sf4e::ui::Scale();
	const float shape = image.width > 0 && image.height > 0 ? (float)image.height / image.width : expectedShape;
	const float natural = std::floor(width * shape), height = (std::min)(natural, std::floor(tallest));
	if (!(height > 0)) return 0;
	const float wide = height < natural ? std::floor(height / shape) : width;
	const ImVec2 min(at.x + std::floor((width - wide) * .5f), at.y), max(min.x + wide, at.y + height);
	if (image.texture) draw->AddImage(image.texture, min, max, image.uvMin, image.uvMax, IM_COL32(255, 255, 255, (int)(alpha * 255 + .5f)));
	else draw->AddRect(min, max, v2::Colors().border, 0, 0, s);
	return height;
}

// The look's picture: the shop's picture of the costume where SfvPictureSpec
// allows it and the shop has one, else the fighter's banner.
float DrawLookPicture(ImDrawList* draw, const PlayerPick& shown, ImVec2 at, float width, float tallest, float alpha)
{
	const sf4e::ui::SelectionImage costume = CostumeImage(shown.pick.fighter, shown.pick.costume);
	if (!costume.missing) return DrawPicture(draw, costume, at, width, kShopCostumeShape, tallest, alpha);
	return DrawPicture(draw, FighterImage(shown.pick.fighter, FighterPicture::Banner), at, width, kBannerShape, tallest, alpha);
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

// The look: the picture, the fighter's and the costume's names, the colour
// chips and the colour's label; dimmed where this player cannot ready it.
// `room` is the pane's height from `at`, which the loadout's lines under the
// look share: the picture gives way so that every line shows.
float DrawLook(ImDrawList* draw, const PlayerPick& shown, ImVec2 at, float width, float room)
{
	const auto& colors = v2::Colors();
	const float gap = 10 * sf4e::ui::Scale(), line = ImGui::GetTextLineHeightWithSpacing();
	const int fighter = shown.pick.fighter;
	const bool locked = FighterNotOwned(fighter) || CostumeNotOwned(fighter, shown.pick.costume) ||
		ColourNotOwned(fighter, shown.pick.costume, shown.pick.color);
	const Strip strip = LayOutStrip(shown, width);
	const float top = at.y, tallest = PictureRoom(room, LookWordsHeight(line, strip.layout.height), gap);
	at.y += DrawLookPicture(draw, shown, at, width, tallest, locked ? kLocked : 1.f) + gap;
	draw->AddText(at, colors.text, ember::Catalog().fighterName(fighter));
	at.y += line;
	draw->AddText(at, colors.muted, CostumeName(fighter, shown.pick.costume).c_str());
	at.y += line;
	DrawStrip(draw, strip, at);
	at.y += strip.layout.height;
	const std::string colour = std::string(loc::T("selection.color_row")) + " " + ColourName(shown.pick.color);
	draw->AddText(at, colors.muted, colour.c_str());
	at.y += line;
	return at.y - top;
}

// The game's mark for the part's choice in a frame `width` by `height` at
// `at`: the mark fitted on whole pixels, never larger than it is; Ember's
// plate with the numeral where the mark does not load; nothing while it
// loads.
void DrawMark(ImDrawList* draw, picks::LoadoutPart part, int number, ImVec2 at, float width, float height)
{
	const auto& colors = v2::Colors();
	const sf4e::ui::SelectionImage mark = LoadoutImage(part, number);
	if (mark.texture && mark.width > 0 && mark.height > 0) {
		const float tall = std::floor((std::min)(height, (float)mark.height));
		const float wide = std::floor((std::min)(width, mark.width * tall / mark.height));
		const ImVec2 min(std::floor(at.x + (width - wide) * .5f), std::floor(at.y + (height - tall) * .5f));
		draw->AddImage(mark.texture, min, ImVec2(min.x + wide, min.y + tall), mark.uvMin, mark.uvMax);
		return;
	}
	if (!mark.missing) return;
	const char* numeral = picks::Numeral(number);
	draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), colors.frame);
	const ImVec2 size = ImGui::CalcTextSize(numeral);
	draw->AddText(ImVec2(Whole(at.x + (width - size.x) * .5f), Whole(at.y + (height - size.y) * .5f)), colors.text, numeral);
}

// One line per loadout part: its mark, then its label and its choice in the
// game's words. `focused`: the part whose line is brighter, if any.
float DrawLoadout(ImDrawList* draw, const PlayerPick& shown, std::optional<picks::LoadoutPart> focused, ImVec2 at)
{
	const auto& colors = v2::Colors();
	const float s = sf4e::ui::Scale(), line = ImGui::GetTextLineHeightWithSpacing();
	const float height = Whole(ImGui::GetTextLineHeight()), width = Whole(height * kMarkWidth);
	const picks::Loadout loadout = picks::LoadoutOf(shown.pick);
	const float top = at.y;
	for (const picks::LoadoutPart part : picks::kLoadoutParts) {
		const int number = loadout.choice[part];
		DrawMark(draw, part, number, ImVec2(std::floor(at.x), std::floor(at.y)), width, height);
		const std::string text =
			std::string(PartLabel(part)) + "  " + ChoiceText(number, MoveName(shown.pick.fighter, part, number));
		draw->AddText(ImVec2(at.x + width + Whole(8 * s), at.y), focused == part ? colors.text : colors.muted, text.c_str());
		at.y += line;
	}
	return at.y - top;
}

// The stage's card, dimmed for one the opponent lacks, its name and, for
// Random, what Random does; the card gives way to the words in `room`.
void DrawStage(ImDrawList* draw, const PickPreview& preview, ImVec2 at, float width, float room)
{
	const auto& colors = v2::Colors();
	const float gap = 10 * sf4e::ui::Scale(), line = ImGui::GetTextLineHeightWithSpacing();
	const int stage = preview.pick.stage;
	const bool random = stage == picks::kRandomStage;
	const std::string detail = random ? loc::T("selection.random_stage.detail") : "";
	// Measured as drawn: v2::Paragraph with no draw list only measures.
	const float words = line + (random ? v2::Paragraph(nullptr, at, width, detail, ImGui::GetFontSize(), colors.muted) : 0);
	at.y += DrawPicture(draw, StageImage(stage), at, width, kStageCardShape, PictureRoom(room, words, gap),
		preview.stageShared ? 1.f : kLocked) + gap;
	draw->AddText(at, colors.text, sf4e::ui::StageLabel(stage).c_str());
	if (!random) return;
	at.y += line;
	v2::Paragraph(draw, at, width, detail, ImGui::GetFontSize(), colors.muted);
}

} // namespace

float LookWordsHeight(float line, float strip)
{
	// Two names, the colour's line and a line per loadout part.
	return (3 + (float)std::size(picks::kLoadoutParts)) * line + strip;
}

float PictureRoom(float room, float words, float gap)
{
	return (std::max)(0.f, room - words - gap);
}

void DrawPickPreview(const PickPreview& preview)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const auto& colors = v2::Colors();
	const float gap = 10 * sf4e::ui::Scale();
	ImVec2 at = ImGui::GetCursorScreenPos();
	const ImVec2 room = ImGui::GetContentRegionAvail();
	const float width = room.x, bottom = at.y + room.y;
	if (!preview.note.empty()) {
		at.y += v2::Paragraph(draw, at, width, preview.note, ImGui::GetFontSize(), colors.danger) + gap;
	}
	if (preview.focus == PreviewFocus::Stage) {
		DrawStage(draw, preview, at, width, bottom - at.y);
		return;
	}
	at.y += DrawLook(draw, preview.pick, at, width, bottom - at.y);
	const std::optional<picks::LoadoutPart> focused =
		preview.focus == PreviewFocus::Loadout ? std::optional<picks::LoadoutPart>(preview.part) : std::nullopt;
	DrawLoadout(draw, preview.pick, focused, at);
}

} // namespace sfve::adapter
