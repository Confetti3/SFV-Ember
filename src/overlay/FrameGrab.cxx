#include "overlay/FrameGrab.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameHooks.hxx"
#include "mod/Log.hxx"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

namespace sfve::overlay::grab {

namespace {

using Microsoft::WRL::ComPtr;
using mod::Log;

// A picture's side a grab takes: past any swap chain or depth target SFV
// makes, and small enough that a row's and a file's byte counts fit 32 bits.
constexpr int kMaxSide = 16384;

// The BMP's two headers: BITMAPFILEHEADER (14 bytes) and BITMAPV4HEADER (108),
// written byte by byte (little-endian) so the encoding needs no Windows types.
constexpr std::uint32_t kFileHeaderSize = 14;
constexpr std::uint32_t kInfoHeaderSize = 108;
constexpr std::uint32_t kBiBitfields = 3;
constexpr std::uint32_t kLcsSrgb = 0x73524742; // 'sRGB'
constexpr std::int32_t kPixelsPerMetre = 2835; // 72 dpi

void Put16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
	out.push_back((std::uint8_t)(v & 0xFF));
	out.push_back((std::uint8_t)(v >> 8));
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
	for (int shift = 0; shift < 32; shift += 8) out.push_back((std::uint8_t)((v >> shift) & 0xFF));
}

std::uint32_t Read32(const std::uint8_t* at)
{
	return (std::uint32_t)at[0] | ((std::uint32_t)at[1] << 8) | ((std::uint32_t)at[2] << 16) | ((std::uint32_t)at[3] << 24);
}

// A 10-bit channel to 8 bits, rounded.
std::uint8_t Ten(std::uint32_t v)
{
	return (std::uint8_t)((v * 255u + 511u) / 1023u);
}

const char* FormatName(unsigned format)
{
	switch (format) {
	case DXGI_FORMAT_D32_FLOAT: return "D32_FLOAT";
	case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
	case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
	case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
	case DXGI_FORMAT_R24G8_TYPELESS: return "R24G8_TYPELESS";
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32_FLOAT_S8X24_UINT";
	case DXGI_FORMAT_R32G8X24_TYPELESS: return "R32G8X24_TYPELESS";
	case DXGI_FORMAT_D16_UNORM: return "D16_UNORM";
	case DXGI_FORMAT_R16_TYPELESS: return "R16_TYPELESS";
	case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
	case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
	case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
	default: return nullptr;
	}
}

std::string FormatText(unsigned format)
{
	const char* name = FormatName(format);
	return name ? std::string(name) : "DXGI_" + std::to_string(format);
}

// A depth format's typeless family, which a staging copy is made in (a
// staging texture cannot be a depth-stencil one, and CopyResource takes
// formats of one family); DXGI_FORMAT_UNKNOWN for a format DepthWords cannot
// read.
DXGI_FORMAT DepthFamily(unsigned format)
{
	switch (format) {
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return DXGI_FORMAT_R32G8X24_TYPELESS;
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
	default: return DXGI_FORMAT_UNKNOWN;
	}
}

// Bytes per texel of a DepthFamily format.
int DepthTexelBytes(DXGI_FORMAT family)
{
	switch (family) {
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R24G8_TYPELESS: return 4;
	case DXGI_FORMAT_R32G8X24_TYPELESS: return 8;
	case DXGI_FORMAT_R16_TYPELESS: return 2;
	default: return 0;
	}
}

std::wstring Wide(const std::string& utf8)
{
	const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
	std::wstring out((size_t)(length > 0 ? length : 0), L'\0');
	if (length > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), out.data(), length);
	return out;
}

// Writes `bytes` to the UTF-8 `path`, its folder made first. Empty, or why not.
std::string WriteFileBytes(const std::string& path, const void* bytes, size_t size)
{
	const std::filesystem::path target(Wide(path));
	if (target.empty()) return "the path is not UTF-8";
	std::error_code ignored;
	if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ignored);
	FILE* f = nullptr;
	if (_wfopen_s(&f, target.c_str(), L"wb") != 0 || !f) return "cannot open " + path + " (" + std::to_string(GetLastError()) + ")";
	const size_t written = size ? fwrite(bytes, 1, size, f) : 0;
	const bool closed = fclose(f) == 0;
	if (written != size || !closed) return "could not write all of " + path;
	return {};
}

// ---- The grab queue and its status (any thread, under s_mutex) ----

