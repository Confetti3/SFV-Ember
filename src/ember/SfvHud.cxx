#include "ember/SfvHud.hxx"

#include "ember/EmberHost.hxx"
#include "mod/HudText.hxx"
#include "mod/Log.hxx"

#include "common/NativeMenuLock.hxx"

#include "netplay/SessionController.hxx"
#include "session/RoomQueries.hxx"
#include "ui/Theme.hxx"

#include <windows.h>

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
		mod::Log("hud: side %d keeps the game's label (room %s, match %s)", side + 1,
			sf4e::input::RoomStateName(snapshot->session.room), sf4e::input::MatchStateName(snapshot->session.match));
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

} // namespace

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
		return;
	}
	if (!shellShown || (strip.noticeSeverity >= 2 && !strip.notice.empty()) || strip.predictionStalled ||
		(strip.connectionWarning && strip.disconnectCountdownMs >= 0)) {
		const int severity = strip.noticeSeverity >= 2 ? 2 : (strip.connectionWarning || strip.predictionStalled) ? 1 : strip.noticeSeverity;
		sf4e::ui::DrawMatchNotice(sf4e::ui::MatchStripStateLine(strip), severity);
	}
}

} // namespace sfve::adapter
