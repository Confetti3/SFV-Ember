#include "mod/OfflineDialogs.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <wchar.h>

#include <atomic>
#include <memory>

namespace sfve::mod {

namespace {

using ShowErrorFn = uint64_t (*)(void* errorService, uint32_t code);
using ShowNamedDialogFn = void* (*)(const GameString* dialogId, const GameString* argument);

// How long the title may take to reach its wait after the notice (the notice
// can come before or after the title opens its boot window).
constexpr int kTitleWaitFrames = 60 * 30;

ShowErrorFn o_showError;
ShowNamedDialogFn o_showNamedDialog;
std::atomic<uint32_t> s_suppressed;
std::atomic<bool> s_bootNoticeHidden;
uintptr_t s_imageBase;

bool IsBlockedDownloadFailure(uint32_t code)
{
	for (uint32_t blocked : sfv::dialogs::kBlockedDownloadCodes) {
		if (code == blocked) return true;
	}
	return false;
}

// A listener on the game's error broadcast: the caller ignores the result.
// The service's own "no dialog for this code" path also returns 0.
uint64_t h_ShowError(void* errorService, uint32_t code)
{
	if (IsBlockedDownloadFailure(code)) {
		s_suppressed++;
		Log("dialogs: no error dialog for download failure %X (the mod keeps the game offline)", code);
		return 0;
	}
	Log("dialogs: game error %X left to the game's dialogs", code);
	return o_showError(errorService, code);
}

void ShowOfflineNotice()
{
	const size_t length = wcslen(sfv::dialogs::kOfflineNotice) + 1;
	const GameString id{sfv::dialogs::kOfflineNotice, (int32_t)length, (int32_t)length};
	const GameString noArgument{nullptr, 0, 0};
	o_showNamedDialog(&id, &noArgument); // copies both
}

// The title leaves for the main menu when its boot window says "Login",
// which offline it does only once the notice is closed. Without the notice,
// the mod takes that step itself once the title is waiting on the window:
// the same two writes the window's "Login" command makes (rva 0x5EA630).
// Should the title never wait that way, the notice is shown after all, so
// the player can always go on.
void LeaveTitleWhenItWaits()
{
	auto frames = std::make_shared<int>(0);
	RunEachGameFrame([frames] {
		uint8_t* state = CurrentSceneState();
		auto* step = state ? reinterpret_cast<uintptr_t*>(state + sfv::title::kStep) : nullptr;
		if (SceneStateKind(state) == sfv::title::kVtable && *step == s_imageBase + sfv::scene::kIdleStep) {
			*step = s_imageBase + sfv::kTitleLeaveStep.rva;
			*reinterpret_cast<int*>(state + sfv::title::kStepDelay) = sfv::title::kLoginDelayFrames;
			Log("dialogs: the title goes on to the main menu");
			return false;
		}
		if (SceneStateKind(state) == sfv::scene::kMainMenuVtable) return false; // it got there by itself
		if (++*frames < kTitleWaitFrames) return true;
		Log("dialogs: the title never waited for its boot window; showing the offline notice after all");
		ShowOfflineNotice();
		return false;
	});
}

// Callers ignore the result (the posted message's shared reference is
// released inside); nothing waits on the dialog itself. Only the first
// notice, the one boot posts, is hidden: a later one (a failed search)
// shows, and the title is not touched again.
void* h_ShowNamedDialog(const GameString* dialogId, const GameString* argument)
{
	if (dialogId && dialogId->data && wcscmp(dialogId->data, sfv::dialogs::kOfflineNotice) == 0 &&
		!s_bootNoticeHidden.exchange(true)) {
		s_suppressed++;
		Log("dialogs: no offline-mode notice");
		LeaveTitleWhenItWaits();
		return nullptr;
	}
	return o_showNamedDialog(dialogId, argument);
}

} // namespace

bool InstallOfflineDialogs(uintptr_t imageBase, OfflineDialogs level)
{
	if (level == OfflineDialogs::Show) {
		Log("dialogs: every dialog shows (by request)");
		return true;
	}
	s_imageBase = imageBase;
	bool ok = HookCodeSite(imageBase, sfv::kShowErrorDialog, (void*)h_ShowError, (void**)&o_showError);
	if (ok && level == OfflineDialogs::HideAll) {
		ok = HookCodeSite(imageBase, sfv::kShowNamedDialog, (void*)h_ShowNamedDialog, (void**)&o_showNamedDialog);
	}
	Log("dialogs: %s%s", ok ? "blocked-download errors suppressed" : "NOT all suppressed",
		ok && level == OfflineDialogs::HideAll ? ", and the offline notice" : "");
	return ok;
}

uint32_t SuppressedDialogs()
{
	return s_suppressed;
}

} // namespace sfve::mod
