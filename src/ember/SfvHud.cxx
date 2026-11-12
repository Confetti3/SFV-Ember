#include "ember/SfvHud.hxx"

#include "ember/EmberHost.hxx"
#include "ember/SfvFrameMeter.hxx"
#include "ember/SfvHitboxes.hxx"
#include "ember/SfvTraining.hxx"
#include "mod/HudText.hxx"
#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "common/NativeMenuLock.hxx"
#include "common/PadKind.hxx"
#include "common/ProductIdentity.hxx"

#include "netplay/SessionController.hxx"
#include "session/RoomQueries.hxx"
#include "ui/GameMenu.hxx"
#include "ui/MenuGlyphs.hxx"
#include "ui/Theme.hxx"
#include "ui/TrainingInRoom.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <algorithm>
#include <cfloat>

#include <windows.h>

#include <atomic>
#include <string>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;

std::wstring Wide(const std::string& utf8)
{
	const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
	std::wstring out((size_t)(length > 0 ? length : 0), L'\0');
	if (length > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), out.data(), length);
	return out;
}

// A side's name while a room's game is prepared or fought: the member in that
// seat of the local member's table (P1 the left plate, P2 the right). The HUD
// names its plates as the battle loads, before the runtime's session starts
// and takes the match's sides (NoteRuntimeMatchStart), so the table the game
// was launched from is the one that knows them then. Outside a room's game,
// the game's own label.
bool PlateName(int side, std::wstring& name)
{
	if (!EmberRuntimeRunning() || side < 0 || side > 1) return false;
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	if (!snapshot || !snapshot->room) return false;
	if (!sf4e::netplay::Fighting(snapshot->session.match)) {
		// Once per state: the HUD may ask again each round.
		static std::atomic<int> logged{-1};
		const int state = (int)snapshot->session.room * 16 + (int)snapshot->session.match;
		if (logged.exchange(state) != state)
			mod::Log("hud: the game's labels stay (room %s, match %s)", sf4e::input::RoomStateName(snapshot->session.room),
				sf4e::input::MatchStateName(snapshot->session.match));
		return false;
	}
	const sf4e::room::Snapshot& room = *snapshot->room;
	const sf4e::room::Table* table = sf4e::room::LocalTable(room);
	if (!table) return false;
	const sf4e::room::MemberId seated = side == 0 ? table->p1 : table->p2;
	for (const auto& member : room.members)
		if (seated != 0 && member.id == seated) name = Wide(member.name);
	return !name.empty();
}

// The room line's top, as a share of the screen's height: under SFV's timer
// and life bars, as SF4's line sits under SF4's (TrainingRoomStatusTop).
constexpr float kRoomLineTop = .23f;
// The banner's band across the middle of the screen.
constexpr float kBannerTop = .40f;

// Where a member waiting in Training stands in their room: one muted line on
// a band of the theme's ink, the two players' names shortened to fit as SF4's
// line fits them (TrainingRoomLine), within two fifths of the screen.
void DrawRoomLine(const sf4e::ui::TrainingRoomStatus& status)
{
	if (status.Empty()) return;
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	const float s = sf4e::ui::Scale(), size = ImGui::GetFontSize() * .9f;
	const auto width = [&](const std::string& text) { return font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x; };
	// The players' names give way, Ember's words keep their room: the two
	// names share what is left of two fifths of the screen.
	const bool hasRoom = !status.room.empty(), hasOpponent = !status.opponent.empty();
	const std::string mark = "W";
	const float fixed = width(sf4e::ui::TrainingRoomLine(status, hasRoom ? mark : std::string(), mark)) -
		(hasRoom ? width(mark) : 0) - (hasOpponent ? width(mark) : 0);
	const float spare = (std::max)(0.f, vp->Size.x * .4f - fixed), least = width("W...");
	const float roomWidth = hasRoom ? width(status.room) : 0, opponentWidth = hasOpponent ? width(status.opponent) : 0;
	const float half = hasRoom && hasOpponent ? spare / 2 : spare;
	const float roomFits = (std::min)(roomWidth, (std::max)(half, spare - (std::min)(opponentWidth, half)));
	const float opponentFits = (std::min)(opponentWidth, spare - roomFits);
	const auto fit = [&](const std::string& text, float room) {
		if (width(text) <= room) return text;
		std::string cut = text;
		while (!cut.empty() && width(cut + "...") > room) {
			do cut.pop_back(); while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80);
			if (!cut.empty() && static_cast<unsigned char>(cut.back()) >= 0xC0) cut.pop_back();
		}
		return cut + "...";
	};
	const std::string line = sf4e::ui::TrainingRoomLine(status,
		hasRoom ? fit(status.room, (std::max)(least, roomFits)) : std::string(),
		hasOpponent ? fit(status.opponent, (std::max)(least, opponentFits)) : std::string());
	sf4e::ui::NoteUserText(line);
	const float lineWidth = width(line), padX = 10 * s, padY = 4 * s;
	const ImVec2 at(vp->Pos.x + (vp->Size.x - lineWidth) / 2, vp->Pos.y + vp->Size.y * kRoomLineTop);
	const auto& colors = sf4e::ui::v2::Colors();
	draw->AddRectFilled(ImVec2(at.x - padX, at.y - padY), ImVec2(at.x + lineWidth + padX, at.y + size + padY),
		sf4e::ui::v2::color::Alpha(colors.ink, .72f));
	draw->AddText(font, size, at, colors.muted, line.c_str());
}

