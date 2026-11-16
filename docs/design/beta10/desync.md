# Beta.10: the desyncs of 2026-11-15, game data, and the state the snapshot left out

Two beta.9 field desyncs on 2026-11-15 (`reviews/2026-11-15-sfv-beta10/TRIAGE.md`, section B) each
differed in exactly one region: `CharaUnit[0].CharaActionPart+0xD8` at checkpoint 2440 (P1's Z41 on the first
frame of V-Skill II, script 875; a watcher's copy equalled P1's, so P2's copy was the odd one) and
`CharaUnit[0].CharaMotionPart+0xC0` at 8200 (P1's Z36 two frames into a heavy punch, script 602). P2 ran Windows
10 build 19045. Four of the six beta.8 desyncs had the same shape: the maintainer's copy equalled a straight
playback of the recorded inputs, the opponent's had drifted, and no lag or rollback variant reproduced it.

Every address below is an rva of Steam build 13173604, read in the laptop's IDA database this session unless it
says "from notes". A published analysis of the same build named some of these places; each was checked in our
own database before use, and none of its data or code was taken.

## The two hypotheses

1. **The opponent's game computed with other data** (another balance version of a fighter, or altered move files).
2. **A script start replayed after a rollback reads state the snapshot does not restore** (the action
   controller's command queue, part +0x3EB4, animation state).

What the evidence says:

- **Both copies played the same script on the same frame.** P2's report at 2440 says "P1 script 875 frame 1.00,
  mirrored", as P1's does. The 15808 bytes that differ are the action controller's, most of them 120-byte box
  records that come from the move data. Same script and frame, different controller contents on the script's
  first frame, is what two copies with different move data for that move produce; a stale input to the start
  of a script would more likely show in the script player itself (`->0x3EC8+0x30`, equal on both).
- **The command queue was never carried between updates.** `SfvAdapter::NoteCommandQueues` runs after every
  bound update in every build and logs (`actions:`) any fighter unit whose queue (controller +15832) is not
  empty. The maintainer's log for the whole day (seven battles, about 71,000 frames, over 7,400 rollbacks,
  matches 6 and 7 among them) has no such line. The queue is filled and emptied within one update, so a
  restore cannot keep an abandoned timeline's commands.
- **Part +0x3EB4 is recomputed before anything reads it.** Chara Prepare (task order 0x2020020, rva 0x37DED6)
  copies the facing (+0x3E90, restored) into it every tick. Its readers run later in the same tick: rva
  0x36AC30 from CharaManager AttackReaction (order 0x2030030, rva 0x3DDAF0, registered at rva 0x3D8390), and rva
  0x2FBB50 from a script command handler of the projectile action part (rva 0x458370, its vtable at rva
  0x2C6E9D8), which runs in the projectile tasks (0x207xxxx). Nothing reads it before Chara Prepare.
- **The motion part's registration covers what a script start writes.** The part is 0x34B0 bytes (its
  allocation at rva 0x2F4774, constructor rva 0x343B30). The game registers +0xC0 (13092 bytes) and +0x33E8
  (192). Left out: the part's own header (+0x00..+0xBF), +0x33E4 (cleared by the reset rva 0x358C90 with the
  registered words beside it) and +0x34A8, a flag two characters' unique parts write every tick from fight state
  (rva 0x3BDFF0, 0x3B65B0) and rva 0x3652C0 reads to set animation-layer flags. None is a script's. +0x34A8 is
  open (below), but it cannot be the 8200 difference: that difference is inside the registered +0xC0 run.
- **Socket reads were not involved.** All five reports (P1, P2 and the watcher at 2440; P1 and P2 at 8200) list
  0 socket-read misses in the 120 frames before the checkpoint, and the four anchored sites are not used by
  Z41, Z36 or Z25.
- **Not established:** which of the two the opponent's copy did. Its `.state` files and its version list were
  never sent. Beta.10 now captures both automatically (below), so the next such match decides it.

**Verdict.** No state outside the snapshot was found on the script-start path; the evidence favours hypothesis 1.
Both are now closed where cheap: different data ends the match before frame 0 with its name (sections 1 and
2), and the state the RE did find outside the snapshot on other paths is now kept (sections 4 and 5). If a
copy with equal game data, versions and paks still desyncs this way, hypothesis 2 is back, and the region
exchange (section 6) names the words.

## 1. Balance versions

**How the game picks a fighter's frame data.**

