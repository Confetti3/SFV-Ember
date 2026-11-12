#include "mod/BoothMode.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameInput.hxx"
#include "mod/GameThread.hxx"
#include "mod/HudText.hxx"
#include "mod/IntroSkip.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/SfvBattleLayouts.hxx"
#include "mod/TinyFighters.hxx"
#include "overlay/Overlay.hxx"

#include <windows.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <memory>
#include <system_error>
#include <utility>

namespace sfve::mod {

namespace booth {

namespace {

// Bounds no driver needs to pass: a battle of ten minutes, a step of one, a
// script of 64 steps, a grab a minute of presents away.
constexpr int kMaxFightFrame = 60 * 60 * 10;
constexpr int kMaxStepFrames = 60 * 60;
constexpr size_t kMaxSteps = 64;
constexpr int kMaxAfterPresents = 60 * 60;

struct ButtonName {
	std::string_view name;
	PackedInput bits;
};
constexpr ButtonName kButtons[] = {{"UP", input::kUp}, {"DOWN", input::kDown}, {"LEFT", input::kLeft},
	{"RIGHT", input::kRight}, {"LP", input::kLP}, {"MP", input::kMP}, {"HP", input::kHP}, {"LK", input::kLK},
	{"MK", input::kMK}, {"HK", input::kHK}, {"NONE", 0}};

bool SameIgnoringCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) {
		const char x = a[i] >= 'a' && a[i] <= 'z' ? (char)(a[i] - 32) : a[i];
		if (x != b[i]) return false;
	}
	return true;
}

// A whole decimal integer in [low, high]; none for anything else.
std::optional<int> Integer(std::string_view text, int low, int high)
{
	if (text.empty() || text[0] == '+' || (text[0] == '-' && low >= 0)) return std::nullopt;
	int value = 0;
	const auto [at, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc() || at != text.data() + text.size() || value < low || value > high) return std::nullopt;
	return value;
}

// A whole decimal number that is finite; none for anything else.
std::optional<float> Number(std::string_view text)
{
	if (text.empty() || text[0] == '+') return std::nullopt;
	float value = 0.f;
	const auto [at, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc() || at != text.data() + text.size() || !std::isfinite(value)) return std::nullopt;
	return value;
}

// Splits off the next space-separated word of `rest` (leading spaces
// skipped); empty at the end.
std::string_view NextWord(std::string_view& rest)
{
	const size_t start = rest.find_first_not_of(' ');
	if (start == std::string_view::npos) {
		rest = {};
		return {};
	}
	rest.remove_prefix(start);
	const size_t end = rest.find(' ');
	const std::string_view word = rest.substr(0, end);
	rest = end == std::string_view::npos ? std::string_view() : rest.substr(end);
	return word;
}

bool OnlySpaces(std::string_view text)
{
	return text.find_first_not_of(' ') == std::string_view::npos;
}

} // namespace

bool ParseSteps(std::string_view text, std::vector<Step>& steps, std::string& error)
{
	if (text.empty()) return error = "no steps", false;
	if (text.find_first_of(" \t") != std::string_view::npos) return error = "steps have no spaces", false;
	std::vector<Step> parsed;
	while (true) {
		const size_t comma = text.find(',');
		const std::string_view item = text.substr(0, comma);
		if (item.empty()) return error = "an empty step", false;
		Step step;
		const size_t colon = item.find(':');
		std::string_view buttons = item.substr(0, colon);
		if (colon != std::string_view::npos) {
			const std::optional<int> frames = Integer(item.substr(colon + 1), 1, kMaxStepFrames);
			if (!frames) return error = "step \"" + std::string(item) + "\": frames are 1 to " + std::to_string(kMaxStepFrames), false;
			step.frames = *frames;
		}
		bool none = false;
		int names = 0;
		while (true) {
			const size_t plus = buttons.find('+');
			const std::string_view name = buttons.substr(0, plus);
			const ButtonName* found = nullptr;
			for (const ButtonName& b : kButtons) {
				if (SameIgnoringCase(name, b.name)) found = &b;
			}
			if (!found) return error = "step \"" + std::string(item) + "\": no button \"" + std::string(name) + "\"", false;
			none = none || found->bits == 0;
			step.buttons |= found->bits;
			names++;
			if (plus == std::string_view::npos) break;
			buttons.remove_prefix(plus + 1);
		}
		if (none && names > 1) return error = "step \"" + std::string(item) + "\": NONE stands alone", false;
		if ((step.buttons & (input::kLeft | input::kRight)) == (input::kLeft | input::kRight) ||
			(step.buttons & (input::kUp | input::kDown)) == (input::kUp | input::kDown)) {
			return error = "step \"" + std::string(item) + "\": opposite directions", false;
		}
		parsed.push_back(step);
		if (parsed.size() > kMaxSteps) return error = "more than " + std::to_string(kMaxSteps) + " steps", false;
		if (comma == std::string_view::npos) break;
		text.remove_prefix(comma + 1);
	}
	steps = std::move(parsed);
	return true;
}

