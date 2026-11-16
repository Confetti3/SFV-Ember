# Friends across SF4 Ember and SFV Ember (beta 10)

The maintainer, 2026-11-16: "the ids use discord so it should work between
games". The design was decided in the beta 10 bridge kit's `CROSS_GAME.md`
(option (a), the kit's `FRIENDS=sf4`): SFV Ember keeps its friends on SF4
Ember's production bridge, `https://bridge.embernetplay.link`, the **account
service**, and everything that belongs to one game on SFV's own bridge,
`https://sfv-bridge.embernetplay.link`. SF4 Ember's released 1.6.0 client and
SF4's bridge binary are not changed. This note says what players get, how the
library routes each request, the sign-in, the one server change on SFV's
bridge, what is left out and why, and what only a run against two bridges can
prove. EMBER_LIBRARY.md section 63 is the short form.

## What players get

In SFV Ember:

- **One friend list with SF4 Ember.** Friends, requests (sent and received),
  blocks, Appear offline and the friend code are one set per Discord account,
  kept on the account service: a request sent in SFV is answered in SF4 and
  back, a block holds in both games, and Appear offline hides the player in
  both.
- **"Playing USF4" / "In a room · USF4"** for a friend in SF4 Ember (the
  library's `GameTitle("usf4")`), with no Join, Watch or Invite: another
  game's room is not joined from here.
- **A friend's private SFV room**: Join from the list asks the account service
  for the room's invitation (same game and build, a seat free), which goes the
  way a Discord invitation does. Unchanged from the library.
- **A friend's public SFV room**: shown as "In a room", with Join and Watch off
  and the words "That room cannot be joined now." (`friends.join_unavailable`,
  `Refusal::RoomElsewhere`). The account service lists only its own game's
  public rooms (see "Left out"). **An invite** from that friend to their
  public room does lead in: the invite carries the room's id, and the visit
  takes its ticket from SFV's bridge.
- **Add friend from a room member's card, the post-set prompt or a profile**:
  sent to the account service by the friend code the member's card on SFV's
  bridge shows (see "The friend code on a card"). A member whose card shows
  none (friends off, Appear offline, an older build) gets "This player does not
  share a friend code here. Ask them for it, then add them by friend code."
- **Find a player**: a name is searched on SFV's bridge (SFV players met in
  rooms); a friend code on the account service, whose hit's card opens there
  and can be added or blocked there directly.
- **Cards**: a friend's card (the Friends tab's focused friend, a request, a
  code) comes from the account service: SF4's points, class and awards, the
  name and flag last synced. A room member's card, the player's own card,
  points, awards and class come from SFV's bridge.
- **Block a room member**: not from the member's card (SF4's bridge cannot
  open SFV's card token, and its Block takes only a token): "A player met here
  is blocked from their friend request or your friend list." A player found by
  code is blocked from their card.

What an SF4 Ember 1.6.0 player sees of an SFV friend (nothing changed on their
side): "Playing SFV" / "In a room · SFV" with no Join, Watch or Invite; their
card is SF4's `player_cards` row (SF4 class and points, the name and flag the
SFV client last synced), and the main empty while the friend plays SFV (SFV
sends none; the next SF4 sync puts it back).

## How a request finds its bridge

`netplay/AccountRoutes.hxx` is the one table (`ServiceOf(Ask)`):

| Account service | SFV's bridge (the game's) |
|---|---|
| friends sync, every friends action (join included) | room tickets, a friend's visit included |
| a card by friend id, request id or friend code | a card by a room member's token, a recent opponent's, a rival's |
| Find a player by code | Find a player by name, a name's profile |
| | the player's own card save, own points |

A card token opens only on the bridge that sealed it, so a profile's key
carries the service its token came from (`profiles::Key::from`): a code
search's hit is the account service's, everything else the game's. The runtime
(`RuntimeBridges`) resolves the table to bridge ids: the game's is the one
levels are read from; the account service's is the one the interface names
(`commands::AccountService`) once the player trusts it. Friends' cards go in a
look-up of their own there (`runtime->friendCards`) and are merged with the
room's for the view (`ShownCards`). A product whose game bridge is its account
bridge (SF4 Ember: `ProductIdentity::accountService` null) resolves every kind
to the one bridge, and its friends' cards ride with the room's in one look-up,
byte for byte as before.

The friends sync names `game: "sfv"`, the build id and what the player is
doing, and **no main**: a fighter index means a fighter of the bridge's game,
and SF4's bridge refuses 44 and would read any SFV index as a USF4 fighter
(`friends::ListingFor`).