- The battle setting's player record (1424 bytes) holds the fighter's balance version at +156
  (`jump::kSettingVersion`). KWBattlePlayerSetting's SetCharaVersion (rva 0x7C6D10) writes it. The mode
  data's fighter record (CharPkg) carries none: rva 0x7CAA40, which copies a CharPkg into the setting, never
  touches +156. So a launch that writes only the picks leaves whatever the record held.
- The battle's resource load (rva 0x69BC20) and its creation (rva 0x6A45E0) call rva 0x7B32D0
  (`sfv::kResolvePlayerVersion`) for each player first. It looks up the fighter's entry in the fighter table
  (rva 0x57F030), resolves +156 against the entry's list (rva 0x56B360, then 0x56B2E0), writes the result back
  to +156, and to +172 when the record names a support fighter.
- The resolve (rva 0x56B2E0): a version the list holds stays; 500 to 599 stay whatever the list holds
  (development versions, whose path is built from the last entry's by rva 0x553530); anything else, and -1,
  becomes the list's last entry.
- The list is the fighter's BattleResourceList, `/Game/Chara/<code>/DA_<code>_BattleResourceList`
  (KWBattleResourceListAsset). Its path is in the fighter table embedded in the exe; the fighter table's parse
  (rva 0x56D050) loads the asset from the paks (rva 0x53D080). The asset is a TArray of 24-byte entries at +40
  (count +48), each {int version, FString path}. The game's resource lookup (rva 0x5518C0) takes a version's
  path from it.
- **So the list is not static in the exe.** It comes from whichever mounted pak provides that asset. On a stock
  install each fighter's list lives in exactly one pak (an index scan of all 69 stock paks found no duplicate).
  The asset data inside the paks is encrypted, so the lists could not be read offline.
- **What can end a list early on one PC:** any pak the engine mounts that replaces the asset (section 2). That
  includes a mod pak or an older or altered file in `Content\Paks` or any subfolder of it, in
  `Engine\Content\Paks`, or in the game's `Saved\Paks`, which for SFV's installed build is under
  `%LOCALAPPDATA%\StreetFighterV\Saved`. Then -1, or a version that list lacks, silently plays that PC's own
  newest. Two PCs with the same exe can therefore play one match with different frame data.

**What changed.**

- Every launch writes an explicit version per fighter, for both players.
  - `VersusLaunch` makes each pick's `balanceVersion` explicit before the jump (`ChooseVersions`, using the
    pure rule `BalanceVersionRules::ChooseLaunchVersion`): this copy's newest for a room's match and the pipe's
    Training, the recorded one for a replay.
  - A copy whose list lacks that version refuses the launch, naming the fighter: "player N's fighter GUL has no
    balance version 60 on this copy (it has 10, 21, 42, 43): verify the game's files or remove mods".
  - The battle jump's hook set now holds the resolve. `BalanceVersions` writes the armed version into each
    player record, matched by the fighter the record holds, just before the game resolves it.
- **The value each copy loaded is compared before frame 0.**
  - At bind, `FighterData` reads every fighter unit's resource key. That is CharaActionPart +0x3EB8, a shared
    pointer to {+8 the code as an MSVC std::wstring, +40 the version, +44 the alternate set}. Verified: the
    part's secondary-base virtual at rva 0x335420 (this = part + 8) binds the four resources from it, through
    rva 0x2F67B0.
  - The version is resolved as the game's lookup resolves it. The result goes into the match data
    (`core/MatchData`) with the unit's code and side.
  - The start agreement (protocol version 2) carries the match data in P1's summary and in the question for it.
  - Copies whose data differ both end at frame 0 (`StartAgreement::Result::DataDiffers`). Each pushes the notice
    and writes a report of its own class: "SFV game data differs: build ... match ...", with both copies' facts.
    A watcher whose data differs refuses alone.
- **Recorded.**
  - The replay header's players carry `balanceVersion`, the agreed version, and a playback launches exactly it.
    A header without it plays this copy's newest.
  - Every report names the game data.
  - Each unit's whole version list is logged at bind: "fighter data: Z41 (P1) asked balance version 61 of this
    copy's 10, ...". That is what the opponent's copy would have had to send us; every copy now writes it in
    its own log and report.
- **Not done:** the notice is English text pushed with `PushAlert`. Our overlay catalog may only override
  msgids the library has, so a localized "game data differs" needs a library msgid (owed).

