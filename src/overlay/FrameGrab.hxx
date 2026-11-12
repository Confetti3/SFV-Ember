#pragma once

// Frame grabs for the photo booth (development builds, -sfve_mode=booth; the
// protocol is docs/design/beta8/booth-protocol.md): the game's back buffer,
// and the scene's depth target when it can be told apart, written to files at
// a commanded Present. Booth mode has no overlay interface, so a grab has
// nothing of Ember on it. No PrintWindow, no screen capture: the copy is made
// by the Direct3D 11 device that presents, through a staging texture let go
// of at once.
//
// This header is the interface; stream A-mod fills FrameGrab.cxx. The parts
// that need no game (the BMP encoding, the depth choice's text) are pure, so
// PhotoBoothTests reaches them.
//
// A grab:
//   - colour: the swap chain's back buffer at the Present the grab is due on,
//     as a 32-bit BMP (BITMAPV4HEADER, top-down, BGRA with the alpha channel
//     kept) from R8G8B8A8, B8G8R8A8 (and their sRGB/typeless forms) and
//     R10G10B10A2 (each channel scaled to 8 bits); another format is refused
//     with a reason.
//   - depth (booth only, unless `depth` is Off): the booth hooks the device
//     context's OMSetRenderTargets and its draw calls (found from a throwaway
//     device, as the overlay finds Present), counts draws per depth target
//     during the frame before the Present, and the chosen target's texture is
//     copied at the Present to `<path>.depth` as raw 32-bit values row by
//     row, with `<path>.json` beside it giving width, height, DXGI format,
//     row pitch in bytes, the grab's present number and the draw counts per
//     depth target (the `booth depth` report). Auto picks the target with the
//     most draws; an index picks the n-th target by first use; Off writes no
//     depth.

#include <cstdint>
#include <string>
#include <vector>