int StepFrames(const std::vector<Step>& steps)
{
	int total = 0;
	for (const Step& step : steps) total += step.frames;
	return total;
}

std::optional<PackedInput> StepAt(const std::vector<Step>& steps, int frame)
{
	if (frame < 0) return std::nullopt;
	for (const Step& step : steps) {
		if (frame < step.frames) return step.buttons;
		frame -= step.frames;
	}
	return std::nullopt;
}

std::optional<Command> ParseCommand(std::string_view args, std::string& error)
{
	std::string_view rest = args;
	const std::string_view verb = NextWord(rest);
	Command c;
	const auto none = [&](Verb v) -> std::optional<Command> {
		if (!OnlySpaces(rest)) return error = std::string(verb) + " takes nothing", std::nullopt;
		c.verb = v;
		return c;
	};
	if (verb.empty()) return error = "usage: booth hold|run|play|scale|grab|depth|hud|hudlog|leave ...", std::nullopt;
	if (verb == "hold" || verb == "run") {
		const bool hold = verb == "hold";
		const std::optional<int> frame = Integer(NextWord(rest), hold ? 0 : 1, kMaxFightFrame);
		if (!frame || !OnlySpaces(rest)) {
			return error = hold ? "usage: booth hold <fight frame, 0 to " + std::to_string(kMaxFightFrame) + ">"
								: "usage: booth run <frames, 1 to " + std::to_string(kMaxFightFrame) + ">",
				   std::nullopt;
		}
		c.verb = hold ? Verb::Hold : Verb::Run;
		c.frame = *frame;
		return c;
	}
	if (verb == "play") {
		const std::optional<int> side = Integer(NextWord(rest), 0, 1);
		const std::string_view steps = NextWord(rest);
		if (!side || steps.empty() || !OnlySpaces(rest)) return error = "usage: booth play <0|1> <steps>", std::nullopt;
		std::string why;
		if (!ParseSteps(steps, c.steps, why)) return error = why, std::nullopt;
		c.verb = Verb::Play;
		c.side = *side;
		return c;
	}
	if (verb == "scale") {
		const std::optional<float> p1 = Number(NextWord(rest));
		const std::optional<float> p2 = Number(NextWord(rest));
		if (!p1 || !p2 || !OnlySpaces(rest)) return error = "usage: booth scale <p1> <p2>", std::nullopt;
		if (!tiny::UsableFactor(*p1) || !tiny::UsableFactor(*p2)) return error = "scale: factors are positive", std::nullopt;
		c.verb = Verb::Scale;
		c.scale[0] = *p1;
		c.scale[1] = *p2;
		return c;
	}
	if (verb == "grab") {
		// The path is one word, or "a quoted path" with spaces.
		const size_t start = rest.find_first_not_of(' ');
		if (start == std::string_view::npos) return error = "usage: booth grab <path> [after=<presents>] [depth=auto|<n>|off]", std::nullopt;
		rest.remove_prefix(start);
		if (rest[0] == '"') {
			const size_t close = rest.find('"', 1);
			if (close == std::string_view::npos || close == 1) return error = "grab: the quoted path is not closed", std::nullopt;
			c.grab.path = std::string(rest.substr(1, close - 1));
			rest.remove_prefix(close + 1);
			if (!rest.empty() && rest[0] != ' ') return error = "grab: a space after the quoted path", std::nullopt;
		} else {
			c.grab.path = std::string(NextWord(rest));
		}
		for (std::string_view option = NextWord(rest); !option.empty(); option = NextWord(rest)) {
			if (option.substr(0, 6) == "after=") {
				const std::optional<int> after = Integer(option.substr(6), 0, kMaxAfterPresents);
				if (!after) return error = "grab: after= is 0 to " + std::to_string(kMaxAfterPresents) + " presents", std::nullopt;
				c.grab.afterPresents = *after;
			} else if (option.substr(0, 6) == "depth=") {
				if (!overlay::grab::ParseDepthChoice(std::string(option.substr(6)), c.grab.depth)) {
					return error = "grab: depth= is auto, off or a target's number", std::nullopt;
				}
			} else {
				return error = "grab: no option \"" + std::string(option) + "\"", std::nullopt;
			}
		}
		c.verb = Verb::Grab;
		return c;
	}
	if (verb == "depth") return none(Verb::Depth);
	if (verb == "hud") {
		const std::string_view method = NextWord(rest);
		const std::optional<float> number = Number(NextWord(rest));
		if (method.empty() || !number) return error = "usage: booth hud <method> <number> [text]", std::nullopt;
		for (const char ch : method) {
			if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) {
				return error = "hud: a method name is letters, digits and _", std::nullopt;
			}
		}
		c.verb = Verb::Hud;
		c.method = std::string(method);
		c.number = *number;
		// The rest of the line, after the one space that ends the number.
		if (!rest.empty()) c.text = std::string(rest.substr(1));
		return c;
	}
	if (verb == "hudlog") {
		const std::string_view state = NextWord(rest);
		if ((state != "on" && state != "off") || !OnlySpaces(rest)) return error = "usage: booth hudlog on|off", std::nullopt;
		c.verb = Verb::HudLog;
		c.on = state == "on";
		return c;
	}
	if (verb == "leave") {
		// "leave still": the battle stands still until the menu replaces it, as before.
		const std::string_view how = NextWord(rest);
		if ((!how.empty() && how != "still") || !OnlySpaces(rest)) return error = "usage: booth leave [still]", std::nullopt;
		c.verb = Verb::Leave;
		c.on = how == "still";
		return c;
	}
	if (verb == "hide") {
		const std::optional<int> p1 = Integer(NextWord(rest), 0, 1);
		const std::optional<int> p2 = Integer(NextWord(rest), 0, 1);
		if (!p1 || !p2 || !OnlySpaces(rest)) return error = "usage: booth hide <0|1> <0|1>", std::nullopt;
		c.verb = Verb::Hide;
		c.side = *p1 | (*p2 << 1);
		return c;
	}
	if (verb == "hudhide") {
		const std::string_view state = NextWord(rest);
		if ((state != "on" && state != "off") || !OnlySpaces(rest)) return error = "usage: booth hudhide on|off", std::nullopt;
		c.verb = Verb::HudHide;
		c.on = state == "on";
		return c;
	}
	return error = "no booth command \"" + std::string(verb) + "\"", std::nullopt;
}

} // namespace booth

