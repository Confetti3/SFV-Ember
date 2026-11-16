#pragma once

// Who drives each side of SFV's Training battle, and Training's own dummy
// setting, in the match data (sfv::battledata: the object kGetBattleData,
// rva 0x307F90, and rva 0x2F85B0 both return, the owner's +272). Derived
// from our IDA database (build 13173604), docs/design/beta10/training.md:
//
// - Each side's block is 1424 bytes from +0 (sides 0..3; the gauge params
//   of SfvOffsets' kSetPlayerParam sit at +560 in it). +540 is the side's
//   control kind and +544 the pad that drives it:
//     0  a local pad, +544 its index;
//     1  a remote player (the input unit's remote mask);
//     2  the game's CPU;
//     3  Training's dummy (stand, crouch, jump as the dummy setting says).
//   The input unit's setup (rva 0x439A60) gives each present side its slot
//   in side order and marks kinds 0, 2 and 3 local (inputunit::kLocalMask),
//   so in Training both slots are recorded by InputRecord (rva 0x43B310)
//   whatever the dummy's kind. PadsAny (rva 0x2F30D0) reads a kind-0 side's
//   pad by +544; the fighters' own checks compare +540 with 2 and 3 (rva
//   0x348190, 0x374AD0, 0x49B430 and others).
// - The Training settings at +5908: 14 entries of {value, default, min,
//   max} (int32 each), read by rva 0x2F9E50 and set by rva 0x309390. Entry 0
//   is the dummy's action: 0 stand, 1 crouch, 2 jump, 3 CPU, 4 Player 2's
//   controller, 5 and 6 the game's own recording playback, 7 the game's own
//   recording (P1's pad plays the dummy). The pause menu writes it from its
//   tables (rva 0x4A82D0: {0, 3, 4, 6} and {0, 1, 2}) and then rva 0x3CAB90
//   sets side 1's control from it: 3 -> {2, -1}, 4 -> {0, 1}, 7 -> {0, 0},
//   5 and 6 leave it, anything else -> {3, -1}.
//
// None of it is in the game's rollback snapshot (the match data's snapshotted
// part is +6176..+6404, SFV_REGION_MAP.md), and a room's battle never runs
// Training's dummy: the lab writes it only in the Training battle
// (SfvTrainingLab).

#include <stdint.h>

namespace sfve::mod::sfv::dummy {

constexpr uint32_t kSideStride = 1424;
constexpr uint32_t kSideKind = 540;
constexpr uint32_t kSidePad = 544;
constexpr int32_t kKindPad = 0;
constexpr int32_t kKindRemote = 1;
constexpr int32_t kKindCpu = 2;
constexpr int32_t kKindDummy = 3;

constexpr uint32_t kTrainingSettings = 5908;
constexpr uint32_t kSettingStride = 16;
constexpr uint32_t kActionSetting = 0;
constexpr int32_t kActionPlayer2 = 4;

} // namespace sfve::mod::sfv::dummy