std::mutex s_mutex;
std::optional<Request> s_request; // queued, waiting for its present
std::uint64_t s_dueAt = 0;        // the present number it is taken at
bool s_busy = false;              // taken, its files still being written
Status s_status;
std::atomic<std::uint64_t> s_presents;

// ---- The depth probe (render thread's context calls, under s_depthMutex) ----

// ID3D11DeviceContext's vtable slots, in d3d11.h's declaration order:
// IUnknown (0-2), ID3D11DeviceChild (3-6), then VSSetConstantBuffers 7,
// PSSetShaderResources 8, PSSetShader 9, PSSetSamplers 10, VSSetShader 11,
// DrawIndexed 12, Draw 13, Map 14, Unmap 15, PSSetConstantBuffers 16,
// IASetInputLayout 17, IASetVertexBuffers 18, IASetIndexBuffer 19,
// DrawIndexedInstanced 20, DrawInstanced 21, ... OMSetRenderTargets 33,
// OMSetRenderTargetsAndUnorderedAccessViews 34, OMSetBlendState 35,
// OMSetDepthStencilState 36, SOSetTargets 37, DrawAuto 38,
// DrawIndexedInstancedIndirect 39, DrawInstancedIndirect 40.
constexpr int kDrawIndexedSlot = 12;
constexpr int kDrawSlot = 13;
constexpr int kDrawIndexedInstancedSlot = 20;
constexpr int kDrawInstancedSlot = 21;
constexpr int kOMSetRenderTargetsSlot = 33;
constexpr int kOMSetRenderTargetsAndUavsSlot = 34;
constexpr int kDrawAutoSlot = 38;
constexpr int kDrawIndexedInstancedIndirectSlot = 39;
constexpr int kDrawInstancedIndirectSlot = 40;
// ... Dispatch 41, DispatchIndirect 42, RSSetState 43, RSSetViewports 44,
// RSSetScissorRects 45, CopySubresourceRegion 46, CopyResource 47,
// UpdateSubresource 48, CopyStructureCount 49, ClearRenderTargetView 50,
// ClearUnorderedAccessViewUint 51, ClearUnorderedAccessViewFloat 52,
// ClearDepthStencilView 53.
constexpr int kClearDepthStencilViewSlot = 53;

// Depth targets counted in one frame: past this many, the rest are not.
constexpr size_t kMaxTargets = 64;

using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
	ID3D11DepthStencilView*);
using OMSetRenderTargetsAndUavsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
	ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
using DrawAutoFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using DrawIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);

OMSetRenderTargetsFn o_omSetRenderTargets;
OMSetRenderTargetsAndUavsFn o_omSetRenderTargetsAndUavs;
DrawIndexedFn o_drawIndexed;
DrawFn o_draw;
DrawIndexedInstancedFn o_drawIndexedInstanced;
DrawInstancedFn o_drawInstanced;
DrawAutoFn o_drawAuto;
DrawIndirectFn o_drawIndexedInstancedIndirect;
DrawIndirectFn o_drawInstancedIndirect;

std::atomic<bool> s_probeInstalled;
// The game's immediate context (the swap chain's device's): only its calls
// are counted, not a deferred context's. Not owned.
std::atomic<ID3D11DeviceContext*> s_immediate;
IDXGISwapChain* s_contextOf = nullptr; // render thread: the swap chain s_immediate was read from

struct Tracked {
	ComPtr<ID3D11Texture2D> texture; // held until the frame after next, so the copy at Present finds it
	unsigned format = 0;
	DepthCandidate info;
};

std::mutex s_depthMutex;
std::vector<Tracked> s_frame; // this frame's targets, by first use
int s_bound = -1;             // the target bound now (an index in s_frame), or none
std::vector<Tracked> s_last;  // the frame before the last present's

std::vector<DepthCandidate> Infos(const std::vector<Tracked>& targets)
{
	std::vector<DepthCandidate> out;
	for (const Tracked& t : targets) out.push_back(t.info);
	return out;
}

std::uint32_t s_drawNumber = 0; // the frame's draws so far (under s_depthMutex)

