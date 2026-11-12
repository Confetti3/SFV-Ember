#include "mod/SocketProbe.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <string>

namespace sfve::mod {

namespace {

// sfv::kSocketWorldPosition: (CharaVisualPart*, socket name, float out[4],
// flag); the flag is passed on untouched.
using SocketFn = void (*)(uint8_t* visualPart, const wchar_t* socket, float* out, uint64_t flag);

SocketFn o_socket;
uintptr_t s_base;
std::atomic<int32_t> s_frame{-1};
std::atomic<bool> s_replaying;
std::atomic<int> s_lines;
// A runaway caller cannot fill the disk: the log stops here.
constexpr int kMostLines = 20000;

// The calls of the socket read that run every tick, by their return
// addresses (call site + 5; read in IDA, build 13173604).
constexpr uint32_t kEveryTick[] = {
	0x37D48E,           // Chara PreInput (rva 0x37D250): bone 28 into CharaUnit+1984 (masked)
	0x31B10E, 0x31B1DE, // the camera's handler rva 0x31AF60
	0x32FA5F,           // the camera's handler rva 0x32F790
	0x33C41F,           // the camera's handler rva 0x33C1C0
	0x3E3EF0, 0x3E3F57, // rva 0x3E3E60, from rva 0x3E39F0
};

std::string SocketName(const wchar_t* name)
{
	std::string out;
	for (int i = 0; name && i < 48; i++) {
		wchar_t c = 0;
		if (!TryRead(name + i, &c) || !c) break;
		out += c >= 0x20 && c < 0x7F ? (char)c : '?';
	}
	return out.empty() ? "?" : out;
}

// The side of the fighter a visual part belongs to, or -1.
int SideOf(const uint8_t* visualPart)
{
	const uint8_t* fighter = nullptr;
	int side = -1;
	if (!visualPart || !TryRead(visualPart + sfv::visual::kOwnerObject, &fighter) || !fighter ||
		!TryRead(fighter + sfv::fighter::kSide, &side)) {
		return -1;
	}
	return side;
}

void h_Socket(uint8_t* visualPart, const wchar_t* socket, float* out, uint64_t flag)
{
	o_socket(visualPart, socket, out, flag);
	const uint32_t from = (uint32_t)(reinterpret_cast<uintptr_t>(_ReturnAddress()) - s_base);
	if (socket_probe::EveryTick(from) || s_lines.fetch_add(1) >= kMostLines) return;
	// The read writes nothing without the actor's mesh (rva 0x3919D0).
	const uint8_t* actor = nullptr;
	float p[3] = {};
	const bool read = visualPart && TryRead(visualPart + sfv::visual::kActor, &actor) && actor && out &&
		TryRead(out + 0, &p[0]) && TryRead(out + 1, &p[1]) && TryRead(out + 2, &p[2]);
	const std::string name = SocketName(socket);
	const int side = SideOf(visualPart);
	if (!read) {
		Log("socket: frame %d%s, P%d, socket %s, from rva 0x%X: no model, nothing written", s_frame.load(),
			s_replaying.load() ? " replayed" : "", side + 1, name.c_str(), from);
		return;
	}
	Log("socket: frame %d%s, P%d, socket %s, from rva 0x%X: %.5f %.5f %.5f", s_frame.load(), s_replaying.load() ? " replayed" : "",
		side + 1, name.c_str(), from, p[0], p[1], p[2]);
}

} // namespace

namespace socket_probe {

bool EveryTick(uint32_t returnRva)
{
	return std::find(std::begin(kEveryTick), std::end(kEveryTick), returnRva) != std::end(kEveryTick);
}

} // namespace socket_probe

void NoteSocketProbeUpdate(int32_t inputFrame, bool replaying)
{
	s_frame.store(inputFrame);
	s_replaying.store(replaying);
}

bool InstallSocketProbe(uintptr_t imageBase)
{
	s_base = imageBase;
	const bool ok = HookCodeSite(imageBase, sfv::kSocketWorldPosition, reinterpret_cast<void*>(&h_Socket),
		reinterpret_cast<void**>(&o_socket));
	Log("socket: %s", ok ? "the fight's reads of the fighters' models are logged (-sfve_socket_log)"
						 : "the socket read NOT hooked; nothing is logged");
	return ok;
}

} // namespace sfve::mod