## Configuration

`ProductIdentity::accountService`, filled by SFV's `sfve_product` from the
library's x64 build: `cmake/ember-x64-probe` `EMBER_ACCOUNT_SERVICE` (default
`https://bridge.embernetplay.link`), handed up as `EMBER_ACCOUNT_ORIGIN`. A
test build points it at a staging or local bridge with
`-DEMBER_ACCOUNT_SERVICE=<origin>`; an empty value, or SFV's own bridge again,
keeps friends on SFV's bridge (the kit's `FRIENDS=sfv`).

## Sign-in, step by step

A session and a Discord link exist per bridge. The helper keeps a session
token per bridge and renews only the one a bridge refuses
(`client::with_session`), so the two never disturb each other.

1. **Trust.** The public rooms setup trusts SFV's bridge as before. Once it is
   trusted, by the setup, Connect Discord or a player from beta 9, the
   interface looks the account service up and approves it in the background
   (`IdentityPanel::TrustAccountService`), unless the player removed it; no
   second prompt, and nothing about it delays a room. The helper's trusted
   list is shared with SF4 Ember, so a player of both usually trusts it
   already. A failure leaves friends unavailable and is tried again when the
   services are read next.
2. **Connect Discord** (one row: Settings > Discord, the Ember ID screen's
   Discord row, the Friends tab's row): the Connect screen shows a line for
   **Rooms** (SFV's bridge) and **Friends** (the account service), each
   connected by name, not connected, or blank while unread. One press runs the
   steps for the first that is not trusted or not connected
   (`NextToConnect`): SFV's bridge first. When Discord connects there, the
   status line says "Discord connected for rooms. Discord opens once more for
   friends: approve it there too.", and Discord's page opens again for the
   account service. A service already connected is only read and passed over.
3. **Two round trips.** Each bridge keeps its own Discord link (its own
   redirect and its own `discord_accounts` row), and both ask Discord with
   `prompt=consent`, so Discord shows its page once for each. One round trip
   for both would need SF4's bridge to accept another bridge's sign-in, which
   is a change to SF4's bridge, out of scope.
4. **Honest state.** Settings' Discord account row and the Ember ID screen's
   Discord row say "Friends keep their own Discord link on Ember's account
   service, and it is not connected there yet. Press to connect it." while SFV
   is connected and the account service is not known connected. The Friends
   tab says "Friends are kept on Ember's account service, shared with SF4
   Ember ..." with Connect Discord while the account service is not trusted.

A player of beta 8 and 9 who connected Discord only on SFV's bridge connects it
once more, for friends (same Discord account, same ID). A player of SF4 Ember
usually has it there already, and their SF4 friends appear at once.

## The friend code on a card

The main way players add each other is a room member's card or the post-set
prompt, and both hold SFV's card token, which SF4's bridge cannot open. So:

- The SFV client puts its own friend code (the account service's
  `FriendsState.code`) on its card on SFV's bridge (`Bundle::friendCode`,
  `PUT /v1/cards/me` `friend_code`), **only while its friends are on and it
  does not appear offline** (`profiles::SharedCode`); a code that changes
  (New code, friends turned off) is saved again, and a save without one
  clears it.
- SFV's bridge (the library bridge, migration 068 `player_cards.friend_code`,
  capability `card_friend_code`) shows it on that card to another player who
  looks it up by a room token (`Purpose::Card` only: never on search hits,
  rivals or a friends state). The capability and the column are inert for a
  bridge whose game is usf4 or whose friends are on, so SF4's production
  binary, which is not rebuilt, never has them, and no wire shape SF4's
  released client depends on changes.
- The helper sends `friend_code` only to a bridge that lists the capability,
  and drops it for any other.
- The other SFV client sends the request by that code to the account service.
  The account service decides as it does for every request: a player who
  blocked the sender, or turned friends off, never sees it (the sender's
  request still looks sent), and a request that crosses one of theirs makes
  them friends.

The card's own player is created on SFV's bridge without a Profile save: the
runtime saves the card at start once a Discord account is connected
(`TickCardSave`, the same as before; the friends sync, which wrote cards on a
bridge with friends, now goes to the account service). A player who connects
Discord during a session gets theirs within the save's wait (at most ten
minutes after a `no_discord_account` refusal) or at the next start.

## Failure behaviour