// Under s_depthMutex: the frame's target for `view`'s texture, added at its
// first use; -1 for none (no view, not a 2D texture, or the list is full).
int TargetOf(ID3D11DepthStencilView* view)
{
	if (!view) return -1;
	ComPtr<ID3D11Resource> resource;
	view->GetResource(&resource);
	if (!resource) return -1;
	for (size_t i = 0; i < s_frame.size(); i++) {
		if (s_frame[i].texture.Get() == resource.Get()) return (int)i;
	}
	// QueryInterface rather than a cast: a view of a 1D or 3D texture (none
	// is expected) is not counted.
	Tracked t;
	if (s_frame.size() >= kMaxTargets || FAILED(resource.As(&t.texture))) return -1;
	D3D11_TEXTURE2D_DESC desc{};
	t.texture->GetDesc(&desc);
	t.format = desc.Format;
	t.info.index = (int)s_frame.size();
	t.info.format = FormatText(desc.Format);
	t.info.width = (int)desc.Width;
	t.info.height = (int)desc.Height;
	t.info.samples = (int)desc.SampleDesc.Count;
	t.info.id = (std::uint64_t)(uintptr_t)t.texture.Get();
	s_frame.push_back(std::move(t));
	return (int)s_frame.size() - 1;
}

void NoteDepthTarget(ID3D11DepthStencilView* view)
{
	std::lock_guard lock(s_depthMutex);
	s_bound = TargetOf(view);
}

void CountDraw(ID3D11DeviceContext* context)
{
	if (context != s_immediate.load()) return;
	std::lock_guard lock(s_depthMutex);
	++s_drawNumber;
	if (s_bound < 0) return;
	DepthCandidate& info = s_frame[(size_t)s_bound].info;
	if (!info.draws++) info.firstDraw = s_drawNumber;
	info.lastDraw = s_drawNumber;
}

using ClearDepthStencilViewFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
ClearDepthStencilViewFn o_clearDepthStencilView;

void STDMETHODCALLTYPE h_ClearDepthStencilView(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags,
	FLOAT depth, UINT8 stencil)
{
	if (context == s_immediate.load()) {
		std::lock_guard lock(s_depthMutex);
		const int target = TargetOf(view);
		if (target >= 0) {
			DepthCandidate& info = s_frame[(size_t)target].info;
			info.clears++;
			if (flags & D3D11_CLEAR_DEPTH) info.clearDepth = depth;
		}
	}
	o_clearDepthStencilView(context, view, flags, depth, stencil);
}

void STDMETHODCALLTYPE h_OMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* views,
	ID3D11DepthStencilView* depth)
{
	if (context == s_immediate.load()) NoteDepthTarget(depth);
	o_omSetRenderTargets(context, count, views, depth);
}

void STDMETHODCALLTYPE h_OMSetRenderTargetsAndUavs(ID3D11DeviceContext* context, UINT count,
	ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth, UINT uavStart, UINT uavCount,
	ID3D11UnorderedAccessView* const* uavs, const UINT* initialCounts)
{
	// D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL leaves the targets as they are.
	if (context == s_immediate.load() && count != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) NoteDepthTarget(depth);
	o_omSetRenderTargetsAndUavs(context, count, views, depth, uavStart, uavCount, uavs, initialCounts);
}

void STDMETHODCALLTYPE h_DrawIndexed(ID3D11DeviceContext* context, UINT a, UINT b, INT c)
{
	CountDraw(context);
	o_drawIndexed(context, a, b, c);
}

void STDMETHODCALLTYPE h_Draw(ID3D11DeviceContext* context, UINT a, UINT b)
{
	CountDraw(context);
	o_draw(context, a, b);
}

void STDMETHODCALLTYPE h_DrawIndexedInstanced(ID3D11DeviceContext* context, UINT a, UINT b, UINT c, INT d, UINT e)
{
	CountDraw(context);
	o_drawIndexedInstanced(context, a, b, c, d, e);
}

void STDMETHODCALLTYPE h_DrawInstanced(ID3D11DeviceContext* context, UINT a, UINT b, UINT c, UINT d)
{
	CountDraw(context);
	o_drawInstanced(context, a, b, c, d);
}

void STDMETHODCALLTYPE h_DrawAuto(ID3D11DeviceContext* context)
{
	CountDraw(context);
	o_drawAuto(context);
}

void STDMETHODCALLTYPE h_DrawIndexedInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* args, UINT offset)
{
	CountDraw(context);
	o_drawIndexedInstancedIndirect(context, args, offset);
}

void STDMETHODCALLTYPE h_DrawInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* args, UINT offset)
{
	CountDraw(context);
	o_drawInstancedIndirect(context, args, offset);
}