// The call back from Training: SF4's words across the middle of the screen
// on a band of ink closed by the theme's gold hairlines, as SFV's own bands
// are (section 17), the words in the gold and still rather than flashing.
// Under them, smaller: who sat down and their fighter, and go now's prompt.
void DrawChallengerBand(const sf4e::ui::ChallengerCall& call)
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	const auto& colors = sf4e::ui::v2::Colors();
	const ImU32 rule = sf4e::ui::palette::Rgb(ember::ProductTheme().rule);
	const std::string text = sf4e::ui::v2::Capitals(sf4e::loc::T("training.challenger"));
	// A long translation in a narrow window shrinks to nine tenths of its width.
	const float tall = vp->Size.y * .065f, wide = font->CalcTextSizeA(tall, FLT_MAX, 0, text.c_str()).x;
	const float size = wide > vp->Size.x * .9f ? tall * vp->Size.x * .9f / wide : tall;
	const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str());
	const float small = size * .42f, glyph = small * 1.4f, gap = small * 1.5f;
	const std::string goLabel = call.goNowGlyph
		? sf4e::ui::PromptLabel(call.goNowGlyph, sf4e::loc::T("training.go_now"), sf4e::ui::MenuPromptBrand())
		: std::string();
	const float goWidth = call.goNowGlyph ? glyph + small * .35f + font->CalcTextSizeA(small, FLT_MAX, 0, goLabel.c_str()).x : 0;
	std::string who = call.opponent;
	if (!who.empty() && ember::ValidFighter(call.fighter)) {
		who += "  \xC2\xB7  " + std::string(ember::Catalog().fighterName(call.fighter));
	}
	// Only the name gives way, so go now is never pushed off the screen.
	const float whoRoom = vp->Size.x * .9f - goWidth - (call.goNowGlyph ? gap : 0);
	if (!who.empty() && font->CalcTextSizeA(small, FLT_MAX, 0, who.c_str()).x > whoRoom) {
		const char* end = nullptr;
		font->CalcTextSizeA(small, (std::max)(1.f, whoRoom - font->CalcTextSizeA(small, FLT_MAX, 0, "...").x), 0, who.c_str(),
			nullptr, &end);
		who = std::string(who.c_str(), end) + "...";
	}
	const float whoWidth = who.empty() ? 0 : font->CalcTextSizeA(small, FLT_MAX, 0, who.c_str()).x;
	const bool second = !who.empty() || call.goNowGlyph;
	const float y = vp->Pos.y + vp->Size.y * kBannerTop, pad = size * .45f, below = second ? glyph + size * .3f : 0;
	const float top = y - pad, bottom = y + extent.y + pad + below, hairline = (std::max)(1.f, sf4e::ui::Scale());
	draw->AddRectFilled(ImVec2(vp->Pos.x, top), ImVec2(vp->Pos.x + vp->Size.x, bottom), sf4e::ui::v2::color::Alpha(colors.ink, .86f));
	draw->AddRectFilled(ImVec2(vp->Pos.x, top), ImVec2(vp->Pos.x + vp->Size.x, top + hairline), rule);
	draw->AddRectFilled(ImVec2(vp->Pos.x, bottom - hairline), ImVec2(vp->Pos.x + vp->Size.x, bottom), rule);
	draw->AddText(font, size, ImVec2(vp->Pos.x + (vp->Size.x - extent.x) / 2, y), colors.accent, text.c_str());
	if (!second) return;
	const float lineY = y + extent.y + size * .25f;
	float x = vp->Pos.x + (vp->Size.x - whoWidth - goWidth - (whoWidth && goWidth ? gap : 0)) / 2;
	if (!who.empty()) {
		sf4e::ui::NoteUserText(call.opponent);
		draw->AddText(font, small, ImVec2(x, lineY + (glyph - small) / 2), colors.text, who.c_str());
		x += whoWidth + gap;
	}
	if (call.goNowGlyph) {
		sf4e::ui::DrawPromptGlyph(draw, call.goNowGlyph, ImVec2(x, lineY), glyph, sf4e::ui::MenuArt(), glyph / 32);
		draw->AddText(font, small, ImVec2(x + glyph + small * .35f, lineY + (glyph - small) / 2), colors.text,
			goLabel.c_str());
	}
}

} // namespace

