# Watching with Ember's room open: the watched game's picture on SFV

The maintainer, 2026-11-18: port SF4 Ember's picture-in-picture and room use while watching to SFV. A member who
watches a table's game can open Ember over it and use the room (board, chat, members, the queue) while the
game plays on, and sees the game as a picture in Ember's menu: Small in a corner, Big in the room board's
space, or Off, by the same preference SF4 Ember keeps (`PlayerPreferences::pictureMode`, `pictureSpot`).

Library branch `s011/lib-field-pip` (from `s011/lib` 505131a63), SFV branch `s011/field-pip` (from
`s011/release` 4ff0db7).

## What SF4 Ember does

- **The room while watching.** A watched room game is `input::MenuContext::WatchedMatch`. Start or F10 opens
  Ember's menu over it (`ControllerOpensMenu`, `MenuOverFight`) and the shell lands on the board at Stop
  watching (`ApplicationShell`, "Opened over a fight"). Everything on the board works there: chat, members,
  another table's seat or the queue (which says it ends the view, `room.watching_ends`), the table's Unwatch
  and Stop watching (`LeaveWatchedGame`). Ready needs the main menu, so it waits for the watched game's end
  in SF4 too.
- **The picture.** While a game is watched and the picture is not Off, SF4's overlay copies the game's render
  target before Ember draws anything in the frame (`sf4e__WatchedPicture.cxx`: a `StretchRect` of the target
  at its own size, which also resolves a multisampled one, then a linear `StretchRect` into an `X8R8G8B8`
  texture at 62% of the screen, `ui::WatchedPictureTaken`), so the picture is the game alone and samples
  opaque whatever alpha the game left. The texture is made once and let go with the overlay. The shell
  draws it (`ShellView::watchedPicture`): Small in a corner window the mouse moves and resizes
  (`ui/WatchedPicture.cxx`), or at the column's top (`RoomPanelTables`); Big in the tables' place with Hide
  picture on it. With a picture the menu is opaque; with none (Off, or a copy that failed) the menu is
  lighter and the game shows through it (`ShellView::SeeThrough`).
- **The overlay's other watched-game handling.** For its first 10 s a watched game says how to leave it
  (`runtime.watching_hint`, "Start or F10 opens Ember, where Stop watching returns you to the room."), and
  Alt+Enter is kept from the game while a stream is watched (`ui::KeepsAltEnter`): the switch to and from
  exclusive full screen stalls the game past the backlog at which P1 drops a watcher.
- **Tournament watching** (`IdentityPanel__Watch`) joins the match's room and watches its table: the same
  watched game, the same picture. `WatchQueue` is the Replays screen's queue of a set's games, not the
  room's; it has nothing to do with a live watched game.

## What moves where

**Library (game-neutral).** Everything above, except taking the picture, already was the library's, and SFV
already runs it (`Capabilities::spectators`, EMBER_LIBRARY.md section 25): the room logic, the shell's
landing and rows, the picture's drawing, the Settings and More... rows, the save. What was SF4's was the rule
for when to take the picture, inside SF4's `ShellGame::FillView`. It is now the library's:

- `ui::WatchedPictureToTake(watching, pictureMode, screen)`: a size while a game is watched and the picture
  is not Off (62% of the screen), none otherwise.
- `ShellGame::WatchedPicture(width, height)`: the game takes the picture, before Ember draws anything in the
  frame. Empty by default, so a game without one keeps the see-through menu. `ShellHost::Draw` asks it.

SF4 answers it with its Direct3D 9 copy, unchanged. No capability flag: the default answer is the
see-through menu, which is right for a game that takes no picture.