// Render thread, at each present: the frame that ends here becomes the
// report's, and a new one starts with the target still bound (the game need
// not bind it again).
std::vector<Tracked> FinishDepthFrame()
{
	std::lock_guard lock(s_depthMutex);
	s_last = std::move(s_frame);
	s_frame.clear();
	s_drawNumber = 0;
	if (s_bound >= 0) {
		Tracked kept = s_last[(size_t)s_bound];
		const std::uint64_t id = kept.info.id;
		kept.info = DepthCandidate{0, kept.info.format, kept.info.width, kept.info.height, kept.info.samples};
		kept.info.id = id;
		s_frame.push_back(std::move(kept));
		s_bound = 0;
	}
	return s_last;
}

// The swap chain's device's immediate context, read again when the game
// presents on another swap chain.
void NoteContext(IDXGISwapChain* swap)
{
	if (swap == s_contextOf) return;
	s_contextOf = swap;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	if (SUCCEEDED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(device.GetAddressOf())))) {
		device->GetImmediateContext(&context);
	}
	s_immediate.store(context.Get()); // lives as long as the game's device
}

bool IsGameWindow(HWND window)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	return window && pid == GetCurrentProcessId() && IsWindowVisible(window) && GetAncestor(window, GA_ROOT) == window;
}

// A texture's first subresource read back through a staging copy in `format`
// (its own, or the typeless family a depth texture is copied in), rows
// packed at `texelBytes` per texel. The read waits for the GPU's copy, about
// a frame's stall, once per grab. Empty with `error` set when it cannot be
// read.
struct Readback {
	std::vector<std::uint8_t> bytes;
	int width = 0;
	int height = 0;
	int rowPitch = 0;
};
Readback ReadTexture(ID3D11Texture2D* texture, DXGI_FORMAT format, int texelBytes, std::string& error)
{
	Readback out;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	texture->GetDevice(&device);
	device->GetImmediateContext(&context);
	D3D11_TEXTURE2D_DESC desc{};
	texture->GetDesc(&desc);
	if (desc.SampleDesc.Count != 1) return error = "the texture is multisampled", out;
	if ((int)desc.Width <= 0 || (int)desc.Width > kMaxSide || (int)desc.Height <= 0 || (int)desc.Height > kMaxSide) {
		return error = "the texture is " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height), out;
	}
	desc.Format = format;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;
	ComPtr<ID3D11Texture2D> staging;
	HRESULT result = device->CreateTexture2D(&desc, nullptr, &staging);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(result)) {
		context->CopyResource(staging.Get(), texture);
		result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
	}
	if (FAILED(result)) {
		char text[64];
		snprintf(text, sizeof(text), "the copy could not be read (0x%08lX)", (unsigned long)result);
		return error = text, out;
	}
	out.width = (int)desc.Width;
	out.height = (int)desc.Height;
	out.rowPitch = out.width * texelBytes;
	out.bytes.resize((size_t)out.rowPitch * (size_t)out.height);
	if (mapped.RowPitch < (UINT)out.rowPitch) {
		context->Unmap(staging.Get(), 0);
		out = Readback{};
		return error = "the copy's rows are shorter than its texels", out;
	}
	for (int y = 0; y < out.height; y++) {
		memcpy(out.bytes.data() + (size_t)out.rowPitch * (size_t)y, static_cast<const std::uint8_t*>(mapped.pData) +
			(size_t)mapped.RowPitch * (size_t)y, (size_t)out.rowPitch);
	}
	context->Unmap(staging.Get(), 0);
	return out;
}

// What one grab took on the render thread, written out on a thread of its own.
struct Taken {
	Request request;
	std::uint64_t present = 0;
	std::string error;      // the colour could not be taken
	Readback colour;
	PixelFormat format = PixelFormat::Unsupported;
	std::string depthNote;  // "off", or why no depth, or "" with depth taken
	Readback depth;
	unsigned depthFormat = 0;
	int depthIndex = -1;
	std::vector<DepthCandidate> targets;
	// depth=all: every target that could be read, by its index.
	struct Extra {
		int index = -1;
		unsigned format = 0;
		Readback texels;
	};
	std::vector<Extra> all;
};

// Writes one depth readback as <path><suffix>: one uint32 per texel; the
// values' kind, or empty when the format cannot be read.
std::string WriteDepthWords(const std::string& path, const Readback& texels, unsigned format, std::string& error)
{
	std::vector<std::uint32_t> words;
	std::string kind;
	if (!DepthWords(texels.bytes.data(), texels.width, texels.height, texels.rowPitch, DepthFamily(format), words, kind)) {
		error = "the depth target's format " + FormatText(format) + " cannot be read";
		return {};
	}
	error = WriteFileBytes(path, words.data(), words.size() * sizeof(std::uint32_t));
	return error.empty() ? kind : std::string();
}

