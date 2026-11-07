#include "overlay/Overlay.hxx"

#include "mod/Log.hxx"
#include "overlay/TextureUploaderD3D11.hxx"

#include "platform/GameWindowSection.hxx"

#include "ui/GameMenu.hxx"
#include "ui/OverlayLifecycle.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/Theme.hxx"
#include "ui/Win32Input.hxx"

#include <MinHook.h>

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <xinput.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace sfve::overlay {

namespace {

using mod::Log;

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain* swap, UINT syncInterval, UINT flags);
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain* swap, UINT count, UINT width, UINT height, DXGI_FORMAT format,
	UINT flags);

// IDXGISwapChain's vtable: IUnknown (3), IDXGIObject (4), IDXGIDeviceSubObject
// (1), then Present, GetBuffer, Set/GetFullscreenState, GetDesc,
// ResizeBuffers...
constexpr int kPresentSlot = 8;
constexpr int kResizeBuffersSlot = 13;

PresentFn o_present;
ResizeBuffersFn o_resizeBuffers;

std::mutex s_uiMutex;
OverlayUi s_ui;              // as set; copied to the render thread under the mutex
std::atomic<bool> s_haveUi;

// What the render thread last published, for the window procedure.
std::atomic<bool> s_shown, s_available;
std::atomic<bool> s_focused{true};
std::atomic<HWND> s_window; // the game's window, once its swap chain presents
std::atomic<WNDPROC> s_gameWndProc;

// The window's ImGui input, applied by the render thread, and the guard
// around the context's backends (OverlayLifecycle.hxx).
sf4e::ui::Win32InputBridge s_inputBridge;
sf4e::ui::OverlayLifecycle s_lifecycle;
// Written on the render thread only, from Draw.
sf4e::platform::GameWindowPublisher s_windowPublisher;

// Render thread only.
struct Renderer {
	IDXGISwapChain* swap = nullptr; // not owned: the game's
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	ID3D11RenderTargetView* target = nullptr;
	std::unique_ptr<sf4e::ui::SelectionArt> art;
	bool ready = false;
};
Renderer s_r;

template <typename T>
void Release(T*& object)
{
	if (object) object->Release();
	object = nullptr;
}

// A shader stage's binding, held (with references) to put back.
template <typename Shader>
struct ShaderBinding {
	static constexpr UINT kMaxInstances = 256;
	Shader* shader = nullptr;
	ID3D11ClassInstance* instances[kMaxInstances] = {};
	UINT count = kMaxInstances;
	~ShaderBinding()
	{
		Release(shader);
		for (UINT i = 0; i < count && i < kMaxInstances; i++) Release(instances[i]);
	}
};

// Ahead of the game's window procedure. With an interface set, keyboard and
// mouse reach ImGui; while it is shown they are its own (key releases still
// reach the game, so a key held when it opened is not left down there), and
// F10, which shows and hides it, is never the game's where it can be shown.
LRESULT CALLBACK OverlayWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_ACTIVATEAPP) {
		s_focused = wParam != 0;
		if (!wParam) s_inputBridge.RequestClear(); // nothing stays held while away
	}
	if (s_haveUi) {
		const sf4e::ui::OverlayLifecycle::Message handling(s_lifecycle);
		if (ImGui::GetCurrentContext() && s_r.ready) {
			const LRESULT handled =
				sf4e::ui::HandleOverlayMessage(window, message, wParam, lParam, s_shown && s_focused, s_available);
			if (handled && message != WM_KEYUP && message != WM_SYSKEYUP) return handled;
		}
	}
	return CallWindowProcW(s_gameWndProc, window, message, wParam, lParam);
}

bool IsGameWindow(HWND window)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	return window && pid == GetCurrentProcessId() && IsWindowVisible(window) && GetAncestor(window, GA_ROOT) == window;
}

// The folder a module was loaded from: the game's exe (null) or the mod.
std::wstring FolderOf(HMODULE module)
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(module, path, MAX_PATH);
	std::wstring folder(path);
	return folder.substr(0, folder.find_last_of(L"\\/"));
}

// The picture's size, for layout and the mouse: the game may draw at another
// size than its window's client area.
void FollowRenderSize(const DXGI_SWAP_CHAIN_DESC& desc)
{
	ImGui_ImplWin32_SetRenderSize((float)desc.BufferDesc.Width, (float)desc.BufferDesc.Height);
}