namespace sfve::overlay::grab {

// The back buffer's pixel layouts a grab can encode. Unsupported is anything
// else.
enum class PixelFormat { Unsupported, R8G8B8A8, B8G8R8A8, R10G10B10A2 };

// Which depth target a grab copies.
// All: the chosen target (as Auto) to <path>.depth as usual, and every
// target of the frame to <path>.depth.<index>, for finding which is the
// scene's (development: the booth pilot).
enum class DepthMode { Auto, Index, Off, All };
struct DepthChoice {
	DepthMode mode = DepthMode::Auto;
	int index = 0; // Index's target, from 0 by first use in the frame
};

// "auto", "off", "all", or a number from 0: the booth command's `depth=`
// value. False (and `choice` untouched) for any other text.
bool ParseDepthChoice(const std::string& text, DepthChoice& choice);

// Pure. The layout a DXGI_FORMAT (its number) is encoded from: the 8-bit
// RGBA and BGRA formats with their sRGB and typeless forms, and
// R10G10B10A2 (UNORM, UINT, typeless); Unsupported for any other.
PixelFormat PixelFormatOf(unsigned dxgiFormat);

// One depth-stencil target as the probe counted it in a frame: `index` by
// first use in the frame, its texture's DXGI format (by name: D32_FLOAT,
// R24G8_TYPELESS...), size, sample count and the draws made while it was
// bound.
struct DepthCandidate {
	int index = 0;
	std::string format;
	int width = 0;
	int height = 0;
	int samples = 1;
	std::uint32_t draws = 0;
	// The texture's address, the same from frame to frame while the game
	// keeps the texture: what pairs one grab's targets with another's when
	// the order of first use differs.
	std::uint64_t id = 0;
	// The frame's draws are numbered from 1 in order: the first and the last
	// one made into this target (0: none).
	std::uint32_t firstDraw = 0;
	std::uint32_t lastDraw = 0;
	// ClearDepthStencilView calls on it in the frame, and the depth the last
	// one cleared to (-1: none cleared the depth); 0 for a reversed-Z far.
	std::uint32_t clears = 0;
	float clearDepth = -1.0f;
};

// Pure. The candidate a grab's depth choice takes, by its index in
// `candidates`; -1 for none (Off, an index past the list, or no candidate
// with a draw). Auto: the scene's depth, which the frame draws into first:
// of the targets of the back buffer's size (`backWidth` x `backHeight`),
// single-sampled and drawn into, the one with the earliest first draw; when
// none is that size, the earliest drawn of all. Measured on the laptop
// (2026-11-12, docs/design/beta8/booth.md): SFV's frame draws the scene's
// depth first (cleared to 0, reversed-Z), then a fighters-only target (UE4's
// custom depth), the shadow map (R16, gone when no fighter casts one) and
// the HUD's stencil targets last; draw counts change with what is on screen
// (the HUD's can pass the scene's once the fighters are away), the order
// does not.
int ChooseDepthTarget(const std::vector<DepthCandidate>& candidates, const DepthChoice& choice, int backWidth, int backHeight);

// Pure. `{"targets":[...]}` for DepthReport and the depth sidecar.
std::string DepthTargetsJson(const std::vector<DepthCandidate>& candidates);

// Pure. A depth texture's texels (`pixels`, rows `rowPitch` bytes apart, of
// the typeless or depth DXGI format `dxgiFormat`) as one 32-bit word per
// texel, rows packed: R32 / D32_FLOAT as they are (float bits); R24G8 /
// D24_UNORM_S8_UINT as they are (depth in the low 24 bits, stencil in the
// top 8); R32G8X24 / D32_FLOAT_S8X24_UINT the float depth (the texel's first
// 4 bytes); R16 / D16_UNORM zero-extended. `kind` names which ("float32",
// "unorm24_stencil8", "unorm16"). False (nothing written) for any other
// format, a size that is not positive or a pitch shorter than a row.
bool DepthWords(const std::uint8_t* pixels, int width, int height, int rowPitch, unsigned dxgiFormat,
	std::vector<std::uint32_t>& out, std::string& kind);

// Pure. A 32-bit top-down BMP file (BITMAPV4HEADER, alpha kept) of a
// `width` by `height` picture of `format` pixels whose rows are `rowPitch`
// bytes apart. Empty for a size that is not positive, a pitch shorter than a
// row, a null `pixels` or an unsupported format.
std::vector<std::uint8_t> EncodeBmp(const std::uint8_t* pixels, int width, int height, int rowPitch, PixelFormat format);

// What a grab was asked for.
struct Request {
	std::string path;        // the BMP's path (UTF-8); the depth files are named from it
	int afterPresents = 0;   // wait this many presents before copying (0: the next)
	DepthChoice depth;
};

// Any thread: queues a grab, taken on the render thread at the Present that
// is `afterPresents` after this call. One grab is pending at a time; false
// with the reason in `error` when one already is, or the request has no path.
bool QueueGrab(const Request& request, std::string& error);

// What the grabs have done so far, for the `booth` status field.
struct Status {
	std::uint64_t presents = 0; // presents seen since the hook was installed
	bool pending = false;       // a grab is queued and not yet taken
	std::string lastPath;       // the last grab's BMP path ("" before the first)
	bool lastOk = false;
	std::string lastError;      // why the last grab failed ("" when it did not)
	std::uint64_t lastPresent = 0; // the present the last grab was taken at
	// The last grab's depth: "off" (not asked), "ok" (written), or why not.
	std::string lastDepth;
	bool depthProbe = false;    // InstallDepthProbe hooked the context
};
Status CurrentStatus();

// The depth targets counted in the frame before the last Present, as JSON:
// `{"targets":[{"index":0,"format":"D32_FLOAT","width":1920,"height":1080,"draws":123},...]}`
// (`booth depth`). "{\"targets\":[]}" before any.
std::string DepthReport();

// Render thread, from the overlay's present tap (SetPresentTap): counts the
// present and takes the queued grab when it is due. `swapChain` is the
// IDXGISwapChain the game presents on, passed as the opaque pointer the tap
// gives, so this header needs no DXGI.
void OnPresent(void* swapChain);

// Game thread, once, booth mode only: hooks the context's OMSetRenderTargets
// and draw calls for the depth counts. False (logged) when they could not be
// hooked, and then a grab has colour only.
bool InstallDepthProbe();

} // namespace sfve::overlay::grab