**SFV adapter.** `SfvShellGame::WatchedPicture` answers with the overlay's Direct3D 11 copy
(`overlay::WatchedPicture`, below). The HUD says the watching hint (`DrawSfvMatchHud`, the shared text, as
the Practice match's end hint is said there), and `SfvShell`'s window hook keeps Alt+Enter while a stream is
watched (`ui::KeepsAltEnter`).

## The Direct3D 11 copy

SFV draws Ember in its Present hook (`overlay/Overlay.cxx`): ImGui's frame is built inside `OverlayUi::draw`
and rendered into the back buffer after it, just before the real Present. The picture is taken while the
shell's frame is built, so every GPU command it records precedes Ember's draw in the immediate context: the
copy holds the game's frame alone, SFV's own HUD included, as SF4's does.

`overlay/WatchedFrame` per frame that asks:

1. The swap chain's back buffer is copied (`CopyResource`) into the frame's copy, a texture of the back
   buffer's size and format that can be sampled; a multisampled one is resolved into it
   (`ResolveSubresource`). The back buffer itself may not be sampled (its usage is the game's).
2. The copy is drawn, linearly filtered and with blending off, into the picture: a `B8G8R8X8_UNORM` render
   target at the size asked. The X8 format makes it sample opaque whatever alpha the game left in its back
   buffer, as SF4's `X8R8G8B8` does. The draw is ImGui's own renderer (`ImGui_ImplDX11_RenderDrawData`) on a
   one-image draw list whose callback turns blending off: it saves and restores the engine's pipeline state
   as it does for Ember's frame, and the overlay's `Render` keeps the render targets and the shaders ImGui
   does not save. No shader of our own is compiled.
3. The picture's view is the `ImTextureID` the shell draws.

The colour is unchanged by the trip: the copy's view has the back buffer's own format (an sRGB one decodes
and the picture's draw re-encodes it as Ember's frame is encoded), the picture is UNORM like every texture
Ember draws.

**Cost.** One full-frame copy (about 8 MB at 1080p, GPU to GPU) and one draw of about 1190x670 pixels per
frame, only in a frame where Ember's menu is shown over a watched game with the picture on. Hidden menu,
picture Off, no watched game: nothing is copied. Both textures are made at the first size seen and again
only when the back buffer's size or format, or the size asked, changes; they go with the overlay
(`Detach`). A texture the device refuses is logged once and not tried again until the overlay is made again;
the menu then shows the game through itself, as with Off.

**The photo booth** (development, `-sfve_mode=booth`) has no overlay interface, so nothing asks for the
picture; its grab runs at the Present tap, before the overlay, and the picture only reads the back buffer.
**OBS** captures at Present after the overlay (the vtable slot leads to Ember first), so a stream shows
Ember's menu with the picture in it, as SF4's.

## Input while watching

Unchanged from section 25, and the same as SF4's: with Ember's menu shown the keyboard and the mouse are its
(the overlay's capture) and the pads too (`MenuPads::Holds`, the game sees idle pads). The watched battle
does not read the pads at all: it plays the fighters' streamed inputs, so it runs on behind the menu, also
behind other windows (`BackgroundPlay`). Start or F10 closes the menu again over the fight.

## At the match's end

The watched game is active while its battle is live (`WatchedGameNow`). A set going on in place stays the same
watched game, so the picture goes on over the rematch. When the battle closes (the fighters went back to the
room, or the stream failed or was left), the shell stops asking: the picture goes, the menu is the room's
usual opaque menu (SFV's backdrop allowed again at the main menu), and a hidden menu reopens on the room as
after any game (`OverlayPresentation`, "a match ended"). The textures stay for the next watched game.

## Tests

- Library: `WatchedMatchExit` (`PictureIsAskedWhileWatched`, `PictureOverTheWatchedBoard`: the menu is opaque
  exactly with a picture, which is drawn once, on the board or in its corner, and Ember still lands on Stop
  watching), `RuntimeCapabilities` (a game without a picture gives none).
- SFV: `WatchedFrameTests` (the copy's plan: when the textures are made again, copy or resolve, nothing for
  an empty size; no picture without the overlay's device).

## Needs a hands-on check on the laptop (Proton)

Two copies in a room plus a third watching (`tools/runroom.ps1 -watchers 1 -keep`), on the watcher:

1. Start over the watched fight: Ember opens on Stop watching, the menu is opaque, the picture is in the top
   right corner and moves with the fight (not a frozen or black picture: DXVK's copy and the X8 target).
2. More... > Picture: Big shows it in the tables' place with Hide picture; Off makes the menu see-through.
   Settings > Matches' corner and size rows move it. The mouse drags it to another corner and resizes it.
3. The picture's colours equal the game's (no washed-out or dark picture), at 1080p and after a window
   resize (the textures are made again).
4. Chat, a member's card and the queue of another table work over the fight; the fighters' copies log no
   watcher drop while the menu is open.
5. The watching hint shows for 10 s as the watched fight starts; Alt+Enter does nothing while watching.
6. At the game's end the picture goes and the room's menu is back; a rematch in place keeps it.
7. OBS game capture of the watcher shows the menu with the picture.
8. The frame time with the picture on against off (the overlay's log, or the game's own counter): the copy
   should not be visible.