void Finish(const Taken& taken, const std::string& error, const std::string& depth)
{
	std::lock_guard lock(s_mutex);
	s_busy = false;
	s_status.pending = false;
	s_status.lastPath = taken.request.path;
	s_status.lastOk = error.empty();
	s_status.lastError = error;
	s_status.lastPresent = taken.present;
	s_status.lastDepth = depth;
}

// The files of a grab: the BMP, then the depth's raw words and its sidecar.
void WriteGrab(const Taken& taken)
{
	std::string error = taken.error;
	if (error.empty()) {
		const std::vector<std::uint8_t> bmp =
			EncodeBmp(taken.colour.bytes.data(), taken.colour.width, taken.colour.height, taken.colour.rowPitch, taken.format);
		error = bmp.empty() ? "the back buffer could not be encoded" : WriteFileBytes(taken.request.path, bmp.data(), bmp.size());
	}
	std::string depth = taken.depthNote;
	if (error.empty() && depth.empty()) {
		const std::string kind = WriteDepthWords(taken.request.path + ".depth", taken.depth, taken.depthFormat, depth);
		// depth=all: every other target beside it, <path>.depth.<index>.
		std::string all = "[";
		for (const Taken::Extra& extra : taken.all) {
			std::string why;
			const std::string extraKind =
				WriteDepthWords(taken.request.path + ".depth." + std::to_string(extra.index), extra.texels, extra.format, why);
			all += (all.size() > 1 ? ",{" : "{") + std::string("\"index\":") + std::to_string(extra.index) +
				",\"width\":" + std::to_string(extra.texels.width) + ",\"height\":" + std::to_string(extra.texels.height) +
				",\"format\":" + mod::JsonString(FormatText(extra.format)) + ",\"values\":" + mod::JsonString(extraKind) +
				",\"error\":" + mod::JsonString(why) + "}";
		}
		all += "]";
		if (depth.empty()) {
			const std::string json = "{\"width\":" + std::to_string(taken.depth.width) + ",\"height\":" +
				std::to_string(taken.depth.height) + ",\"format\":" + mod::JsonString(FormatText(taken.depthFormat)) +
				",\"values\":" + mod::JsonString(kind) + ",\"rowPitch\":" + std::to_string(taken.depth.width * 4) +
				",\"present\":" + std::to_string(taken.present) + ",\"index\":" + std::to_string(taken.depthIndex) +
				",\"colour\":{\"width\":" + std::to_string(taken.colour.width) + ",\"height\":" +
				std::to_string(taken.colour.height) + "},\"depthTargets\":" + DepthTargetsJson(taken.targets) +
				",\"all\":" + all + "}";
			depth = WriteFileBytes(taken.request.path + ".json", json.data(), json.size());
		}
		if (depth.empty()) depth = "ok";
	}
	if (!error.empty()) Log("grab: %s: %s", taken.request.path.c_str(), error.c_str());
	else Log("grab: %s at present %llu, %dx%d; depth %s", taken.request.path.c_str(), (unsigned long long)taken.present,
		taken.colour.width, taken.colour.height, depth.c_str());
	Finish(taken, error, depth);
}