namespace {

// How long the pipe waits for the game thread to answer: many frames of a
// running game, and well inside the driver's own wait.
constexpr auto kGameThreadWait = std::chrono::seconds(2);

// Runs `work` on the game thread (GameThread's queue, pumped once a frame by
// the scene manager in menus and battles alike) and returns its answer, or
// "error <what>: ..." when the game thread does not take it in time; taken
// back while still queued, so it never runs later.
std::string AskGameThread(std::function<std::string()> work, const char* what)
{
	struct Ask {
		std::mutex mutex;
		bool taken = false;
		bool dropped = false;
		std::function<std::string()> work;
		std::promise<std::string> answer;
	};
	auto ask = std::make_shared<Ask>();
	ask->work = std::move(work);
	std::future<std::string> answer = ask->answer.get_future();
	RunOnGameThread([ask] {
		{
			std::lock_guard lock(ask->mutex);
			if (ask->dropped) return;
			ask->taken = true;
		}
		ask->answer.set_value(ask->work());
	});
	if (answer.wait_for(kGameThreadWait) == std::future_status::ready) return answer.get();
	{
		std::lock_guard lock(ask->mutex);
		if (!ask->taken) {
			ask->dropped = true;
			return std::string("error ") + what + ": the game thread did not answer";
		}
	}
	return answer.get();
}

std::wstring Wide(const std::string& utf8)
{
	const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
	std::wstring out((size_t)(length > 0 ? length : 0), L'\0');
	if (length > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), out.data(), length);
	return out;
}

} // namespace