## 2. Game data: paks and move data

- **Pak folders (verified):** `FPakPlatformFile::Initialize` (rva 0x234FF50) takes the folder list of rva
  0x234F9D0. That list is `%sPaks/` of three roots: the game's Content dir (rva 0x117F9B0), its Saved dir (rva
  0x117FF70, the user dir + "Saved/") and the engine's Content dir (rva 0x117D440). It mounts every file a
  recursive visit of them finds (rva 0x23514D0).
- **`PakList` (at the mod's start):**
  - Lists every .pak in `<game>\StreetFighterV\Content\Paks`, `<game>\Engine\Content\Paks`,
    `<game>\StreetFighterV\Saved\Paks` and `%LOCALAPPDATA%\StreetFighterV\Saved\Paks`, recursively.
  - Logs each one with its size and verdict, then a summary that every report carries.
  - `PakRules` judges the paks against the stock set: Steam's depot 310951, manifest 4281346506506612445, whose
    Content/Paks are `pakchunk0..68-WindowsNoEditor.pak` with their sizes. The set was read from the install's
    depot manifest and checked equal to the install's files. The two DLC depots in the install hold wallpapers
    only.
  - Named in the summary: anything else, a stock name with another size, a stock name in another folder or a
    subfolder, and a missing chunk.
- **Move data fingerprint (verified):**
  - The resource lookup (rva 0x5518C0) returns the fighter's KWBattleResourceDataAsset for a version. It holds
    two TArrays of resource objects: +56 (count +64), and the alternate set at +72 (count +80), per rva
    0x551480.
  - Each resource is a UKWBattleResource: 56 bytes (rva 0xC04960), whose one property "Data" is a byte array at
    +40, count +48 (rva 0xC72550). KWBAC, KWBCM and KWBCH are its subclasses, and the action part binds that
    array's pointer (rva 0x2F67B0).
  - At bind `GameContent::FighterMoveDataHash` hashes (XXH3) each resource's place, size and bytes of the set
    the unit's key names. It reads under a structured guard, in a few milliseconds a fighter, once.
  - The hash rides in the match data. Two copies at the same version with other move files end at frame 0 with
    "game data differs (fighter X)".

## 3. Script start (the competing hypothesis)

See "The two hypotheses" above. No code changed for it: no state the fight reads on the script-start path was
found outside the snapshot.

## 4. Effect switches (status part, four 72-byte records from +0x340)

- **Verified:**
  - Reset rva 0x351760 numbers the kinds 1 to 4. A script installs a whole record (rva 0x35EBF0) and sets the
    override (rva 0x35EB40) and the level (rva 0x35EB80). The lookup is rva 0x34F750, and its users set +0x2C
    to 2 (rva 0x344F40, 0x347CA0, 0x34F330).
  - The status tick (rva 0x362520) reads the kind (+0x28), state (+0x2C), level (+0x38), shown type (+0x3C)
    and override (+0x40). It turns off the previously shown type when the pick changes, writes +0x3C, and adds
    the pick to the effect list (rva 0x364800).
  - The status update (rva 0x361B00) writes +0x08 and +0x14 for kind 2.
  - The visual effect handle at +0x00 is set by rva 0x30E630.
  - The game registers only +0x2C..+0x33 and +0x44 of each record.
- **Chosen:** save, restore and hash +0x28 and +0x34..+0x43 of every record, as `kPartRunsOutsideSnapshot`
  rows (`kMaxPartRuns` 12). Fight-side functions write those words from script data, and the tick that reads
  them decides what it turns on and off.
- No reader of the +0x44 words was found beyond the reset and the install, so they stay hashed as the game
  registers them.
- +0x00 (an effect handle) is never restored. +0x08..+0x27 (effect parameters copied from script data) are
  left out as before (open, low).

## 5. Round ends

- **The KO stop timer (R4).** Finish (0x2C6A668) binds 18 phases with their waits (rva 0x3EEE00: phases 4, 9,
  12 and 16 wait 1.0 and phases 7 and 10 wait 180 frames, or the battle settings' values). The wait that runs
  is the flow state base's kWait (+112), and the flow record already kept it for every class. Finish's own
  fields were not kept:
  - The record kept only Finish's base, because an "address" was seen at +160.
  - Finish's phase 0 (rva 0x40DDD0) sets +152 (the winning side), +156 and +160 (rva 0x2F9B90's results).
    Phases 4, 6 and 7 read them (rva 0x4080E0, 0x4085A0, 0x4081F0), and phase 14 (rva 0x40EBE0) reads +144
    and +148.
  - +164 is padding the heap left. The "address" was +160 with that padding as its upper half.
  - `ClassSize` now states the bytes each class keeps. Finish keeps +144..+163. 0x2C6AA18 and 0x2C6A3F8 still
    keep only their base (open).
- **The round's end wait (R2).** Unit type 0x3A649B8, vtable 0x2C602F8, 0x140 bytes, constructor rva 0x3C7AE0.
  - The KO arms it (rva 0x3D4550).
  - Finish's phase 12 (rva 0x40BC00) asks it every tick through rva 0x2F2E90. That is every round won by KO,
    not only a stage-transition KO: mode +0x104 at 6 or 7 waits, and otherwise the first ask clears the latch
    +0x100.
  - None of it was in the snapshot. +0xF8..+0x10F (`kowait.state`) and the fighter position at +0x120
    (`kowait.position`) are saved, restored and hashed.
  - +0x110 is not kept: it is a qword that only the arm and the constructor write.
  - +0x118 is never copied as bytes: it is a counted reference set through the game's assign (rva 0x2F02D0).
  - The unit is found at bind; a battle without it logs so.

## 6. Reporting

- **The region exchange (`core/RegionExchange`).** After a fighter's copy finds the mismatch, it lists its
  regions at that checkpoint: per region the stable identity (owner, part and offset, `start_state::
  RegionIdentity`), the peers' hash and the size, about 550 entries. The list is the same one the start
  agreement uses.
  - A thread of its own waits for GGPO to give up the session's port, then trades the list with the other
    copy over the same UDP path. Each side resends until it has the other's list and knows the other has its.
  - The two lists are paired as the start agreement pairs them (`start_state::PairRegions`). A `game.N` region
    pairs by hash or by a size only it has, never by name.
  - The log and the report then list only the regions that differ, with both hashes. Without the other list
    every region is listed after a line that says why: a watcher (its session goes on), the port still in use
    after 3 s (a session kept for watchers), or no answer within 4 s.
- **A report's first lines:** the differing regions, both fighters and their scripts, the game data (each unit's
  code, version and move data), the paks' verdict and the OS build. `WindowsVersion` moved from the launcher's
  courier to `DesyncReport` so both use one.

