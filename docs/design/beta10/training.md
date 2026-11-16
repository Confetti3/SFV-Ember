# Ember's Training lab over SFV's Training (beta 10)

The maintainer asked for SF4 Ember 1.6.0's Training controls on SFV "where
it makes sense": the recording and reply controls and the frame meter's
personalisation, and for everything to work in SFV's own Training battle.
This note says what SFV Ember now has, what it leaves to SFV, how the game is
reached, and what is still unproven.

## What a player gets

Over SFV's Training battle (Home's Training row or a room's Wait in Training,
section 27 of EMBER_LIBRARY.md):

| Key / pad | What it does |
|---|---|
| F6, or View held then Start | Opens and closes the lab's controls (the library's Training panel). |
| F7 | Record: standby (P1's pad plays the dummy), then record, then stop. From rest on a recorded slot it opens the recordings and asks before overwriting, as on SF4. |
| F8 | Play a slot that is on, or stop the playback. |
| View held then Start on standby | Starts the recording, as SF4's chord does. |
| F5 | Hides or shows the lab's HUD (meter, strip, input displays, hitboxes). |

The controls' pages (the same panel SF4 Ember draws, its pages chosen by
`Capabilities::trainingFeatures`):

- **Dummy recording**: eight slots (Select chooses an empty one or turns a
  recorded one on or off), Record, Play (once or looped), Order (random or
  in order), Clear, Save as (a name; written to `recordings\` beside
  `training.json` in `%APPDATA%\SFVEmber`), Load (a saved file into the
  selected slot) and Mirror.
- **Input history**: both sides' inputs as the battle recorded them.
- **Frame data**: each side's last reading, the colour key, the meter's
  style (angled, flat, classic), frames shown (60, 90, 120), the recovery
  number; **Meter settings**: position (the bottom edge at 12..92 % of the
  height), history, after pause (start over or continue), scroll (pages or
  smooth); **Classic**: bar height and the classic parts (data, P2, inputs,
  colours, extras, slant).
- **HUD**: its size, its parts (numbers, bars, state line, shortcuts,
  status), the input displays (off, stick, arrows, both) and **Hitboxes**
  (the switch; Training select's Hitboxes row is the same switch).
- **Dummy reply**: when (off, after hit, after block, on wake-up, any),
  order, timing (-5..+5 frames), chance (25..100 %). It replies with the
  recording slots that are on.

All of it is kept in `training.json` by the library's one owner
(`ui::TrainingMeterOptions`, `TrainingLabState`); SfvHitboxes' own copy of
that file is gone (review D8).

## What SFV does not get, and why

- The dummy's own settings (action, guard, recovery, gauges) and its
  stance at random: SFV's pause menu has them and writes them itself.
- Position save and reset: SFV's own reset (and its Back) stays the game's.
- Typed replies and the moves list: USF4's command tables.
- Combo check, the phone remote, the Data panel: SF4's battle internals.
- The hitboxes' parts: SFV's box view has no skeleton and draws every hurt
  box, so only the switch is offered.
- Pad buttons of its own for Record and Play: SFV Ember has no button layout
  page to bind them on; F7, F8 and the chord on standby do it.
- Esc does not stop standby or a playback (SFV's keyboard pause is Esc);
  F7 and F8 stop them.
- Invincibility marks on the meter: SFV's hurtbox invincibility was not read
  (it needs the box lists' state per tick; not settled in IDA), so none are
  drawn, as before.
- The meter over Practice rooms, watched games and replays (`caps.frameMeter`,
  `replayMeter`): next (it needs a sample per confirmed frame in
  `RollbackSession`, which this stream did not touch).

## How the game is reached

Everything below is in our IDA database (image base 0x140000000, build
13173604) and written in `src/mod/SfvDummyOffsets.hxx`.

- **The input.** InputRecord (rva 0x43B310) writes each local slot's value
  for the tick (`InputUnit+0x118 + 4*slot`) into the slot's ring; the battle
  reads its input back from the ring one tick later (rva 0x4440D0, the input
  frame `+0x2180` = the record frame - 1). The input unit's setup (rva
  0x439A60) gives each present side its slot in side order and marks control
  kinds 0, 2 and 3 local, so in Training both slots are recorded whatever the
  dummy does. Training's own playback is an input source of class
  `TrainingRecordUnit` (registered at rva 0x143BD0, vtable rva 0x2C72AF0)
  whose fill (rva 0x4C0020) writes its recording into one slot before
  InputRecord. The lab writes the two slots inside InputRecord's hook
  (`mod::SetTrainingPadsHook`, after the game's fill), so its input is the
  battle's input that tick: the lowest place the fight reads, and one tick of
  lag, which is the library's `DummyInputLag`.
- **Who plays the dummy.** The match data (`GetBattleData`, rva 0x307F90,
  the same object as rva 0x2F85B0) holds per side (1424-byte blocks) a
  control kind at +540 and a pad at +544: 0 a local pad, 1 remote, 2 CPU,
  3 Training's dummy. Training's dummy action is entry 0 of the Training
  settings at +5908 ({value, default, min, max} x 14; reader rva 0x2F9E50,
  setter rva 0x309390): 0 stand, 1 crouch, 2 jump, 3 CPU, 4 Player 2's
  controller, 5 and 6 the game's own playback, 7 the game's own recording.
  The pause menu (rva 0x4A82D0, tables at rva 0x2C73040) writes it and rva
  0x3CAB90 sets side 1's control from it (3 -> CPU, 4 -> {0, pad 1},
  7 -> {0, pad 0}, else {3, -1}). The fighters' guard and other dummy
  behaviours check the kind against 2 and 3 (rva 0x348190, 0x374AD0, ...).
  While the lab supplies the dummy's input it gives side 1 Training's own
  "Player 2's controller" control (kind 0, its slot), from the first update
  it supplies to the one after the last, and then puts back what was there
  (`training::DummyControl`, as SF4's lab does with SF4's player control). A
  dummy action changed in SFV's pause menu meanwhile stands and the lab
  stops. None of this is in the game's rollback snapshot (+6176..+6404 of
  the match data is).
- **The stored form.** A slot holds the battle's recorded value, which is
  after the player's button settings (rooms exchange it between copies whose
  players set their buttons differently, and it plays the same moves on
  both), in the lab's fight-button word: SFV's HP bit 0x200 swapped with the
  library's 0x400 (`SfvLabInput::ToLab`), everything else as SFV has it.
  Left and Right are the screen's, held for a fighter facing right and
  turned for the dummy's facing (the action part's +0x3E90) on each frame.
- **The dummy's free frame.** CharaUnit+0x91E (the frame meter's
  untilActionable): after a tick, the ticks until the fighter's state is its
  own again, hitstop included, 1000 lying down. Training's own reversal
  recording times its replies on the same field (DummyControlUnit's
  PreInput, rva 0x3DE3F0, cases 14 and 15). The lab gives it to the stun
  watch as the frames to the free frame (`SfvLabInput::ScriptedFrames`) for
  hit, block, launch, down and getting up; the library's reply decision
  (`ReplyDecision`, unchanged) plays the reply so its first attack button
  is acted on that frame.
- **One gate.** Every write is behind `LabBattle`: SfvTraining's Training
  battle (`TrainingBattleState` in front, no scene change pending) and the
  adapter's battle. The pads hook runs only for a battle the pads play
  (`PadSource::Pads`); a room's battle and a replay are played by a session
  and never reach it.

## The code

Library (`b10/lib-training`):
- `training/LabCore.hxx`: `LabChannel` (the published view, the command
  queue and the room's call under one lock) and `ReplyLab` (the plan, the
  stun watch, the decision, the trace and the draws), extracted from SF4's
  `TrainingRuntime.cxx`, which now uses them in the same order.
- `common/TrainingFeatures.hxx` and `Capabilities::trainingFeatures`; the
  panel, HUD, shortcut strip and Data panel are neutral and gated by it;
  SF4's own pages are `ui/TrainingPanelSf4.cxx` behind `TrainingExtraPages`.
- `Session::SetFacing`; the recordings' save and load by name.
- Test: `training_lab_core_test.cxx` (`TrainingLabCore`).

SFV (`b10/training`):
- `mod/SfvDummyOffsets.hxx`, `SfvAdapter`'s pads hook and
  `TrainingBattleData`, `FighterReader`'s facing and position.
- `ember/SfvLabInput` (pure: the word, the free frame), `ember/SfvTrainingLab`
  (the game thread's lab), `ember/SfvTrainingHost` (the overlay's side),
  `SfvFrameMeter` as the battle's one follower (meter, hitboxes, lab),
  `SfvHitboxes` without its file, `SfvGame`'s features.
- Test: `tests/SfvTrainingLabTests.cxx`.
- Tools: `tools/traininglab.py`, `runtraining.py --part lab`; the pipe's
  `traininglab ...` (record, play, stop, select, slot, loop, order, clear,
  mirror, save, load, reply, meter, controls, page, hud, and two test aids:
  `setting` for one of Training's own settings, `leave`) and the statuses
  `traininglab` and `traininghost`.

## In game (2026-11-16, laptop, the integration build d007ae5 / 81579ea62)

One copy, RYU against RYU, GUL, ZGF, Z25 and CNL, driven through the pipe
(`laptop-setup/sfv-b10tr-run.py`, job sfv-b10tr-3; the two starts before it
stopped at the test tool). The log lines are the game's.

- The dummy plays from its slot: on standby the dummy's control read kind
  0, pad 1, P1's pad played P2 (the meter logged P2's script 600, P1 none),
  and Stop gave the control back to kind 3, pad -1, as it was at the battle's
  start ("its control kind 3, pad -1, action 0").
- View held then Start opened the controls and not SFV's pause; on standby it
  started the recording. The controls and every page drew over the game; each
  meter option changed the HUD (shots: style, frames, size, position,
  history, after pause, scroll, bar height, colours, parts, input displays,
  hitboxes, F5).
- A Hadoken and a throw recorded on standby (P2's scripts 900 and 715) played
  back with P2 on the right, and again with P2 on the left (P1 at x 3.07,
  P2 at 1.55): scripts 900 and 715 again, so the slot was turned for its
  facing. Looped (the cursor went round twice in 16 s); saved (12,809 bytes),
  cleared and loaded back (265 frames).
- Training setting 1 = 1 makes the dummy block (setting 1 is the guard).
  Setting 2 changes the dummy's rise: 0 plays rise script 240, 1 and 3 script
  246, 2 none the trace saw.
- RYU's reply after block, after hit and on wake-up (normal rise): the reply's
  move began on the dummy's first free frame each time ("dummy free 2, in its
  move 2; move 0 after free, 0 after the button was acted on").
- Off elsewhere: in an offline Versus battle the lab is not available and
  writes nothing; in a private room's game (two copies, 20 s) neither copy's
  lab is available and neither logs a lab line.

## Unproven

- The replies over GUL, ZGF, Z25 and CNL: the run's tool pressed Record on
  the slot RYU's battle had kept, which asks before overwriting; the tool now
  clears it first (`traininglab.py`), not run again.
- A wake-up reply after a quick or back rise (setting 2's 1 and 3), and
  V-Reversals.
