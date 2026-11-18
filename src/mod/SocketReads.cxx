#include "mod/SocketReads.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/ProjectileRelease.hxx"

#include <intrin.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>

namespace sfve::mod {

namespace {

// sfv::kSocketWorldPosition: (CharaVisualPart*, socket name, float out[4],
// flag); the flag is passed on untouched.
using SocketFn = void (*)(uint8_t* visualPart, const wchar_t* socket, float* out, uint64_t flag);

SocketFn o_socket;
uintptr_t s_base;
bool s_log;
// -sfve_socket_anchor: the reads that may feed fight state are given the
// fight's position in a battle a session plays (socket_reads::Policy).
bool s_anchor;

// The update now running (NoteSocketUpdate, before the update's tasks start)
// and the battle's counts. The reads run on the battle's job threads, several
// units' at once; they are few (spawns at a bone), so one lock serves them all.
std::mutex s_mutex;
SocketUpdate s_update;
socket_reads::Counts s_counts;
// The call sites not classified that were anchored this battle, each logged
// once (ForgetSocketReads), at most kMostUnknownSites of them.
constexpr int kMostUnknownSites = 8;
uint32_t s_unknownSites[kMostUnknownSites];
int s_unknownSiteCount = 0;
std::atomic<int> s_lines;
// A runaway caller cannot fill the disk: the log stops here.
constexpr int kMostLines = 20000;
// The anchor lines logged in any build, once a battle each (ForgetSocketReads):
// a read whose unit, position or facing was not known; a name the claw's site
// has no offset for.
std::atomic<bool> s_anchorUnresolvedLogged;
std::atomic<bool> s_anchorNameLogged;

// The calls of the socket read that are not the fight's, by their return
// addresses (call site + 5; read in IDA, build 13173604).
constexpr uint32_t kNotTheFights[] = {
	0x37D48E,                     // Chara PreInput (rva 0x37D250): bone 28 into CharaUnit+1984 (masked, the camera's)
	0x31B10E, 0x31B1DE,           // the camera's handler rva 0x31AF60
	0x32FA5F,                     // the camera's handler rva 0x32F790
	0x33C41F,                     // the camera's handler rva 0x33C1C0
	0x3E3EF0, 0x3E3F57,           // rva 0x3E3E60, from rva 0x3E39F0
	0x38F401, 0x38F69B, 0x38F6B7, // the head (b_C_Head) for Chara PostVisual (rva 0x3897C0 through 0x3A53E0), after the tick
	0x39D8BB,                     // CharaUniquePart_010: a VFX at the head
	0x404B2D,                     // CursorPart: interface
	0x434FB5,                     // TutorialPart: interface
	0x43986C, 0x439882,           // a TutorialPart sub-object: interface
};
// The spawn bone resolver (rva 0x4CFB70, which tail-calls through rva
// 0x32BAA0): any script's spawn with a bone. Its position goes only to a
// VfxFieldGroupUnit (type 0x3A75050, its name registered at rva 0x15E500)
// through rva 0x4D9180 (the object's virtuals +56, +16 and +8), and that
// unit's manager, VfxManagerUnit (registered at rva 0x4D4970), keeps no
// rollback state (docs/design/SFV_REGION_MAP.md: post-restore callbacks
// only). So the read is drawn only: an effect, never fight state.
constexpr uint32_t kSpawnBoneResolver = 0x4CFC99;

// The four reads that feed fight state.
constexpr uint32_t kItemSiteI = 0x3BDB02;  // CharaUniquePart_036: the item let go as V-Trigger I ends (Sock_Item_R or Sock_Item_L), which becomes a projectile (script 50, kind 200)
constexpr uint32_t kItemSiteII = 0x3BDF3B; // CharaUniquePart_038: the item let go as V-Trigger II ends
constexpr uint32_t kPropSite = 0x3BC294;   // CharaUniquePart_015: a dropped prop (b_C_Head), the same kind of projectile
constexpr uint32_t kClawSite = 0x3A4B79;   // CharaUniquePart_008: where the claw (b_R_Hand, b_L_Hand) or the mask (b_C_Head) starts when knocked off
constexpr uint32_t kKnownSites[] = {kItemSiteI, kItemSiteII, kPropSite, kClawSite};

// The socket names the claw's site gives offsets of their own (AnchorOffsetFor).
constexpr const wchar_t* kRightHand = L"b_R_Hand";
constexpr const wchar_t* kLeftHand = L"b_L_Hand";
constexpr const wchar_t* kHead = L"b_C_Head";
constexpr const wchar_t* kNamedSockets[] = {kRightHand, kLeftHand, kHead};

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

// The CharaUnit a visual part belongs to, or null.
const uint8_t* OwnerOf(const uint8_t* visualPart)
{
	const uint8_t* chara = nullptr;
	return visualPart && TryRead(visualPart + sfv::visual::kOwnerObject, &chara) ? chara : nullptr;
}

// One read as the log names it.
struct ReadContext {
	int32_t frame = -1;
	bool replaying = false;
	int32_t unit = -1;
	int32_t side = -1;
	const wchar_t* socket = nullptr;
	uint32_t from = 0;
};

void LogRead(const ReadContext& read, const char* what, const float* p)
{
	if (!s_log || s_lines.fetch_add(1) >= kMostLines) return;
	const std::string name = SocketName(read.socket);
	char who[32];
	if (read.unit >= 0) snprintf(who, sizeof(who), "unit %d (P%d)", read.unit, read.side + 1);
	else snprintf(who, sizeof(who), "no known unit");
	Log("socket: frame %d%s, %s, socket %s, from rva 0x%X: %.5f %.5f %.5f %s", read.frame, read.replaying ? " replayed" : "", who,
		name.c_str(), read.from, p[0], p[1], p[2], what);
}

// A call site not classified, anchored: logged the first time each is seen
// in a battle (in any build: the site wants classifying).
void NoteUnknownSite(const ReadContext& read)
{
	{
		std::lock_guard lock(s_mutex);
		s_counts.unknownSites++;
		if (std::find(s_unknownSites, s_unknownSites + s_unknownSiteCount, read.from) != s_unknownSites + s_unknownSiteCount) return;
		if (s_unknownSiteCount >= kMostUnknownSites) return;
		s_unknownSites[s_unknownSiteCount++] = read.from;
	}
	Log("socket: frame %d, unit %d (P%d), socket %s, from rva 0x%X: a call site not classified; anchored with the head's offset "
		"(logged once a battle)",
		read.frame, read.unit, read.side + 1, SocketName(read.socket).c_str(), read.from);
}

// An anchored read: the value computed from the owner's fight position, the
// same on every copy, written over what the engine's read just wrote. When
// the unit, its position or its facing is not known, the engine's value
// stays: counted, and logged once a battle.
void GiveAnchor(const ReadContext& read, const UnitPlace& unit, const socket_reads::ReadRule& rule, float* out)
{
	if (!unit.anchorable) {
		{
			std::lock_guard lock(s_mutex);
			s_counts.anchorUnresolved++;
		}
		if (!s_anchorUnresolvedLogged.exchange(true)) {
			Log("socket: frame %d, unit %d (P%d), socket %s, from rva 0x%X: anchor unresolved (the fighter's unit, position or facing is "
				"not known): the engine's value kept; counted, logged once a battle",
				read.frame, read.unit, read.side + 1, SocketName(read.socket).c_str(), read.from);
		}
		return;
	}
	if (rule.otherName && !s_anchorNameLogged.exchange(true)) {
		Log("socket: frame %d, unit %d (P%d), socket %s, from rva 0x%X: anchor: no offset for this socket at this call site, the "
			"head's is used (logged once a battle)",
			read.frame, read.unit, read.side + 1, SocketName(read.socket).c_str(), read.from);
	}
	if (rule.site == socket_reads::Site::Unknown) NoteUnknownSite(read);
	const socket_reads::Position given = socket_reads::AnchorPosition(unit.position, unit.facingLeft, rule.offset);
	memcpy(out, given.xyzw, sizeof(given.xyzw));
	{
		std::lock_guard lock(s_mutex);
		s_counts.anchored++;
	}
	LogRead(read, "from the fight's position", given.xyzw);
}

void h_Socket(uint8_t* visualPart, const wchar_t* socket, float* out, uint64_t flag)
{
	o_socket(visualPart, socket, out, flag);
	const uint32_t from = (uint32_t)(reinterpret_cast<uintptr_t>(_ReturnAddress()) - s_base);
	const socket_reads::ReadRule rule = socket_reads::Policy(from, socket, s_anchor && BattleLatch().Latched());
	// A Live read keeps what the engine's read wrote. An anchored one with
	// nowhere to write has nothing to give.
	if (!rule.anchor || !out) return;
	ReadContext read;
	read.from = from;
	read.socket = socket;
	const uint8_t* chara = OwnerOf(visualPart);
	UnitPlace unit;
	{
		std::lock_guard lock(s_mutex);
		read.frame = s_update.inputFrame;
		read.replaying = s_update.replaying;
		for (int i = 0; chara && i < s_update.unitCount; i++) {
			if (s_update.units[i].chara == chara) unit = s_update.units[i];
		}
	}
	read.unit = unit.unit;
	read.side = unit.side;
	GiveAnchor(read, unit, rule, out);
}

// The units' meshes: each unit's components logged once a battle, and the
// times the LOD had to be forced again after that (a few, then quiet).
std::atomic<bool> s_meshesLogged[kMostFighterUnits];
std::atomic<int> s_reforced;
constexpr int kMostReforcedLines = 8;

// A component the game would use: not marked unusable, and of a class with
// the skeletal mesh component cast flag (rva 0xE5F7D0, 0xE58F10).
bool UsableSkeletalMesh(const uint8_t* component)
{
	int32_t flags = 0;
	const uint8_t* cls = nullptr;
	uint64_t cast = 0;
	return component && TryRead(component + sfv::mesh::kObjectFlags, &flags) && (flags & sfv::mesh::kUnusable) == 0 &&
		TryRead(component + sfv::mesh::kObjectClass, &cls) && cls && TryRead(cls + sfv::mesh::kClassCastFlags, &cast) &&
		(cast & sfv::mesh::kSkeletalMeshComponentFlag) != 0;
}

int ForceOne(uint8_t* component, int which, std::vector<socket_reads::MeshLod>* seen)
{
	namespace m = sfv::mesh;
	if (!UsableSkeletalMesh(component)) return 0;
	socket_reads::MeshLod lod;
	lod.component = which;
	if (!TryRead(component + m::kForcedLodModel, &lod.forced) || !TryRead(component + m::kMinLodModel, &lod.minLod) ||
		!TryRead(component + m::kPredictedLodLevel, &lod.predicted) || !TryRead(component + m::kUpdateFlag, &lod.updateFlag)) {
		return 0;
	}
	if (lod.forced != m::kForceLodZero) lod.written = TryWrite(component + m::kForcedLodModel, m::kForceLodZero);
	// A mesh that only ticks its pose when rendered would not be evaluated
	// while the window is minimised or occluded: the sockets would read the
	// last drawn pose.
	if (lod.updateFlag != m::kAlwaysTickPoseAndRefreshBones) {
		lod.flagWritten = TryWrite(component + m::kUpdateFlag, m::kAlwaysTickPoseAndRefreshBones);
	}
	if (seen) seen->push_back(lod);
	return lod.written || lod.flagWritten ? 1 : 0;
}

std::string Describe(const std::vector<socket_reads::MeshLod>& seen)
{
	std::string text;
	for (const socket_reads::MeshLod& lod : seen) {
		char one[160];
		const std::string kind = lod.component == 0 ? "mesh" : lod.component == 1 ? "second mesh" : "layer " + std::to_string(lod.component - 2);
		snprintf(one, sizeof(one), "%s%s LOD %d (forced %d, min %d, update flag %u)%s%s", text.empty() ? "" : "; ", kind.c_str(),
			lod.predicted, lod.forced, lod.minLod, (unsigned)lod.updateFlag, lod.written ? ", now LOD 0" : "",
			lod.flagWritten ? ", now ticking every frame" : "");
		text += one;
	}
	return text;
}

} // namespace

void KeepFighterMeshesAtLodZero(uint8_t* const* visualParts, int count)
{
	for (int unit = 0; unit < count && unit < kMostFighterUnits; unit++) {
		uint8_t* actor = nullptr;
		if (!visualParts[unit] || !TryRead(visualParts[unit] + sfv::visual::kActor, &actor) || !actor) continue;
		if (!s_meshesLogged[unit].load()) {
			std::vector<socket_reads::MeshLod> seen;
			socket_reads::ForceLodZero(actor, &seen);
			if (seen.empty()) continue; // the actor's meshes are not there yet: look again next update
			s_meshesLogged[unit].store(true);
			// "fighter k": the battle's order of its fighter units (CharaUnit[k]), not the side.
			Log("meshes: fighter %d: %zu skeletal mesh component(s): %s", unit, seen.size(), Describe(seen).c_str());
			continue;
		}
		const int written = socket_reads::ForceLodZero(actor, nullptr);
		if (written && s_reforced.fetch_add(1) < kMostReforcedLines) {
			Log("meshes: fighter %d: %d component(s) given LOD 0 and the pose tick again (something reset ForcedLodModel or the update flag)", unit, written);
		}
	}
}

namespace socket_reads {

int ForceLodZero(uint8_t* actor, std::vector<MeshLod>* seen)
{
	namespace v = sfv::visual;
	if (!actor) return 0;
	int written = 0;
	uint8_t* component = nullptr;
	if (TryRead(actor + v::kActorMesh, &component)) written += ForceOne(component, 0, seen);
	component = nullptr;
	if (TryRead(actor + v::kActorSecondMesh, &component)) written += ForceOne(component, 1, seen);
	uint8_t* layers = nullptr;
	int32_t count = 0;
	if (TryRead(actor + v::kActorLayers, &layers) && TryRead(actor + v::kActorLayerCount, &count) && layers && count > 0 &&
		count <= v::kMostLayers) {
		for (int32_t i = 0; i < count; i++) {
			component = nullptr;
			if (TryRead(layers + sizeof(uint8_t*) * (size_t)i, &component)) written += ForceOne(component, 2 + i, seen);
		}
	}
	return written;
}

Site SiteOf(uint32_t returnRva)
{
	if (std::find(std::begin(kNotTheFights), std::end(kNotTheFights), returnRva) != std::end(kNotTheFights)) return Site::NotTheFights;
	if (returnRva == kSpawnBoneResolver) return Site::DrawOnly;
	if (std::find(std::begin(kKnownSites), std::end(kKnownSites), returnRva) != std::end(kKnownSites)) return Site::Known;
	return Site::Unknown;
}

const char* SiteName(Site site)
{
	switch (site) {
	case Site::NotTheFights: return "not the fight's";
	case Site::DrawOnly: return "drawn only";
	case Site::Known: return "known";
	case Site::Unknown: return "not classified";
	}
	return "?";
}

AnchorOffset AnchorOffsetFor(uint32_t returnRva, const wchar_t* socket, bool* otherName)
{
	if (otherName) *otherName = false;
	if (returnRva == kItemSiteI || returnRva == kItemSiteII) return {0.20f, 1.00f};
	if (returnRva != kClawSite) return kHeadOffset;
	const uint64_t name = NameKey(socket);
	if (name == NameKey(kRightHand) || name == NameKey(kLeftHand)) return {0.30f, 1.20f};
	if (name != NameKey(kHead) && otherName) *otherName = true;
	return {0.05f, 1.60f};
}

std::string AnchorRulesText()
{
	const auto rva = [](uint32_t site) {
		char text[16];
		snprintf(text, sizeof(text), "0x%X", site);
		return std::string(text);
	};
	const auto offset = [](const AnchorOffset& o) {
		char text[40];
		snprintf(text, sizeof(text), "%.9g,%.9g", o.forward, o.up);
		return std::string(text);
	};
	std::string text = std::to_string(kAnchorVersion) + "; not the fight's";
	for (const uint32_t site : kNotTheFights) text += " " + rva(site);
	text += "; drawn only " + rva(kSpawnBoneResolver) + "; known";
	for (const uint32_t site : kKnownSites) {
		text += " " + rva(site);
		for (const wchar_t* socket : kNamedSockets) text += " " + SocketName(socket) + " " + offset(AnchorOffsetFor(site, socket));
		text += " other " + offset(AnchorOffsetFor(site, nullptr));
	}
	return text + "; head " + offset(kHeadOffset);
}

ReadRule Policy(uint32_t returnRva, const wchar_t* socket, bool sessionBattle)
{
	ReadRule rule;
	rule.site = SiteOf(returnRva);
	if (!sessionBattle || rule.site == Site::NotTheFights || rule.site == Site::DrawOnly) return rule;
	rule.anchor = true;
	rule.offset = AnchorOffsetFor(returnRva, socket, &rule.otherName);
	return rule;
}

Position AnchorPosition(const float base[4], bool facingLeft, const AnchorOffset& offset)
{
	const float dir = facingLeft ? -1.0f : 1.0f;
	Position p;
	p.xyzw[0] = base[0] + dir * offset.forward;
	p.xyzw[1] = base[1] + offset.up;
	p.xyzw[2] = 0.0f;
	p.xyzw[3] = 1.0f;
	return p;
}

uint64_t NameKey(const wchar_t* name)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (int i = 0; name && i < 64; i++) {
		wchar_t c = 0;
		if (!TryRead(name + i, &c) || !c) break;
		h = (h ^ (uint64_t)(uint16_t)c) * 0x100000001b3ull;
	}
	return h;
}

bool Position::operator==(const Position& o) const
{
	return memcmp(xyzw, o.xyzw, sizeof(xyzw)) == 0;
}

std::string CountsText(const Counts& c)
{
	char text[192];
	snprintf(text, sizeof(text), "%llu given from the fight's position (%llu at a site not classified), %llu anchor unresolved",
		(unsigned long long)c.anchored, (unsigned long long)c.unknownSites, (unsigned long long)c.anchorUnresolved);
	return text;
}

} // namespace socket_reads