// Render thread: the queued grab, due now.
void Take(IDXGISwapChain* swap, Request request, std::uint64_t present, const std::vector<Tracked>& frame)
{
	auto taken = std::make_shared<Taken>();
	taken->request = std::move(request);
	taken->present = present;
	ComPtr<ID3D11Texture2D> back;
	if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.GetAddressOf()))) || !back) {
		taken->error = "the back buffer is not a Direct3D 11 texture";
	} else {
		D3D11_TEXTURE2D_DESC desc{};
		back->GetDesc(&desc);
		taken->format = PixelFormatOf(desc.Format);
		if (taken->format == PixelFormat::Unsupported) {
			taken->error = "the back buffer's format " + FormatText(desc.Format) + " is not one a grab encodes";
		} else {
			taken->colour = ReadTexture(back.Get(), desc.Format, 4, taken->error);
		}
		if (taken->error.empty()) {
			taken->targets = Infos(frame);
			const DepthChoice& choice = taken->request.depth;
			taken->depthIndex = ChooseDepthTarget(taken->targets, choice, (int)desc.Width, (int)desc.Height);
			if (choice.mode == DepthMode::Off) taken->depthNote = "off";
			else if (!s_probeInstalled.load()) taken->depthNote = "the depth probe is not installed";
			else if (taken->depthIndex < 0) taken->depthNote = "no depth target was drawn to in the frame";
			else {
				const Tracked& target = frame[(size_t)taken->depthIndex];
				const DXGI_FORMAT family = DepthFamily(target.format);
				if (family == DXGI_FORMAT_UNKNOWN) {
					taken->depthNote = "the depth target's format " + target.info.format + " cannot be read";
				} else {
					std::string why;
					taken->depth = ReadTexture(target.texture.Get(), family, DepthTexelBytes(family), why);
					taken->depthFormat = target.format;
					if (!why.empty()) taken->depthNote = "the depth target: " + why;
				}
			}
			if (choice.mode == DepthMode::All && taken->depthNote.empty()) {
				for (size_t i = 0; i < frame.size(); i++) {
					const DXGI_FORMAT family = DepthFamily(frame[i].format);
					if (family == DXGI_FORMAT_UNKNOWN) continue;
					std::string why;
					Taken::Extra extra;
					extra.index = (int)i;
					extra.format = frame[i].format;
					extra.texels = ReadTexture(frame[i].texture.Get(), family, DepthTexelBytes(family), why);
					if (why.empty()) taken->all.push_back(std::move(extra));
				}
			}
		}
	}
	// The files are written away from the render thread.
	std::thread([taken] { WriteGrab(*taken); }).detach();
}

} // namespace

bool ParseDepthChoice(const std::string& text, DepthChoice& choice)
{
	if (text == "auto") return choice = DepthChoice{DepthMode::Auto, 0}, true;
	if (text == "off") return choice = DepthChoice{DepthMode::Off, 0}, true;
	if (text == "all") return choice = DepthChoice{DepthMode::All, 0}, true;
	int index = 0;
	const char* end = text.data() + text.size();
	if (text.empty() || text.size() > 4 || text[0] == '-' || text[0] == '+') return false;
	const auto [at, ec] = std::from_chars(text.data(), end, index);
	if (ec != std::errc() || at != end || index < 0) return false;
	choice = DepthChoice{DepthMode::Index, index};
	return true;
}

PixelFormat PixelFormatOf(unsigned dxgiFormat)
{
	switch (dxgiFormat) {
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return PixelFormat::R8G8B8A8;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return PixelFormat::B8G8R8A8;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT: return PixelFormat::R10G10B10A2;
	default: return PixelFormat::Unsupported;
	}
}

std::vector<std::uint8_t> EncodeBmp(const std::uint8_t* pixels, int width, int height, int rowPitch, PixelFormat format)
{
	if (!pixels || width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide || format == PixelFormat::Unsupported) {
		return {};
	}
	if (rowPitch < width * 4) return {};
	const std::uint32_t imageSize = (std::uint32_t)width * (std::uint32_t)height * 4u;
	std::vector<std::uint8_t> out;
	out.reserve(kFileHeaderSize + kInfoHeaderSize + imageSize);
	// BITMAPFILEHEADER
	out.push_back('B');
	out.push_back('M');
	Put32(out, kFileHeaderSize + kInfoHeaderSize + imageSize);
	Put16(out, 0);
	Put16(out, 0);
	Put32(out, kFileHeaderSize + kInfoHeaderSize);
	// BITMAPV4HEADER: top-down (negative height), 32 bits, BI_BITFIELDS with
	// the masks of BGRA bytes, alpha kept, sRGB.
	Put32(out, kInfoHeaderSize);
	Put32(out, (std::uint32_t)width);
	Put32(out, (std::uint32_t)-height);
	Put16(out, 1);
	Put16(out, 32);
	Put32(out, kBiBitfields);
	Put32(out, imageSize);
	Put32(out, (std::uint32_t)kPixelsPerMetre);
	Put32(out, (std::uint32_t)kPixelsPerMetre);
	Put32(out, 0); // colours used
	Put32(out, 0); // colours important
	Put32(out, 0x00FF0000u); // red
	Put32(out, 0x0000FF00u); // green
	Put32(out, 0x000000FFu); // blue
	Put32(out, 0xFF000000u); // alpha
	Put32(out, kLcsSrgb);
	for (int i = 0; i < 9; i++) Put32(out, 0); // CIEXYZTRIPLE endpoints (unused with sRGB)
	for (int i = 0; i < 3; i++) Put32(out, 0); // gamma red, green, blue
	for (int y = 0; y < height; y++) {
		const std::uint8_t* row = pixels + (size_t)rowPitch * (size_t)y;
		for (int x = 0; x < width; x++) {
			const std::uint8_t* p = row + 4 * x;
			std::uint8_t b = 0, g = 0, r = 0, a = 0;
			switch (format) {
			case PixelFormat::R8G8B8A8: r = p[0], g = p[1], b = p[2], a = p[3]; break;
			case PixelFormat::B8G8R8A8: b = p[0], g = p[1], r = p[2], a = p[3]; break;
			case PixelFormat::R10G10B10A2: {
				const std::uint32_t v = Read32(p);
				r = Ten(v & 0x3FF);
				g = Ten((v >> 10) & 0x3FF);
				b = Ten((v >> 20) & 0x3FF);
				a = (std::uint8_t)((v >> 30) * 85u);
				break;
			}
			case PixelFormat::Unsupported: break;
			}
			out.push_back(b);
			out.push_back(g);
			out.push_back(r);
			out.push_back(a);
		}
	}
	return out;
}