void DrawSfvTraining(const facade::PresentationSnapshot& frame, bool shellShown, unsigned unread, bool focused)
{
	if (!frame.runtime) return;
	const auto& snapshot = *frame.runtime;
	const TrainingView training = ReadTrainingView();
	if (!training.available) return;
	const bool paused = TrainingPaused();
	const bool inRoom = snapshot.session.room == sf4e::netplay::RoomState::Joined;
	// As SF4 Ember's overlay decides it (sf4e__Overlay.cxx): this battle is
	// called only while the room's call stands for it and its countdown runs.
	const auto& call = snapshot.trainingCall;
	const bool called = training.leavingIn > 0 && call.Live() && call.generation == training.generation &&
		training.leavingCall == call.serial;
	if (!shellShown && !paused && inRoom && !called) {
		DrawRoomLine(sf4e::ui::DescribeTrainingRoom(snapshot.Room(), unread));
	}
	// The fighters' boxes (docs/design/beta8/hitboxes.md) behind every
	// window, Ember's menu included, as SF4 Ember's lab draws them; never
	// over the game's pause menu.
	if (!paused) DrawSfvHitboxes();
	// Ember's frame meter (section 39), as SF4 Ember's Training HUD draws it,
	// under neither the shell nor the game's pause menu.
	if (!shellShown && !paused) DrawSfvFrameMeter();
	if (!called) return;
	sf4e::ui::ChallengerCall challenger;
	if (inRoom) challenger = sf4e::ui::DescribeChallenger(snapshot.Room(), call);
	challenger.called = true;
	// Behind another window only the pad's View can go now (SfvShell takes
	// it), so the banner offers that one there; Enter needs the focus.
	const bool padBehind =
		!focused && snapshot.inputDevice.connected && snapshot.inputDevice.type == sf4e::input::PadXInput;
	if ((focused || padBehind) && !shellShown && !paused) {
		challenger.goNowGlyph = sf4e::ui::GoNowGlyph(focused ? sf4e::ui::MenuPromptDevice() : sf4e::input::PadXInput);
	}
	DrawChallengerBand(challenger);
}

void InstallSfvHud()
{
	mod::SetPlateNames(PlateName);
}

void DrawSfvMatchHud(const facade::PresentationSnapshot& frame, bool shellShown)
{
	if (!frame.runtime) return;
	const auto& snapshot = *frame.runtime;
	const auto& status = frame.netplay;
	if (!frame.ggpoSessionActive) return;
	// As SF4 Ember's overlay fills it (sf4e__Overlay.cxx). The names are on
	// SFV's own plates; the HUD draws each one's marks beside them.
	sf4e::ui::MatchStripView strip;
	for (int side = 0; side < 2; ++side) {
		strip.names[side] = status.matchSides[side].name;
		strip.links[side] = status.matchSides[side].link;
		strip.badges[side] = snapshot.preferences.classEmblems ? status.matchSides[side].badge : sf4e::SkillBadge();
	}
	if (status.hasMatchScore) strip.score = sf4e::ui::SetScoreText(status.matchScore);
	strip.rollbackFrames = status.rollbackFrames;
	strip.pingMs = status.pingMs;
	strip.appliedDelay = status.appliedDelay;
	strip.spectator = status.spectator;
	strip.behindFrames = status.spectatorBacklog;
	strip.ownDelays = status.ownDelays;
	strip.queuePosition = status.queuePosition;
	strip.queueLength = status.queueLength;
	strip.size = snapshot.preferences.matchHudSize;
	strip.raised = snapshot.preferences.matchHudRaised;
	strip.anchor = snapshot.preferences.matchHudAnchor;
	strip.layout = snapshot.preferences.matchHudLayout;
	strip.spectators = status.spectators;
	strip.notice = status.lastError;
	strip.noticeSeverity = static_cast<int>(status.lastErrorSeverity);
	strip.connectionWarning = status.connectionWarning;
	strip.predictionStalled = status.predictionStalled;
	strip.disconnectCountdownMs = status.disconnectCountdownMs;
	// The match's part of DrawOverlayLayers (ui/OverlayLayers.cxx), which SFV
	// cannot take whole: it draws Training's layers too, and SFV has none. The
	// HUD when nothing is open over the fight; with the shell open, only a
	// notice that explains a stalled or ending fight.
	if (!shellShown && snapshot.preferences.showMatchHud) {
		sf4e::ui::DrawMatchStrip(strip);
		// A Practice room's fighter is told how the game ends (it has no KO
		// and no clock), as SF4's overlay tells them; SFV has no shared
		// save or reset to say anything of (EMBER_LIBRARY.md section 27).
		if (frame.localFighter && snapshot.practiceMatch && strip.notice.empty())
			sf4e::ui::DrawMatchNotice(sf4e::loc::T("training.match.end_hint"), 0);
		return;
	}
	if (!shellShown || (strip.noticeSeverity >= 2 && !strip.notice.empty()) || strip.predictionStalled ||
		(strip.connectionWarning && strip.disconnectCountdownMs >= 0)) {
		const int severity = strip.noticeSeverity >= 2 ? 2 : (strip.connectionWarning || strip.predictionStalled) ? 1 : strip.noticeSeverity;
		sf4e::ui::DrawMatchNotice(sf4e::ui::MatchStripStateLine(strip), severity);
	}
}

} // namespace sfve::adapter