void NoteSocketUpdate(const SocketUpdate& update)
{
	std::lock_guard lock(s_mutex);
	s_update = update;
}

void ForgetSocketReads()
{
	{
		std::lock_guard lock(s_mutex);
		s_counts = {};
		s_update = {};
		s_unknownSiteCount = 0;
	}
	for (std::atomic<bool>& logged : s_meshesLogged) logged.store(false);
	s_reforced.store(0);
	s_anchorUnresolvedLogged.store(false);
	s_anchorNameLogged.store(false);
}

std::string SocketReadsSummary()
{
	std::lock_guard lock(s_mutex);
	return socket_reads::CountsText(s_counts);
}

bool InstallSocketReads(uintptr_t imageBase, bool log, bool anchor)
{
	s_base = imageBase;
	s_log = log;
	s_anchor = anchor;
	const bool ok = HookCodeSite(imageBase, sfv::kSocketWorldPosition, reinterpret_cast<void*>(&h_Socket),
		reinterpret_cast<void**>(&o_socket));
	if (!ok) {
		Log("socket: the socket read NOT hooked; every read of the fighters' models is the engine's");
		return false;
	}
	Log("socket: %s%s",
		anchor ? "in a battle a session plays, every read of a fighter's model that may feed fight state (rva 0x3BDB02, 0x3BDF3B, "
				 "0x3BC294, 0x3A4B79, and any call site not classified) is given a value from the fighter's fight position; the "
				 "camera's, the interface's and the effects' keep the engine's" :
				 "every read of a fighter's model keeps the engine's value (-sfve_socket_anchor=0)",
		log ? "; every anchored read logged (-sfve_socket_log)" : "");
	return true;
}

} // namespace sfve::mod