int ChooseDepthTarget(const std::vector<DepthCandidate>& candidates, const DepthChoice& choice, int backWidth, int backHeight)
{
	switch (choice.mode) {
	case DepthMode::Off: return -1;
	case DepthMode::Index: return choice.index >= 0 && (size_t)choice.index < candidates.size() ? choice.index : -1;
	case DepthMode::Auto:
	case DepthMode::All: break;
	}
	const auto best = [&](bool sized) {
		int found = -1;
		for (size_t i = 0; i < candidates.size(); i++) {
			const DepthCandidate& c = candidates[i];
			if (!c.draws) continue;
			if (sized && (c.width != backWidth || c.height != backHeight || c.samples != 1)) continue;
			if (found < 0 || c.draws > candidates[(size_t)found].draws) found = (int)i;
		}
		return found;
	};
	const int sized = best(true);
	return sized >= 0 ? sized : best(false);
}

std::string DepthTargetsJson(const std::vector<DepthCandidate>& candidates)
{
	std::string json = "{\"targets\":[";
	for (size_t i = 0; i < candidates.size(); i++) {
		const DepthCandidate& c = candidates[i];
		json += (i ? ",{" : "{") + std::string("\"index\":") + std::to_string(c.index) + ",\"format\":" + mod::JsonString(c.format) +
			",\"width\":" + std::to_string(c.width) + ",\"height\":" + std::to_string(c.height) +
			",\"samples\":" + std::to_string(c.samples) + ",\"draws\":" + std::to_string(c.draws) +
			",\"id\":" + std::to_string(c.id) + ",\"firstDraw\":" + std::to_string(c.firstDraw) +
			",\"lastDraw\":" + std::to_string(c.lastDraw) + ",\"clears\":" + std::to_string(c.clears) +
			",\"clearDepth\":" + std::to_string(c.clearDepth) + "}";
	}
	return json + "]}";
}

bool DepthWords(const std::uint8_t* pixels, int width, int height, int rowPitch, unsigned dxgiFormat,
	std::vector<std::uint32_t>& out, std::string& kind)
{
	const DXGI_FORMAT family = DepthFamily(dxgiFormat);
	const int texel = DepthTexelBytes(family);
	if (!pixels || !texel || width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide || rowPitch < width * texel) {
		return false;
	}
	out.assign((size_t)width * (size_t)height, 0);
	for (int y = 0; y < height; y++) {
		const std::uint8_t* row = pixels + (size_t)rowPitch * (size_t)y;
		std::uint32_t* to = out.data() + (size_t)width * (size_t)y;
		for (int x = 0; x < width; x++) {
			const std::uint8_t* p = row + (size_t)texel * (size_t)x;
			to[x] = texel == 2 ? (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) : Read32(p);
		}
	}
	kind = family == DXGI_FORMAT_R24G8_TYPELESS ? "unorm24_stencil8" : family == DXGI_FORMAT_R16_TYPELESS ? "unorm16" : "float32";
	return true;
}

bool QueueGrab(const Request& request, std::string& error)
{
	if (request.path.empty()) return error = "no path", false;
	if (request.afterPresents < 0) return error = "after= is not a present count", false;
	std::lock_guard lock(s_mutex);
	if (s_request || s_busy) return error = "grab pending", false;
	s_request = request;
	s_dueAt = s_presents.load() + 1 + (std::uint64_t)request.afterPresents;
	s_status.pending = true;
	return true;
}

Status CurrentStatus()
{
	std::lock_guard lock(s_mutex);
	Status status = s_status;
	status.presents = s_presents.load();
	status.depthProbe = s_probeInstalled.load();
	return status;
}