// Takes the game's swap chain: ImGui's backends on its window and device,
// and Ember's pictures on that device.
bool Attach(IDXGISwapChain* swap, const DXGI_SWAP_CHAIN_DESC& desc)
{
	const sf4e::ui::OverlayLifecycle::Change change(s_lifecycle);
	if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_r.device)))) return false;
	s_r.device->GetImmediateContext(&s_r.context);
	if (!ImGui::GetCurrentContext()) {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr; // nothing written next to the game
		io.LogFilename = nullptr;
		ImGui::GetPlatformIO().Platform_SetImeDataFn = nullptr;
		sf4e::ui::SetAtlasBuildLog([](const char* line) { Log("overlay: %s", line); });
	}
	const HWND window = desc.OutputWindow;
	ImGui_ImplWin32_Init(window);
	s_inputBridge.RequestClear(); // nothing queued for backends that are gone
	ImGui_ImplWin32_SetInputBridge(&s_inputBridge);
	FollowRenderSize(desc);
	ImGui_ImplDX11_Init(s_r.device, s_r.context);
	s_r.art = std::make_unique<sf4e::ui::SelectionArt>(MakeTextureUploaderD3D11(s_r.device), FolderOf(nullptr),
		mod::ModuleFolder() + L"\\assets\\selection", [](const std::string& line) { Log("art: %s", line.c_str()); });
	sf4e::ui::SetMenuArt(s_r.art.get());
	s_r.swap = swap;
	s_r.ready = true;
	s_window = window;
	if (!s_gameWndProc) {
		// Stored before the switch: the game thread may call OverlayWndProc
		// as soon as it is in place.
		s_gameWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
		s_gameWndProc = reinterpret_cast<WNDPROC>(
			SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&OverlayWndProc)));
	}
	Log("overlay: drawing on swap chain %p (window %p), %ux%u", (void*)swap, (void*)window, desc.BufferDesc.Width,
		desc.BufferDesc.Height);
	return true;
}

void Detach()
{
	const sf4e::ui::OverlayLifecycle::Change change(s_lifecycle);
	Release(s_r.target);
	sf4e::ui::SetMenuArt(nullptr);
	s_r.art.reset();
	if (s_r.ready) {
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
	}
	Release(s_r.context);
	Release(s_r.device);
	s_r = Renderer{};
	s_shown = false;
}

// Draws the frame's ImGui over the picture, leaving the engine's pipeline
// as it was. ImGui's backend restores the state it changes, except the
// render targets and the hull, domain and compute shaders it unbinds (the
// engine's state cache would otherwise think them still bound).
void Render(ImDrawData* data)
{
	ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* depth = nullptr;
	ShaderBinding<ID3D11HullShader> hull;
	ShaderBinding<ID3D11DomainShader> domain;
	ShaderBinding<ID3D11ComputeShader> compute;
	s_r.context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth);
	s_r.context->HSGetShader(&hull.shader, hull.instances, &hull.count);
	s_r.context->DSGetShader(&domain.shader, domain.instances, &domain.count);
	s_r.context->CSGetShader(&compute.shader, compute.instances, &compute.count);
	s_r.context->OMSetRenderTargets(1, &s_r.target, nullptr);
	ImGui_ImplDX11_RenderDrawData(data);
	s_r.context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depth);
	s_r.context->HSSetShader(hull.shader, hull.instances, hull.count);
	s_r.context->DSSetShader(domain.shader, domain.instances, domain.count);
	s_r.context->CSSetShader(compute.shader, compute.instances, compute.count);
	for (ID3D11RenderTargetView*& t : targets) Release(t);
	Release(depth);
}

void Draw(IDXGISwapChain* swap)
{
	DXGI_SWAP_CHAIN_DESC desc{};
	if (FAILED(swap->GetDesc(&desc)) || !IsGameWindow(desc.OutputWindow)) return;
	// The game's window for the launcher (platform/GameWindowSection.hxx):
	// its freeze watch reads whether the window answers, and a second
	// launcher started by a link brings it forward.
	s_windowPublisher.Publish(desc.OutputWindow);
	OverlayUi ui;
	{
		std::lock_guard lock(s_uiMutex);
		ui = s_ui;
	}
	if (!ui.draw) return; // no interface: nothing to draw
	if (s_r.ready && s_r.swap != swap) {
		if (desc.OutputWindow != s_window.load()) return; // another window's swap chain
		Log("overlay: the game's swap chain changed");
		Detach();
	}
	if (!s_r.ready && !Attach(swap, desc)) return Detach();
	const sf4e::ui::OverlayLifecycle::Frame drawing(s_lifecycle);
	if (!drawing) return;
	if (!s_r.target) {
		ID3D11Texture2D* backBuffer = nullptr;
		if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) return;
		s_r.device->CreateRenderTargetView(backBuffer, nullptr, &s_r.target);
		backBuffer->Release();
		if (!s_r.target) return;
		FollowRenderSize(desc); // a resize gives a new back buffer
	}
	// Ember covers the same share of the picture at every resolution.
	if (ui.applyScale && ui.applyScale((float)desc.BufferDesc.Height / 1080.0f)) ImGui_ImplDX11_InvalidateDeviceObjects();
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	if (ui.beginFrame) ui.beginFrame();
	ImGui::NewFrame();
	s_r.art->Pump();
	const OverlayUi::Shown shown = ui.draw();
	s_shown = shown.shown;
	s_available = shown.available;
	ImGui::Render();
	ImDrawData* data = ImGui::GetDrawData();
	if (data->CmdListsCount) Render(data);
}