## 7. The core review's two leads

- **Battle frame counts (S2).** The battle frame's update count (+0x198), ticked-update count (+0x19C) and clock
  (+0x1B4) differ between copies by the updates each ran before binding. They were masked from the peers'
  hash. Readers (IDA, rva 0x4C8000..0x4EA000):
  - The slow-motion gate (rva 0x4D2850, 0x4DC6A0) ticks an update when slow motion (+0x1A0) is 0 or the update
    count is a multiple of it plus one. Slow motion is only ever zeroed or copied (rva 0x4D8830, 0x4E5AC0,
    0x4E9230), so today the count changes nothing.
  - rva 0x4DB680 and 0x4DD4C0 read the clock.
  - +0x19C is only counted.

  A session now sets all three to 0 on every copy at frame 0 (`SfvAdapter::ZeroBattleFrameCounts`, logged)
  and the peers compare them. The input unit's sync clock, a QueryPerformanceCounter reading, stays each
  process's own. Every bound battle logs where its ticks per update change (`TickCounts`).
- **Socket reads (S1).** All 20 references to rva 0x3919D0 are classified:
  - 19 calls and one unwind entry.
  - The 15 sites already in `kNotTheFights`. The review listed 0x39D8BB, 0x404B2D, 0x434FB5, 0x43986C and
    0x439882 as missing; they were already in the list.
  - The four anchored sites.
  - The spawn bone resolver (return 0x4CFC99, rva 0x4CFB70 through 0x32BAA0). Its position goes only to a
    VfxFieldGroupUnit (type 0x3A75050, rva 0x15E500) through rva 0x4D9180, and its manager (VfxManagerUnit,
    rva 0x4D4970) registers no rollback state, so it is drawn only.

  `h_Socket` now follows one policy of the call site. Not the fight's and drawn only: the engine's value.
  The four: their anchors. Any other site: anchored with the head's offset, logged once a battle. The
  record/serve/rebuild cache, the socket grid, the pose holds (core/PoseHold, RollbackSession's chain,
  SyncTest's copy, the GGPO `pose-hold.patch`) and the `-sfve_pose_hold` option are gone.

## Match build

`picks::kMatchBuild` is 0.8. The layout id changed (effect switch rows, Finish, the round's end wait unit, the
battle frame counts, the socket policy version), so `data/pick-space.json` must be exported again from the game
(the pipe's `catalog-export`) in the final round. Its build line was moved to 0.8 by hand with the old layout
id, and until then `RoomBuildTests`' pick space check fails.

## Still open

- Which hypothesis explains 2440 and 8200: the next field match decides it (game data compared at frame 0, the
  region exchange naming the words).
- Motion part +0x34A8 (two characters' unique-part flag, written every tick, read for animation-layer flags);
  the effect records' +0x08..+0x27; the own fields of flow classes 0x2C6AA18 and 0x2C6A3F8; the round's end
  wait's +0x110.
- R6 (the rest of Rose's unique part beyond +0x61C) and D3 (handicap) were not taken up in this branch.
- The "game data differs" notice needs a library msgid to be localized.

## Verify in the final round

The development build of this branch, two copies on the laptop unless said.

1. **Tests.** `sfve_tests` and `sfve_ember_tests` pass (MatchData, StartAgreement, RegionExchange,
   BalanceVersion, PakRules, TickCounts, RoomBuild, DesyncReport, SocketReads). `RoomBuildTests` passes only
   after the re-export in step 9.
2. **Paks.** In each copy's log at start: one `paks:` line per chunk, then `paks: 69 of 69 stock paks` on the
   maintainer's PC and on the laptop. Any other result names the file.
3. **Versions and data, equal copies.** Run
   `tools\runroom.ps1 -p1 Z41:0:0:2:1 -p2 Z25:3:5:1:1 -moves -practice -lag 80 -rounds 3 -games 2` and the same
   with `-p1 Z36:0:0:1:1`. On both copies:
   - `launch: balance versions: P1 v, P2 v` with the same numbers;
   - `versions: Z41 loads balance version v (the record held ...)`;
   - `fighter data: ... of this copy's <list>`;
   - an identical `adapter: the fighters' game data: ...` line;
   - `start: the same state`, or a repair.

   No `game data differs`, and no desync.
4. **Refusal.** A replay whose header's `balanceVersion` is edited to 999 (any of the field replays,
   re-recorded on this build) is refused at Watch now with "has no balance version 999 on this copy". The
   start agreement's refusal has unit tests only. A two-copy check would need a pak difference, which is never
   to be made on the maintainer's install.
5. **Effect switches.** Run
   `tools\synctest.ps1 -p1 VEG:0:0:1:2 -p2 KEN:0:0:1:1 -moves -practice -frames 7000 -check` and the same with
   P1 on Z39. The `learn:` and replay-difference lines name no
   `CharaStatusPart.effectswitch*` region.
6. **Round ends.**
   - A KO-ending recording: two copies with `-sfve_start_ex` and the scripted Critical Arts, as in the beta.9
     skip runs.
   - Play it back with `-sfve_replay_lag=1..7` and `-sfve_replay_jitter=SEED`.
   - Every checkpoint equals the recording.
   - `kowait.state` changes at each KO, and no `this battle has no round's end wait unit` line appears.
7. **Battle frame counts.** In each copy's log: `adapter: the battle frame counted N updates ... before frame 0;
   0 on every copy`, and `ticks:` lines with the same input frames on both copies.
8. **Socket policy.**
   - The 45-fighter sweep (`sfv-sweep.ps1`) with `-moves -practice -check`: no `a call site not classified`
     line.
   - Rooms c, d and e of `D:\sfv-re\b9-pass\s3`: no desync, `anchor unresolved 0`.
9. **Pick space.** Start one copy and run the pipe's `catalog-export` to rewrite `data/pick-space.json`. The
   build line becomes `sfv-ember/13173604/0.8/<fingerprint>/layout-<new id>`. Then rebuild the room host
   (`upgrade.sh rooms`).
10. **Region exchange.** No tool makes a mid-match desync on purpose. In the first natural one (or a two-copy
    run where one copy has `-sfve_start_perturb` on a word the repair leaves, if that ends in a desync), both
    copies' logs show `desync: checkpoint F region R differs, ... here, ... at the other copy`. The reports'
    first line after the title names the same region.