- **Account service down**: the Friends tab says "This service does not offer
  friends yet." / unreachable as before; rooms, ranking, points and cards in
  rooms go on. Friends' requests go through the helper's flights like every
  bridge read; nothing in a room join or a match start waits for them.
- **SFV's bridge down**: SFV rooms are down; friends keep syncing (presence
  "online"), and friends' cards still come from the account service.
- **Account service not trusted**: friends unavailable with the Friends tab's
  Connect Discord row; no request goes to it.

The game process reaches only loopback and the session's peers (the mod's
network guard, `core/NetPolicy`): every bridge request is the helper's, a
separate process, and Discord's page opens in the player's browser. Nothing in
the game process needs the account service's host.

## Left out, and why

- **Join or Watch a friend's public SFV room from the list.** The presence the
  account service keeps of a room is its id, and SF4's bridge shows a room only
  when it is in its own `rooms` table (`store::listed_rooms`). The only other
  members that reach a friend are a private room's offer (an `emd1:`/`emd2:`
  invitation and its facts), and dressing a public room up as a private offer
  would tell every friend a public room is private and put a non-invitation
  where a room host reads invitations. Needs SF4's bridge to show another
  game's public room by id. An invite still works.
- **Block a room member from their card.** Needs SF4's bridge to block by
  friend code, or to open SFV's card token.
- **A member's relation on their card** (Friend, Sent, Received, Blocked):
  SFV's bridge has no friends, so a member's card says none; Add friend to an
  existing friend or a pending request changes nothing on the account service
  (it answers as for any request).
- **SFV's main, points, awards and class on the card SF4 friends see**: SF4's
  card shows SF4's. Needs per-game cards on SF4's bridge.
- **The sender's Sent list names the code**, not the member's room name: a
  request by code is labelled with the code by the account service.

## Tests

- Library `AccountRoutes` (`src/tests/account_routes_test.cxx`): every kind's
  service; two bridges split them, an untrusted account service sends none of
  its kinds; one bridge takes everything (SF4); profile and search asks by key
  and kind, a token's service in its key; Connect Discord's order with
  neither, one or both connected; no main on the account wire; the friend code
  on a card under each privacy switch and in the save; a public room the
  account service cannot list.
- Library `LevelsAnswers`: a card's friend code read, and a malformed one read
  as none.
- Helper (`rust/sf4-net`): two bridges keep separate sessions, and renewing one
  leaves the other's; the card save's code forwarded only with
  `card_friend_code`, refused when malformed; look-up answers pass the code.
- Bridge (`server/ember`): `card_friend_code.rs` (shown to another player by
  token, cleared by a save without it, never on search hits; refused and never
  shown with friends on or for usf4), migration 068, the reader's purposes.
- SFV `LocaleOverlayTests`: the two overlay entries may name SF4 Ember.

SF4's product tests are unchanged: every route resolves to its one bridge.

## What only a run against two bridges can prove

The `--friends` plan for the local stack (on paper; not run):

- `tools/localstack.ps1` gains a second scratch bridge, `New-Bridge` on
  `127.0.0.1:$($port + 1)` with `rooms.friends = true` and `rooms.game =
  "usf4"` (the account service), beside the first with `rooms.friends = false`
  and `rooms.game = "sfv"`. Both copies trust both (selftest Ember folders),
  and both bridges get the same Discord user IDs straight in their databases,
  as `-ranked` does for one.
- `tools/runpublic.ps1 -friends` builds the copies with
  `-DEMBER_ACCOUNT_SERVICE=http://127.0.0.1:<port+1>` and checks, in order:
  1. each copy's friends sync reaches the second bridge (`Friends:` log lines,
     the bridge's `/v1/friends/sync` count) and none reaches the first (404);
  2. copy 0's card on the first bridge carries its friend code
     (`player_cards.friend_code`) once its friends are listed, and loses it
     after Appear offline on;
  3. copy 1 opens copy 0's member card in the public room and presses Add
     friend: a request by code lands on the second bridge; copy 0 accepts;
     both lists show the other "In a room" with Join off
     (`friends.join_unavailable`);
  4. copy 0 invites copy 1 to the public room: copy 1 takes the invite, its
     ticket comes from the first bridge, and it joins;
  5. the second bridge stopped: the room plays its games to PASS and the
     Friends tab says unavailable; started again, friends come back;
  6. the first bridge stopped with both in the menus: friends keep syncing.
- In the game, by hand: Connect Discord's two consent pages in one press, and
  an SF4 Ember 1.6.0 client (against the second bridge) seeing "Playing SFV".
