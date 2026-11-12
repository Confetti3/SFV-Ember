#pragma once

// The photo booth (development builds only, -sfve_mode=booth; the pipe
// protocol is docs/design/beta8/booth-protocol.md): a battle mode that steps
// an offline battle itself, one SfvAdapter::Advance per frame, so a driver can
// freeze it on any fight frame, play scripted inputs, scale the fighters, and
// take frame grabs (overlay/FrameGrab.hxx) of the frozen frame while the game
// keeps presenting. The render store's cutouts and loadout pictures are made
// with it (tools/runbooth.py).
//
// What the mode does for a battle:
//   - ends the intro with the skip bit (IntroDemo), as an offline battle does;
//   - fills both players' gauges once the layout is bound (SetPracticeGauges)
//     so a V-Trigger can be used at once;
//   - counts the fight frames (flow state Fight) it has advanced;
//   - holds, advancing nothing, at the fight frame `booth hold` names (a held
//     battle keeps presenting, as NetplayMode's freeze does), or advances `n`
//     more frames for `booth run n`, or plays scripted button steps for
//     `booth play`;
//   - leaves the battle (mod::LeaveBattle) for `booth leave`.
//
// A player build never makes this mode: ModConfig reads -sfve_mode=booth only
// in a development build (mod/BuildKind.hxx), and BuildKindTests checks it.
//
// The pure parts (the step and command parsers) are in namespace booth and
// need no game: PhotoBoothTests reaches them. Stream A-mod fills BoothMode.cxx
// and adds ModMode::Booth to ModConfig and MakeMode's case; this header is
// final.

#include "core/GameAdapter.hxx"
#include "mod/BattleMode.hxx"
#include "mod/ModConfig.hxx"
#include "overlay/FrameGrab.hxx"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::mod {

namespace booth {

// One step of a scripted input: `buttons` (the game's input bits,
// mod/GameInput.hxx) held for `frames` fight frames.
struct Step {
	PackedInput buttons = 0;
	int frames = 1;
};

// "MP+MK:4,RIGHT:40": comma-separated steps, each its buttons joined by '+'
// (UP DOWN LEFT RIGHT LP MP HP LK MK HK, or NONE for nothing), then ':' and a
// frame count of 1 or more (omitted: 1). Case-insensitive; no spaces. False,
// with the reason in `error` and `steps` left as it was, for any other text.
bool ParseSteps(std::string_view text, std::vector<Step>& steps, std::string& error);

// The fight frames `steps` take, all together.
int StepFrames(const std::vector<Step>& steps);
// What the steps press on their `frame`-th fight frame (from 0); none from
// StepFrames on, or for a negative frame.
std::optional<PackedInput> StepAt(const std::vector<Step>& steps, int frame);

enum class Verb { Hold, Run, Play, Scale, Grab, Depth, Hud, HudLog, Leave, HudHide, Hide };

// One `booth ...` pipe command, parsed. Which fields a verb uses:
//   Hold   frame (the fight frame to hold at)
//   Run    frame (how many frames to advance; the battle then holds)
//   Play   side (0 or 1), steps
//   Scale  scale[0], scale[1] (P1's and P2's size factors)
//   Grab   grab (path, afterPresents, depth)
//   Depth  nothing
//   Hud    method, number, text (optional)
//   HudLog on
//   Leave  on (`leave still`: the battle stands still until the menu comes)
//   HudHide on (the HUD hidden for every grab from now on, or shown again)
//   Hide   side (bit 0 P1, bit 1 P2: the fighters hidden; 0 shows both)
struct Command {
	Verb verb = Verb::Hold;
	int frame = 0;
	int side = 0;
	std::vector<Step> steps;
	float scale[2] = {1.f, 1.f};
	overlay::grab::Request grab;
	std::string method;
	float number = 0.f;
	std::string text;
	bool on = false;
};

// The argument text after the command name (`hold 600`, `grab out.bmp
// after=12 depth=auto`), parsed. None, with the reason in `error`, for an
// unknown verb, a missing or malformed argument, or one the verb does not
// take.
std::optional<Command> ParseCommand(std::string_view args, std::string& error);

} // namespace booth

class BoothMode : public BattleMode {
public:
	explicit BoothMode(const ModConfig& config) : config_(config) {}

	// Registers the `booth` command and the `booth` status field, installs the
	// overlay's present tap for the grabs and the HUD probe.
	void Install() override;
	void NewBattle(SfvAdapter& adapter) override;
	void Frame(SfvAdapter& adapter) override;
	// The status field's value: battle number, flow state, fight frame, held,
	// present count and the last grab, as JSON (booth-protocol.md).
	std::string Describe() const override;

private:
	// The pipe's `booth` (any thread): parsed, then either noted for the next
	// frame under the mutex (hold, run, play, leave), asked of the game thread
	// (scale, hud) or handed to the frame grab (grab, depth, hudlog).
	std::string Command(const std::string& args);
	// Under the mutex: a battle the commands act on (bound, not being left).
	bool InBattle() const { return battle_ != 0 && !leaveWanted_; }

	const ModConfig config_;
	mutable std::mutex mutex_;
	std::uint64_t battle_ = 0; // SfvAdapter::Battle of the battle in hand; 0 before any
	int flow_ = -1;            // the flow state at the last frame's start
	int fightFrame_ = -1;      // advances since the flow first said Fight; -1 before
	int holdAt_ = 0;           // the fight frame the battle holds at
	int nextHold_ = 0;         // a hold asked for between battles: the next battle's
	bool held_ = false;        // the last frame advanced nothing
	bool gauges_ = false;      // SetPracticeGauges took
	std::vector<booth::Step> steps_; // a `play` in progress
	int playSide_ = 0;
	int playStart_ = 0;        // the fight frame its first step plays on
	bool leaveWanted_ = false; // `booth leave`: the battle stands still until the menu replaces it
	bool leaving_ = false;     // LeaveBattle took
	bool leaveStill_ = false;  // `booth leave still`: no update after the leave (else the battle runs on, nobody pressing)
	bool loading_ = false;     // SFV's IsAsyncLoading at the last frame (sfv::kIsAsyncLoading)
	int loadedFrames_ = 0;     // frames in a row it has been false
};

} // namespace sfve::mod
