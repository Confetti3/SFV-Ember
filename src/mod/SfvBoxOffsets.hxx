#pragma once

// The fighters' boxes and the camera the fight is drawn through, as SFV's
// battle holds them: what Ember's hitbox viewer reads in Training
// (docs/design/beta8/hitboxes.md). Part of SfvOffsets.hxx, which includes
// this file and verifies every site below with the rest of the build table
// (kSites), so on another build the mod stays inert. The viewer only reads
// these fields; nothing here is written.
//
// Same build as SfvOffsets.hxx (Steam 13173604, PE timestamp 0x648AD3A4),
// from our IDA database; the record layout was checked against two of the
// maintainer's desync state files (hitboxes.md, "Evidence").

#include "mod/BuildCheck.hxx"

#include <stdint.h>

namespace sfve::mod::sfv {

// void PlaceAttackBoxes(AttackBlock*, const float pos[2], const float scale[2],
// bool mirrored): each listed record's world rectangle (+96: x, y, w, h) from
// its own (+0 x, +4 y, +12 w, +16 h): x = pos.x + scale.x * x, or
// pos.x - scale.x * (x + w) when mirrored. Chara Prepare (task 0x2020020, rva
// 0x37D9A0) calls it on CharaActionPart+0x1F0 with the fighter's position, and
// rva 0x4705C0 on a projectile's, every tick.
constexpr CodeSite kPlaceAttackBoxes{"PlaceAttackBoxes", 0x3826D0,
	{0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x10, 0xF3, 0x0F, 0x10, 0x1D, 0x4E, 0x83}};
// The same for the body block (CharaActionPart+0x16E0), whose lists are by
// group; a record with flag 0x1000000 (+32) is placed from the fighter's
// second position, one of group 3 by rva 0x382650.
constexpr CodeSite kPlaceBodyBoxes{"PlaceBodyBoxes", 0x3827B0,
	{0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41}};
// void ListAttackBox(AttackBlock*, Record*) (the attack block's vtable slot 9):
// files a record this tick under its type (+48), list = block + 4000 + 136 *
// type, at most 16. Its first bytes OR the record's flags (+32) & 0x600000
// into the block's +0xF98.
constexpr CodeSite kListAttackBox{"ListAttackBox", 0x301310,
	{0x8B, 0x42, 0x20, 0x25, 0x00, 0x00, 0x60, 0x00, 0x09, 0x81, 0x98, 0x0F, 0x00, 0x00, 0x48, 0x63}};
// void ListBodyBox(BodyBlock*, Record*) (the body block's slot 9): group =
// block + 3992 + 544 * record+52, and the record goes into each of that
// group's four 136-byte lists whose bit (1, 2, 4, 8) is set in record+48.
constexpr CodeSite kListBodyBox{"ListBodyBox", 0x301380,
	{0x48, 0x63, 0x42, 0x34, 0x4C, 0x69, 0xC0, 0x20, 0x02, 0x00, 0x00, 0x48, 0x8D, 0x81, 0x98, 0x0F}};
// The rectangle shape's constructor: a record's collision shape at +88 (its
// vtable, then x, y, w, h at +8..+20 of the shape: the record's +96..+108),
// whose vtable (the lea below) answers kind 3, the rectangle the overlap test
// (rva 0x4CCEC0) compares as x <= x' + w' and the like.
constexpr CodeSite kBoxRect{"BoxRect", 0x2ED880,
	{0xF3, 0x0F, 0x10, 0x44, 0x24, 0x28, 0x48, 0x8D, 0x05, 0x6B, 0xFE, 0x95, 0x02, 0xF3, 0x0F, 0x11}};
// void AimCamera(CameraUnit*) (the camera unit's update, through its vtable
// rva 0x2C4F160): the point it looks at and its eye, from the fight's space
// through FieldUnit's matrix (+0x1C0, rows first) into the engine's units
// (x * 100, -z * 100, y * 100) at +0x170 and +0x17C, and the horizontal field
// of view at +0x194, 2 atan(tan(vertical / 2) * 1.7777778) of the vertical
// one at +0x104, in degrees.
constexpr CodeSite kAimCamera{"AimCamera", 0x33FE90,
	{0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x57, 0x41, 0x57, 0x48, 0x8D, 0xA8, 0x28}};

namespace boxes {

// The two box blocks of an action part (CharaActionPart and
// ProjectileActionPart alike: ActionPart's controller at part+0xC0 holds the
// attack block at +304 and the body block at +5664; both are inside the part's
// registered run part+0xD8..+0x3E98, so the rollback restores them).
constexpr uint32_t kProjectileActionPartType = 0x3A64D70;
constexpr uint32_t kAttackBlock = 0x1F0;     // constructor rva 0x2EBE60
constexpr uint32_t kAttackBlockSize = 5360;
constexpr uint32_t kBodyBlock = 0x16E0;      // constructor rva 0x2ED440
constexpr uint32_t kBodyBlockSize = 6712;
// Both blocks: up to 32 records of 120 bytes from +8 (the adds rva 0x309560
// and 0x309730), their count (a qword) at +3848.
constexpr uint32_t kRecords = 8;
constexpr uint32_t kRecordSize = 120;
constexpr uint32_t kMostRecords = 32;
constexpr uint32_t kRecordCount = 3848;
static_assert(kRecords + kMostRecords * kRecordSize == kRecordCount, "the records end where their count is");
// A list: 16 record pointers, then their count (a qword) at +128.
constexpr uint32_t kListSize = 136;
constexpr uint32_t kListCount = 128;
constexpr uint32_t kListMost = 16;
// The attack block's seven lists, by the record's type.
constexpr uint32_t kAttackLists = 4000;
constexpr int kAttackTypes = 7;
static_assert(kAttackLists + kAttackTypes * kListSize == 4952, "the attack lists end where the block's next run starts");
// The body block's five groups of four lists.
constexpr uint32_t kBodyLists = 3992;
constexpr uint32_t kBodyGroupSize = 544;
constexpr int kBodyGroups = 5;
constexpr int kBodySubLists = 4;
static_assert(kBodyLists + kBodyGroups * kBodyGroupSize == kBodyBlockSize, "the body lists fill the block");
static_assert(kBodySubLists * kListSize == kBodyGroupSize, "a group is four lists");
// A record: its type (attack) or list bits (body), its group (body), its
// shape's vtable and the world rectangle PlaceAttackBoxes and PlaceBodyBoxes
// write (fight units: left, bottom, width, height).
constexpr uint32_t kRecordType = 48;
constexpr uint32_t kRecordGroup = 52;
constexpr uint32_t kRecordShape = 88;
constexpr uint32_t kRecordRect = 96;
// The rectangle's vtable (data rva), from kBoxRect's lea.
constexpr uintptr_t kRectShapeVtable = 0x2C4D6F8;

// The attack block's types, as the game's attack detection tests them (the
// order table at rva 0x2C51780, read by rva 0x34E770 through 0x344A00):
// 0, 1 and 2 strike a body box whose list bit is theirs; 1 also meets the
// other side's type 1 (rva 0x34DBD0, a projectile clash); 4 is tested against
// the opponent's body boxes without using up a hit (rva 0x300020 skips 4 and
// 6), and in the recorded states it reaches 2 m ahead of a fireball and of
// Cody's rock on the floor: the proximity box that makes the opponent guard;
// 6 is the throw (rva 0x34E3E0 and the order table's last-but-one entry). 3
// was never found tested; 5 is met against the opponent's type 0 with a
// priority (rva 0x34ED80), a box that catches attacks.
enum class AttackType : int { Strike = 0, Projectile = 1, Hit2 = 2, Unused3 = 3, Proximity = 4, Catch = 5, Throw = 6 };

} // namespace boxes

namespace camera {

constexpr uint32_t kCameraUnitType = 0x3A648A0; // as SfvRegionNames' CameraUnit
constexpr uint32_t kTarget = 0x170;             // float3, the engine's units
constexpr uint32_t kEye = 0x17C;                // float3, the engine's units
constexpr uint32_t kFieldOfView = 0x194;        // float, horizontal, degrees
// The picture's aspect AimCamera widens the vertical field of view by.
constexpr float kAspect = 1.7777778f;
// FieldUnit (type below, the one at index 0): the fight's frame in the world,
// a 4x4 matrix, rows first, that AimCamera puts the camera through.
constexpr uint32_t kFieldUnitType = 0x3A64B78;
constexpr uint32_t kFightToWorld = 0x1C0;
// The engine's units per fight unit.
constexpr float kWorldScale = 100.0f;

} // namespace camera

// kBoxRect's lea (bytes 6..12) addresses the rectangle's vtable: its rel32
// from the next instruction.
static_assert(kBoxRect.bytes[6] == 0x48 && kBoxRect.bytes[7] == 0x8D && kBoxRect.bytes[8] == 0x05 &&
		kBoxRect.rva + 13 + (uint32_t(kBoxRect.bytes[9]) | uint32_t(kBoxRect.bytes[10]) << 8 | uint32_t(kBoxRect.bytes[11]) << 16 |
								uint32_t(kBoxRect.bytes[12]) << 24) == boxes::kRectShapeVtable,
	"the rectangle's vtable is the one the shape's constructor stores");
// kListBodyBox's movsxd rax, [rdx+0x34]; imul r8, rax, 0x220; lea rax, [rcx+0xF98].
static_assert(kListBodyBox.bytes[3] == boxes::kRecordGroup && kListBodyBox.bytes[7] == (boxes::kBodyGroupSize & 0xFF) &&
		kListBodyBox.bytes[8] == (boxes::kBodyGroupSize >> 8) && kListBodyBox.bytes[14] == (boxes::kBodyLists & 0xFF) &&
		kListBodyBox.bytes[15] == (boxes::kBodyLists >> 8),
	"the body lists are the ones the game files its records in");
// kListAttackBox's mov eax, [rdx+0x20] and or [rcx+0xF98], eax: the record's
// flags and the attack block's flags word, just before its lists at +4000.
static_assert(kListAttackBox.bytes[2] == 0x20 && kListAttackBox.bytes[10] == 0x98 && kListAttackBox.bytes[11] == 0x0F,
	"the attack block's flags sit just before its lists");

} // namespace sfve::mod::sfv
