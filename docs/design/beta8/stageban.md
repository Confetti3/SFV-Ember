# beta.8: banned stages (2026-11-12)

**The ask.** "A stage filter so people can ban stages that are not competitive. If player 1 selects
a banned stage it should either pick a random stage if they are set to it or the training stage."

## What it does

A room rule **Banned stages**: a set of stages, host-only to change, empty by default (nothing
banned). Room settings has a "Banned stages" row (value: "None" or "N banned") that opens a page
listing every stage the game offers (the catalog's names, in the universe's order) as Banned or
Allowed, and a "Clear all" row. Left/Right or Select on a stage turns it; only the host of a private
room can (the rows are read-only for everyone else, who can open the page and see). A public
(server-owned) room refuses the change and keeps the set empty. The Training stage (`TRN`) is not
listed: it is what stands in for a banned one.

**Resolution (P1's Ready, once).** The stage P1 sends decides the game on every copy (the match
data carries it; both copies launch what it names), so the rule is applied where P1 builds that
message, not separately on each copy:

| P1's stored stage | sent |
|---|---|
| Random, some stage left | Random rolled with the existing deterministic roll, over the stages this copy offers that are not unavailable (the opponent's lack) and not banned |
| Random, none left | the Training stage |
| a specific stage, not banned | that stage |
| a specific stage, banned | the Training stage |
| every stage banned | the Training stage (Random or a pick alike) |

A stage the opponent lacks keeps its handling: the Ready check refuses a sent stage either copy
lacks (`runtime.ready.stage_not_shared` / `stage_unavailable`), so a Training stage the opponent
lacks is refused the same way. A ban beats the opponent's lack for the same stage (it becomes
Training either way). A room that bans a stage after P1 readied does not change the game already
set up; the next Ready applies it. Because the decision is made once from P1's snapshot and sent,
the two copies cannot disagree, whatever the timing of a ban.

The log says, once, in P1's log: `Room: stage: P1 chose <id or Random>, banned by the room: sending
stage <id> (<n> banned)`.

**Pick screen.** On the stage cards page a banned stage is drawn dimmed with its name tagged
"(Banned)", cannot be chosen (Select says "This stage is banned in this room. Pick another stage or
Random." in the pane), and Random stays choosable. The saved stage stays as P1 stored it even when
the room bans it afterwards: the pane says it is banned, and Ready sends the Training stage. P2 and
watchers still see "only P1 chooses".

## Where it lives

Library (`b8/lib-stageban`, off `b8/lib-integrate`):

- `room::Snapshot::bannedStages` (JSON `banned_stages`, written only when non-empty, so every other
  room's state is what it was; a malformed value bans nothing). `BannedStagesFrom` keeps valid
  stages of the game's catalog, ascending, no repeats, at most 64.
- `ActionKind::SetBannedStages` (appended last, `LastActionKind` follows it) with
  `Action::bannedStages`, the whole new set. Host only, fenced on the room revision, gates no seat,
  moves nobody. A server-owned room refuses it. `RoomAuthority`, `SessionServer::EnableCustomRooms`,
  `RuntimeCommand::bannedStages` and `NetplayRuntime::bannedStages` carry it so a replacement room
  keeps what its host set; a new room opens empty.
- `game::Capabilities::bannedStages` (default false; SFV sets it) and, on `GameState`,
  `BannableStages()` (id and name per stage) and `TrainingStage()` (-1 for none). The shell shows
  the row and page only where the capability is on (`ShellView::offersBannedStages`,
  `bannableStages`), so SF4 shows nothing and reads nothing (`StageToSend` ignores the snapshot
  without the capability).
- `common/StageBans.hxx`, `StageUnderBans`: the one pure decision above; `NetplayRuntime__Stages.cxx`
  calls it from `StageToSend` and `StageReadyRefusal`.
- Strings `room.banned_stages` (row and page title), `.detail` (the hint), `.count`, `.tag`,
  `.allowed`, `.clear`, and `selection.stage_banned`, in all 14 catalogs (the CJK text checked
  against the embedded subset fonts' character maps; no subset regeneration needed).
  `selection.stage_banned` is drawn only by SFV's pick screen, so the localization source check
  lists it with the Size row's texts.
- Test `RoomBannedStages`.

SFV (`b8/stageban`, off `b8/integrate`):

- `SfvCapabilities` sets `caps.bannedStages`; `SfvGameState` answers `BannableStages()` (every
  stage of the pick space but `TRN`, named as the shell names them) and `TrainingStage()` (the id of
  `TRN`).
- `StageOffer::Banned` and `StageOptions(unavailable, banned)`: an offered stage the room bans is
  Banned (before the opponent's lack); Random is never one. The pick screen (`SfvSelectionScreen`)
  tags, dims and refuses it (`StageAnswer::Banned`); `SfvShell` passes the room's set.
- The pipe's `room` command takes a last argument, the banned stage codes (`CHN,S25`, `-` for none),
  and `tools\runroom.ps1 -banStages CHN,S25` passes it, then checks the launched stages.
- Tests: `SfvPickRulesTests` (Random never rolls a banned stage; a banned pick and no stage left
  are the Training stage; determinism; the opponent's lack together with a ban; the stage row's
  Banned entries), `SfvSelectionScreenTests` (the card's tag, dimming and refusal; Random and
  another stage still chosen; P2 and a watcher unchanged).

No wire format changes beyond the room JSON's new key and action kind (an older copy ignores the
key and rejects the action as an unknown kind); `kMatchBuild` stays 0.6 because the stage travels
exactly as before, in the match data. The room host needs nothing: public rooms are empty.

## Not built or run here

No builds on this PC (the laptop was busy). The code has not been compiled.

## What a laptop room run must check

1. `runroom -games 2 -lag 30 -banStages CHN,S25 -stage S25`: both games launch `TRN` on both
   copies (the end-of-run line "launched stages: copy 0 [TRN TRN], copy 1 [TRN TRN]"), 0 differing
   checkpoints.
2. `-banStages CHN,S25 -stage RND -games 4`: Random lands only on other stages, the same stage on
   both copies each game, never CHN or S25; log lines `stage: P1 chose Random, banned by the room`
   appear only when the roll would otherwise have landed there.
3. `-banStages CHN,S25 -stage S01` (a stage allowed): launches S01, untouched. No `-banStages`:
   nothing changes.
4. `-uninstalled1 CHN -banStages S25 -stage RND`: the opponent's lack and the ban together leave
   other stages; `-uninstalled1 TRN -banStages S25 -stage S25`: P1's Ready is refused with the
   opponent-lacks reason, not launched.
5. By hand: Room settings (host) shows "Banned stages", the page toggles stages, "Clear all"
   works, the guest sees the page read-only, a public room's row is "None" and read-only; the
   P1 pick screen's stage cards show a banned stage dimmed and tagged, Select on it shows the
   reason, Random still chooses; a ban set after P1 picked a stage still readies as Training.
6. Every language shows the strings (CJK especially), no clipped captions with the "(Banned)" tag
   on the cards.
7. SF4 Ember (the library's own build, run separately): no "Banned stages" row, goldens unchanged.