std::string DepthReport()
{
	std::lock_guard lock(s_depthMutex);
	return DepthTargetsJson(Infos(s_last));
}

void OnPresent(void* swapChain)
{
	auto* swap = static_cast<IDXGISwapChain*>(swapChain);
	DXGI_SWAP_CHAIN_DESC desc{};
	if (!swap || FAILED(swap->GetDesc(&desc)) || !IsGameWindow(desc.OutputWindow)) return;
	NoteContext(swap);
	const std::uint64_t present = ++s_presents;
	std::vector<Tracked> frame = FinishDepthFrame();
	std::optional<Request> due;
	{
		std::lock_guard lock(s_mutex);
		if (s_request && present >= s_dueAt) {
			due = std::move(s_request);
			s_request.reset();
			s_busy = true;
		}
	}
	if (due) Take(swap, std::move(*due), present, frame);
}

bool InstallDepthProbe()
{
	if (s_probeInstalled.load()) return true;
	// The context's functions are the same code for every context of the
	// runtime: read them from a throwaway device's, as the overlay reads the
	// swap chain's.
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	HRESULT hr = E_FAIL;
	for (D3D_DRIVER_TYPE driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
		hr = D3D11CreateDevice(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
		if (SUCCEEDED(hr)) break;
	}
	if (FAILED(hr) || !context) {
		Log("grab: no Direct3D 11 device to find the context's functions; grabs have colour only");
		return false;
	}
	void** vtable = *reinterpret_cast<void***>(context.Get());
	struct Slot {
		const char* name;
		int slot;
		void* detour;
		void** original;
	};
	const Slot slots[] = {
		{"ID3D11DeviceContext::OMSetRenderTargets", kOMSetRenderTargetsSlot, (void*)&h_OMSetRenderTargets,
			(void**)&o_omSetRenderTargets},
		{"ID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews", kOMSetRenderTargetsAndUavsSlot,
			(void*)&h_OMSetRenderTargetsAndUavs, (void**)&o_omSetRenderTargetsAndUavs},
		{"ID3D11DeviceContext::DrawIndexed", kDrawIndexedSlot, (void*)&h_DrawIndexed, (void**)&o_drawIndexed},
		{"ID3D11DeviceContext::Draw", kDrawSlot, (void*)&h_Draw, (void**)&o_draw},
		{"ID3D11DeviceContext::DrawIndexedInstanced", kDrawIndexedInstancedSlot, (void*)&h_DrawIndexedInstanced,
			(void**)&o_drawIndexedInstanced},
		{"ID3D11DeviceContext::DrawInstanced", kDrawInstancedSlot, (void*)&h_DrawInstanced, (void**)&o_drawInstanced},
		{"ID3D11DeviceContext::DrawAuto", kDrawAutoSlot, (void*)&h_DrawAuto, (void**)&o_drawAuto},
		{"ID3D11DeviceContext::DrawIndexedInstancedIndirect", kDrawIndexedInstancedIndirectSlot,
			(void*)&h_DrawIndexedInstancedIndirect, (void**)&o_drawIndexedInstancedIndirect},
		{"ID3D11DeviceContext::DrawInstancedIndirect", kDrawInstancedIndirectSlot, (void*)&h_DrawInstancedIndirect,
			(void**)&o_drawInstancedIndirect},
		{"ID3D11DeviceContext::ClearDepthStencilView", kClearDepthStencilViewSlot, (void*)&h_ClearDepthStencilView,
			(void**)&o_clearDepthStencilView},
	};
	// Two slots sharing one function could not both be hooked, and the set
	// would fail: the set refuses that instead, with the slots named.
	for (size_t i = 0; i < std::size(slots); i++) {
		for (size_t j = 0; j < i; j++) {
			if (vtable[slots[i].slot] == vtable[slots[j].slot]) {
				Log("grab: %s and %s are one function; no depth probe, grabs have colour only", slots[j].name, slots[i].name);
				return false;
			}
		}
	}
	mod::HookTransaction hooks;
	for (const Slot& s : slots) hooks.Add(s.name, vtable[s.slot], s.detour, s.original);
	if (mod::CommitHookSet(hooks, "depth probe")) {
		Log("grab: could not hook the context's draws; grabs have colour only");
		return false;
	}
	s_probeInstalled.store(true);
	Log("grab: the context's render targets and draws hooked (depth probe)");
	return true;
}

} // namespace sfve::overlay::grab