void BoothMode::Install()
{
	// The booth's own picture tools: the scale of each side's fighter, the
	// grab at Present (the overlay draws nothing here: no interface is set in
	// this mode, overlay/Overlay.cxx's Draw), and the depth targets' counts.
	SetFighterScaleAllowed(true);
	InstallHudHide(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
	overlay::SetPresentTap([](void* swapChain) { overlay::grab::OnPresent(swapChain); });
	overlay::grab::InstallDepthProbe();
	RegisterCommand("booth", [this](const std::string& args) { return Command(args); });
	RegisterStatus("booth", [this] { return Describe(); });
	Log("booth: the photo booth is on (development build): battles step themselves and hold at fight frame 0");
}

void BoothMode::NewBattle(SfvAdapter& adapter)
{
	std::lock_guard lock(mutex_);
	// A battle the booth did not leave (it ended its own way): the last
	// battle's HUD movie is gone with it. One the booth left forgot it then.
	if (battle_ != 0 && !leaveWanted_) ForgetHudMovie();
	ForgetHudParts(); // the hidden switch stays: this battle's parts hide as they update
	battle_ = adapter.Battle();
	flow_ = adapter.FlowState();
	fightFrame_ = -1;
	holdAt_ = nextHold_;
	nextHold_ = 0;
	held_ = false;
	steps_.clear();
	leaveWanted_ = leaving_ = false;
	// No booth scale and no part of the last battle's; this battle's parts
	// are remembered at their next update.
	ForgetFighterScale();
	// Training's refilling gauges, as a Practice room's match has them, so a
	// V-Trigger plays at once and nobody is knocked out.
	gauges_ = adapter.SetPracticeGauges();
	Log("booth: battle %llu bound, holds at fight frame %d; %s", (unsigned long long)battle_, holdAt_,
		gauges_ ? "Practice gauges set" : "the battle keeps a match's gauges");
}

void BoothMode::Frame(SfvAdapter& adapter)
{
	FrameInputs inputs;
	bool runOn = false;
	{
		std::lock_guard lock(mutex_);
		flow_ = adapter.FlowState();
		if (leaveWanted_) {
			held_ = true;
			if (!leaving_) {
				ForgetFighterScale();
				ForgetHudMovie();
				ForgetHudParts();
				leaving_ = LeaveBattle();
				if (!leaving_) Log("booth: could not leave the battle yet (no scene manager); asking again next frame");
			}
			// Until the main menu replaces it the battle runs on as the game's own
			// Exit leaves it running (nobody pressing anything): whatever the
			// battle does on its way out is done. `leave still` stands still.
			runOn = !leaveStill_;
		}
	}
	if (runOn) adapter.RunOriginal(true);
	{
		std::lock_guard lock(mutex_);
		if (leaveWanted_) return;
		if (fightFrame_ < 0 && flow_ == sfv::flow::kFight) {
			fightFrame_ = 0;
			Log("booth: battle %llu fights", (unsigned long long)battle_);
		}
		if (fightFrame_ >= 0 && fightFrame_ >= holdAt_) {
			if (!held_) Log("booth: held at fight frame %d", fightFrame_);
			held_ = true;
			steps_.clear();
			return;
		}
		held_ = false;
		// The intro ends on the skip bit (sfv::intro: any pad's value the
		// battle reads while IntroDemo runs, when the battle's intros are
		// skippable); a script plays its side's steps, the other side nothing.
		if (flow_ == sfv::flow::kIntroDemo) inputs.p[0] = inputs.p[1] = intro_skip::kSkipBit;
		if (!steps_.empty() && fightFrame_ >= 0) {
			if (const std::optional<PackedInput> bits = booth::StepAt(steps_, fightFrame_ - playStart_)) inputs.p[playSide_] = *bits;
		}
	}
	// One update with those inputs (no session: nothing rolls back).
	adapter.Advance(inputs, false);
	std::lock_guard lock(mutex_);
	if (fightFrame_ >= 0) fightFrame_++;
}

std::string BoothMode::Command(const std::string& args)
{
	std::string error;
	const std::optional<booth::Command> command = booth::ParseCommand(args, error);
	if (!command) return "error " + error;
	const booth::Command& c = *command;
	switch (c.verb) {
	case booth::Verb::Hold: {
		std::lock_guard lock(mutex_);
		if (!InBattle()) {
			// Between battles (before the first, or after a leave): the next
			// battle holds there.
			nextHold_ = c.frame;
			return "ok";
		}
		if (fightFrame_ > c.frame) return "error frame " + std::to_string(c.frame) + " is past";
		holdAt_ = c.frame;
		steps_.clear();
		return "ok";
	}
	case booth::Verb::Run:
	case booth::Verb::Play: {
		std::lock_guard lock(mutex_);
		if (!InBattle()) return "error no battle";
		if (fightFrame_ < 0) return "error the fight has not started";
		if (!steps_.empty()) return "error a play is running";
		if (c.verb == booth::Verb::Run) {
			holdAt_ = fightFrame_ + c.frame;
		} else {
			steps_ = c.steps;
			playSide_ = c.side;
			playStart_ = fightFrame_;
			holdAt_ = fightFrame_ + booth::StepFrames(c.steps);
		}
		return "ok";
	}
	case booth::Verb::Leave: {
		std::lock_guard lock(mutex_);
		if (!InBattle()) return "error no battle";
		leaveWanted_ = true;
		leaveStill_ = c.on;
		return "ok";
	}
	case booth::Verb::Scale: {
		const float p1 = c.scale[0], p2 = c.scale[1];
		return AskGameThread(
			[this, p1, p2] {
				{
					std::lock_guard lock(mutex_);
					if (!InBattle()) return std::string("error scale: no battle");
				}
				return ApplyFighterScaleNow(p1, p2) ? std::string("ok")
													: std::string("error scale: a side's fighter is not known yet");
			},
			"scale");
	}
	case booth::Verb::Hud: {
		const std::wstring method = Wide(c.method), text = Wide(c.text);
		const float number = c.number;
		return AskGameThread(
			[method, text, number] {
				if (!HudMovieKnown()) return std::string("error hud: no movie");
				return HudCall(method, number, text) ? std::string("ok") : std::string("error hud: the call could not be made");
			},
			"hud");
	}
	case booth::Verb::HudLog: SetHudCallLog(c.on); return "ok";
	case booth::Verb::Hide: {
		const bool p1 = (c.side & 1) != 0, p2 = (c.side & 2) != 0;
		return AskGameThread(
			[this, p1, p2] {
				{
					std::lock_guard lock(mutex_);
					if (!InBattle()) return std::string("error hide: no battle");
				}
				return SetFightersHiddenNow(p1, p2) ? std::string("ok") : std::string("error hide: a side's fighter is not known yet");
			},
			"hide");
	}
	case booth::Verb::HudHide: {
		const bool hidden = c.on;
		return AskGameThread(
			[hidden] {
				const int parts = SetHudHidden(hidden);
				if (parts < 0) return std::string("error hudhide: the HUD parts' update is not hooked");
				return "ok " + std::to_string(parts);
			},
			"hudhide");
	}
	case booth::Verb::Grab:
		if (!overlay::grab::QueueGrab(c.grab, error)) return "error " + error;
		return "ok";
	case booth::Verb::Depth: return "ok " + overlay::grab::DepthReport();
	}
	return "error unknown booth command";
}

std::string BoothMode::Describe() const
{
	std::lock_guard lock(mutex_);
	if (battle_ == 0) return "null";
	const overlay::grab::Status grab = overlay::grab::CurrentStatus();
	std::string json = "{\"battle\":" + std::to_string(battle_) + ",\"flow\":" + std::to_string(flow_) +
		",\"fightFrame\":" + std::to_string(fightFrame_) + ",\"held\":" + (held_ ? "true" : "false") +
		",\"holdAt\":" + std::to_string(holdAt_) + ",\"playing\":" + (steps_.empty() ? "false" : "true") +
		",\"leaving\":" + (leaveWanted_ ? "true" : "false") + ",\"gauges\":" + (gauges_ ? "true" : "false") +
		",\"hudMovie\":" + (HudMovieKnown() ? "true" : "false") + ",\"hudHidden\":" + (HudHidden() ? "true" : "false") +
		",\"hudParts\":" + std::to_string(HudPartsKnown()) + ",\"presents\":" + std::to_string(grab.presents) +
		",\"pending\":" + (grab.pending ? "true" : "false") + ",\"depthProbe\":" + (grab.depthProbe ? "true" : "false") +
		",\"lastGrab\":";
	if (grab.lastPath.empty()) return json + "null}";
	return json + "{\"path\":" + JsonString(grab.lastPath) + ",\"ok\":" + (grab.lastOk ? "true" : "false") +
		",\"error\":" + JsonString(grab.lastError) + ",\"present\":" + std::to_string(grab.lastPresent) +
		",\"depth\":" + JsonString(grab.lastDepth) + "}}";
}

} // namespace sfve::mod
