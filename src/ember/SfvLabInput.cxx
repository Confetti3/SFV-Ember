#include "ember/SfvLabInput.hxx"

#include "training/RecordingTypes.hxx"

namespace sfve::adapter::lab {

namespace {
using mod::sfv::fighter::StateKind;
static_assert(ToLab(0x10 | 0x20 | 0x40 | 0x80 | 0x200 | 0x800 | 0xF) == sf4e::training::FightButtons,
	"SFV's six attacks and four directions are the lab's fight buttons");
static_assert(ToSfv(ToLab(0xFFF)) == 0xFFF, "the swap loses no bit");
} // namespace

int ScriptedFrames(const mod::FighterFrame& dummy)
{
	if (!dummy.valid || dummy.untilActionable <= 0 || dummy.untilActionable >= mod::sfv::fighter::kUntimed) return -1;
	switch (dummy.stateKind) {
	case StateKind::Hit: case StateKind::GuardHit: case StateKind::LaunchHit:
	case StateKind::Down: case StateKind::LyingDown: case StateKind::Rise:
		return dummy.untilActionable;
	default:
		return -1;
	}
}

} // namespace sfve::adapter::lab
