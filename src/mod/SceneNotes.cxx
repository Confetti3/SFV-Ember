#include "mod/SceneNotes.hxx"

#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

namespace sfve::mod {

void SceneNotes::Reset()
{
	scenes_ = {};
	scenesSkips_ = 0;
	roundScenes_ = false;
	fightSeen_ = false;
}

void SceneNotes::Note(SfvAdapter& adapter, int frame)
{
	const int before = scenes_.From();
	const bool decided = adapter.MatchDecided();
	scenes_.Note(intro_skip::Scenes(adapter.FlowState(), decided), frame);
	if (before < 0 && scenes_.From() >= 0) {
		// A round that does not decide the match ends in the scenes between
		// two rounds; the deciding one's are the match's (the decision is
		// made in the KO's own tick, before Finish).
		scenesSkips_ = 0;
		roundScenes_ = !decided;
		if (roundScenes_) {
			Log("netplay: a round is over at frame %d (flow state %d, %u input tick(s) in the last update); its scenes "
				"play to the next round's call", frame, adapter.FlowState(), adapter.TicksInLastUpdate());
		} else {
			Log("netplay: the match is decided at frame %d (flow state %d, %u input tick(s) in the last update); its "
				"scenes play to the result", frame, adapter.FlowState(), adapter.TicksInLastUpdate());
		}
	} else if (before >= 0 && scenes_.From() < 0) {
		if (roundScenes_ && adapter.FlowState() == sfv::flow::kFight) {
			// A rollback took back the KO this copy guessed: the fight again.
			Log("netplay: the round's end seen at frame %d was taken back by a rollback at frame %d", before, frame);
		} else if (roundScenes_) {
			Log("netplay: the round's scenes took %d frame(s), from the round's end at frame %d to the next round at "
				"frame %d, flow state %d (%d skip(s) applied)", frame - before, before, frame, adapter.FlowState(), scenesSkips_);
		} else {
			Log("netplay: the match is no longer decided at frame %d (flow state %d): a rematch in place, or a rollback "
				"took the deciding hit back", frame, adapter.FlowState());
		}
	}
}

void SceneNotes::Skip(SfvAdapter& adapter, int played)
{
	const char* skipped = adapter.TakeScenesSkip();
	if (!skipped) return;
	scenesSkips_++;
	Log("netplay: scenes skipped at frame %d: %s", played, skipped);
}

void SceneNotes::End(int frame)
{
	if (scenes_.From() < 0) return;
	Log("netplay: the scenes took %d frame(s), from the decision at frame %d to the result at frame %d (%d skip(s) applied)",
		scenes_.Shown(frame), scenes_.From(), frame, scenesSkips_);
}

bool SceneNotes::FightStart(SfvAdapter& adapter, int frame)
{
	if (!adapter.Fighting()) {
		fightSeen_ = false;
		return false;
	}
	if (fightSeen_) return false;
	fightSeen_ = true;
	// What a round's end must leave clean for the next round (docs/design/beta9/skip.md):
	// the battle frame's state word (pause bits 0x7, the KO's 0x20) and its
	// slow motion (+0x1A0).
	uint32_t state = 0, slow = 0;
	uint8_t* battleFrame = adapter.BattleFrame();
	if (battleFrame) {
		TryRead(battleFrame + sfv::battleframe::kState, &state);
		TryRead(battleFrame + 0x1A0, &slow);
	}
	Log("netplay: a round's fight starts at frame %d; battle frame state word %08x (pause bits %x, KO bit %d), slow motion %08x",
		frame, state, state & sfv::pause::kBits, (state & 0x20) != 0, slow);
	return true;
}

} // namespace sfve::mod
