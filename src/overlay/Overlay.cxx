#include "overlay/Overlay.hxx"

#include "mod/Log.hxx"

#include <MinHook.h>

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <xinput.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

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

std::atomic<bool> s_open;
// Each slot's last pad state as the game polled it (buttons, then the left
// stick as direction bits), for the interface's navigation: real and scripted
// pads alike, on any slot.
std::atomic<uint32_t> s_pads[XUSER_MAX_COUNT];
constexpr uint32_t kStickUp = 1u << 16, kStickDown = 1u << 17, kStickLeft = 1u << 18, kStickRight = 1u << 19;
constexpr SHORT kStickThreshold = 16000;
std::atomic<DWORD> s_renderThread;
std::atomic<HWND> s_window; // the game's window, once its swap chain presents
std::atomic<WNDPROC> s_gameWndProc;

struct QueuedMessage {
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
};
std::mutex s_inputMutex;
std::vector<QueuedMessage> s_input; // window messages for ImGui, game thread -> render thread

// Render thread only.
struct Renderer {
	IDXGISwapChain* swap = nullptr; // not owned: the game's
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	ID3D11RenderTargetView* target = nullptr;
	float scale = 0.0f;
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

void Toggle(const char* how)
{
	if (!s_haveUi) return; // nothing to show yet
	const bool open = !s_open.load();
	s_open = open;
	Log("overlay: %s (%s)", open ? "open" : "closed", how);
}

bool IsInputMessage(UINT message)
{
	return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST);
}

// Ahead of the game's window procedure. While the panel is open, keyboard
// and mouse go to ImGui only; key releases still reach the game too, so a
// key held when the panel opened is not left down for it.
LRESULT CALLBACK OverlayWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	const bool firstPress = !(lParam & (1 << 30));
	if (message == WM_KEYDOWN && wParam == VK_F1 && firstPress) {
		Toggle("F1");
		return 0;
	}
	if (s_open && IsInputMessage(message)) {
		{
			std::lock_guard lock(s_inputMutex);
			s_input.push_back({message, wParam, lParam});
		}
		if (message != WM_KEYUP && message != WM_SYSKEYUP) return 0;
	}
	return CallWindowProcW(s_gameWndProc, window, message, wParam, lParam);
}

bool IsGameWindow(HWND window)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	return window && pid == GetCurrentProcessId() && IsWindowVisible(window) && GetAncestor(window, GA_ROOT) == window;
}

// Takes the game's swap chain: ImGui's backends on its window and device.
bool Attach(IDXGISwapChain* swap, HWND window)
{
	if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_r.device)))) return false;
	s_r.device->GetImmediateContext(&s_r.context);
	if (!ImGui::GetCurrentContext()) {
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr; // nothing written next to the game
		io.LogFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	}
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(s_r.device, s_r.context);
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
	Log("overlay: drawing on swap chain %p (window %p)", (void*)swap, (void*)window);
	return true;
}

void Detach()
{
	Release(s_r.target);
	if (s_r.ready) {
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
	}
	Release(s_r.context);
	Release(s_r.device);
	s_r = Renderer{};
}

// The pads' merged state as ImGui gamepad keys, once a frame.
void FeedPads()
{
	uint32_t held = 0;
	for (const std::atomic<uint32_t>& pad : s_pads) held |= pad.load();
	struct Map {
		uint32_t bits;
		ImGuiKey key;
	};
	static const Map kMap[] = {
		{XINPUT_GAMEPAD_DPAD_UP | kStickUp, ImGuiKey_GamepadDpadUp},
		{XINPUT_GAMEPAD_DPAD_DOWN | kStickDown, ImGuiKey_GamepadDpadDown},
		{XINPUT_GAMEPAD_DPAD_LEFT | kStickLeft, ImGuiKey_GamepadDpadLeft},
		{XINPUT_GAMEPAD_DPAD_RIGHT | kStickRight, ImGuiKey_GamepadDpadRight},
		{XINPUT_GAMEPAD_A, ImGuiKey_GamepadFaceDown},
		{XINPUT_GAMEPAD_B, ImGuiKey_GamepadFaceRight},
		{XINPUT_GAMEPAD_X, ImGuiKey_GamepadFaceLeft},
		{XINPUT_GAMEPAD_Y, ImGuiKey_GamepadFaceUp},
		{XINPUT_GAMEPAD_LEFT_SHOULDER, ImGuiKey_GamepadL1},
		{XINPUT_GAMEPAD_RIGHT_SHOULDER, ImGuiKey_GamepadR1},
		{XINPUT_GAMEPAD_START, ImGuiKey_GamepadStart},
	};
	ImGuiIO& io = ImGui::GetIO();
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	for (const Map& m : kMap) io.AddKeyEvent(m.key, (held & m.bits) != 0);
}