HRESULT WINAPI h_Present(IDXGISwapChain* swap, UINT syncInterval, UINT flags)
{
	if (!(flags & DXGI_PRESENT_TEST)) Draw(swap);
	return o_present(swap, syncInterval, flags);
}

// The back buffers can only be resized once nothing refers to them.
HRESULT WINAPI h_ResizeBuffers(IDXGISwapChain* swap, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
	if (swap == s_r.swap) Release(s_r.target);
	return o_resizeBuffers(swap, count, width, height, format, flags);
}

// Present and ResizeBuffers are the same code for every swap chain: read
// them from a throwaway one on a hidden window.
bool FindSwapChainFunctions(void** present, void** resizeBuffers)
{
	HWND window = CreateWindowExW(0, L"STATIC", L"sfve-probe", WS_OVERLAPPED, 0, 0, 8, 8, nullptr, nullptr, nullptr, nullptr);
	if (!window) return false;
	DXGI_SWAP_CHAIN_DESC desc{};
	desc.BufferCount = 1;
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.OutputWindow = window;
	desc.SampleDesc.Count = 1;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	IDXGISwapChain* swap = nullptr;
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	HRESULT hr = E_FAIL;
	for (D3D_DRIVER_TYPE driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
		hr = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &swap, &device,
			nullptr, &context);
		if (SUCCEEDED(hr)) break;
	}
	if (SUCCEEDED(hr)) {
		void** vtable = *reinterpret_cast<void***>(swap);
		*present = vtable[kPresentSlot];
		*resizeBuffers = vtable[kResizeBuffersSlot];
	}
	Release(swap);
	Release(context);
	Release(device);
	DestroyWindow(window);
	return SUCCEEDED(hr);
}

} // namespace

bool InstallOverlay()
{
	void* present = nullptr;
	void* resizeBuffers = nullptr;
	if (!FindSwapChainFunctions(&present, &resizeBuffers)) {
		Log("overlay: no Direct3D 11 device to find the swap chain's functions; no overlay");
		return false;
	}
	MH_STATUS st = MH_CreateHook(present, (void*)&h_Present, (void**)&o_present);
	if (st == MH_OK) st = MH_CreateHook(resizeBuffers, (void*)&h_ResizeBuffers, (void**)&o_resizeBuffers);
	if (st == MH_OK) st = MH_EnableHook(present);
	if (st == MH_OK) st = MH_EnableHook(resizeBuffers);
	Log("overlay: %s", st == MH_OK ? "swap chain hooked" : "could not hook the swap chain");
	return st == MH_OK;
}

void SetOverlayUi(OverlayUi ui)
{
	std::lock_guard lock(s_uiMutex);
	s_ui = std::move(ui);
	s_haveUi = static_cast<bool>(s_ui.draw);
}

bool GameWindowFocused()
{
	return s_focused;
}

void FilterGamePad(unsigned long user, void* xinputState)
{
	if (user >= XUSER_MAX_COUNT || !s_haveUi) return;
	std::function<bool(unsigned long, unsigned short)> takesPad;
	{
		std::lock_guard lock(s_uiMutex);
		takesPad = s_ui.takesPad;
	}
	static WORD heldAtRelease[XUSER_MAX_COUNT]; // buttons down when the interface let go: hidden until released
	XINPUT_GAMEPAD& pad = static_cast<XINPUT_STATE*>(xinputState)->Gamepad;
	if (takesPad && takesPad(user, pad.wButtons)) {
		heldAtRelease[user] = pad.wButtons; // what is down now must not reach the game on letting go
		pad = XINPUT_GAMEPAD{};
	} else {
		heldAtRelease[user] &= pad.wButtons;
		pad.wButtons &= ~heldAtRelease[user];
	}
}

} // namespace sfve::overlay