void Draw(IDXGISwapChain* swap)
{
	s_renderThread = GetCurrentThreadId();
	DXGI_SWAP_CHAIN_DESC desc{};
	if (FAILED(swap->GetDesc(&desc)) || !IsGameWindow(desc.OutputWindow)) return;
	if (s_r.ready && s_r.swap != swap) {
		if (desc.OutputWindow != s_window.load()) return; // another window's swap chain
		Log("overlay: the game's swap chain changed");
		Detach();
	}
	if (!s_r.ready && !Attach(swap, desc.OutputWindow)) return Detach();
	std::vector<QueuedMessage> input;
	{
		std::lock_guard lock(s_inputMutex);
		input.swap(s_input);
	}
	if (!s_open) return; // nothing drawn: no cost while closed
	for (const QueuedMessage& m : input) ImGui_ImplWin32_WndProcHandler(desc.OutputWindow, m.message, m.wParam, m.lParam);

	OverlayUi ui;
	{
		std::lock_guard lock(s_uiMutex);
		ui = s_ui;
	}
	if (!ui.draw) return;
	const float scale = std::clamp((float)desc.BufferDesc.Height / 900.0f, 0.75f, 3.0f);
	if (scale != s_r.scale) {
		s_r.scale = scale;
		if (ui.applyScale) ui.applyScale(scale);
		ImGui_ImplDX11_InvalidateDeviceObjects(); // a rebuilt font atlas is uploaded on the next frame
	}
	if (!s_r.target) {
		ID3D11Texture2D* backBuffer = nullptr;
		if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) return;
		s_r.device->CreateRenderTargetView(backBuffer, nullptr, &s_r.target);
		backBuffer->Release();
		if (!s_r.target) return;
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	FeedPads();
	ImGui::NewFrame();
	const bool keepOpen = ui.draw();
	ImGui::Render();
	if (!keepOpen) s_open = false;

	// ImGui's backend restores the pipeline state it changes, except the
	// render targets and the hull, domain and compute shaders it unbinds (the
	// engine's state cache would otherwise think them still bound).
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
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	s_r.context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depth);
	s_r.context->HSSetShader(hull.shader, hull.instances, hull.count);
	s_r.context->DSSetShader(domain.shader, domain.instances, domain.count);
	s_r.context->CSSetShader(compute.shader, compute.instances, compute.count);
	for (ID3D11RenderTargetView*& t : targets) Release(t);
	Release(depth);
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

void FilterGamePad(unsigned long user, void* xinputState)
{
	// ImGui's own pad reading (on the render thread) sees the real state.
	if (GetCurrentThreadId() == s_renderThread.load() || user >= XUSER_MAX_COUNT) return;
	static WORD previous[XUSER_MAX_COUNT];
	static WORD heldAtClose[XUSER_MAX_COUNT]; // buttons down when the overlay closed: hidden until released
	XINPUT_GAMEPAD& pad = static_cast<XINPUT_STATE*>(xinputState)->Gamepad;
	s_pads[user] = pad.wButtons | (pad.sThumbLY > kStickThreshold ? kStickUp : 0) |
		(pad.sThumbLY < -kStickThreshold ? kStickDown : 0) | (pad.sThumbLX < -kStickThreshold ? kStickLeft : 0) |
		(pad.sThumbLX > kStickThreshold ? kStickRight : 0);
	const bool backPressed = (pad.wButtons & XINPUT_GAMEPAD_BACK) && !(previous[user] & XINPUT_GAMEPAD_BACK);
	previous[user] = pad.wButtons;
	if (backPressed && s_window.load()) Toggle("View");
	const bool open = s_open;
	if (open || backPressed) {
		heldAtClose[user] = pad.wButtons; // what is down now must not reach the game on closing
		pad = XINPUT_GAMEPAD{};
	} else {
		heldAtClose[user] &= pad.wButtons;
		pad.wButtons &= ~heldAtClose[user];
	}
}

} // namespace sfve::overlay
