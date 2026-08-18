
# Bugs

- In the map editor selection screen, scrolling through the maps, a preview is displayed in the right column. Moving to a maps that is smaller than the previous one doesn't redraw the entire map preview area, so part of the previous larger map still shows in the area outside the new smaller map.

- In the map editor main screen, there are a redraw issues on the top and right sides outside the main map. Hovering the mouse over these areas leaves ghost mouse cursors artifacts sometimes. Hovering over the main map near the right side, the mouse stats text such as "room 1,0  (28x262)" leaves artifacts and ghost text on the minimap and the area below it.

- Map version number needs to be changed in the bug report policy screen, where it remains unchanged.


## Map selection tweaks

- In the map editor selection screen, show the map name and details below the map preview. Make the text label green and the value white. Map name doesn't need a label.


# Features

## Menu navigation
Change the menu navigation steps for the map editor. Currently we press 7 from the main menu and go into the map selection screen, but let's change this. Instead, go from the main menu directly into the editor and a new map. The level chooser screen will be moved into the Map > Open screen which hasn't been created yet.

## Map editor tweaks
Some of these may already exist in later phases, but these items can be addressed in whichever phase they make the most sense.

- I changed my mind on the wraparound visual. Do not use wraparound to show the SAME rows and columns at the same time on both ends. It's too distracting and confusing. Only show each room of the map once.
- DO keep the separate map movement wrapping feature, so when we move up/down/left/right the map movement continues to wraps around.

- ~~Red, white and yellow grid lines are too bold and visible. Change their alpha so they are transparent and less visible at 40% opacity.~~

- ~~Keep the map "boundary" outline color at 80% opacity so we can see where the map edges are all all times. This will be helpful when we move the map around and the map edges are in the center of the screen somewhere.~~

- The arrow keys can already move the map in any direction. For the mouse, add new arrow buttons to the top, bottom, left, right and corners outside the map boundary that allow us to move the map one room in that direction. These arrow buttons should span the entire width or height of the map size. The corner buttons will move the map one room in both directions. The buttons should be 50% opacity when inactive, and highlight to 100% on mouseover.

- Add the ability to click on the minimap and move to that area of the map

- In the minimap, highlight which area of the map we are zoomed to.

- Add a "Zoom width" and "Zoom height" values which determine how many rooms are shown on the screen at a time. If the user wants to see a 3x2 layout, or a 2x3 layout, or a 5x1 layout, etc, they can change these values to whatever they want (limited by map size).
- Add "Zoom in" and "Zoom out" settings which work in tandem with the Zoom width/height. These Zoom settings either halve or double the Zoom width/height setting to determine how many map rooms are shown on the screen at a time. To handle odd numbers, we would halve to the ceiling. For example, if the Zoom width/height is 5/4, zooming in would reduce to 3/2 since 5 is odd and 2.5 would ceiling to 3. Once one of the height/width values reaches 1, that value never goes any lower. Continuing to zoom in will reduce both values until they reach 1. So 5/4 would zoom in to 3/2, which would zoom in 2/1, which would zoom in to 1/1 and then we can't zoom any further. Zooming out simply doubles each value until we hit the map size limits and can't zoom any further.
-  The Zoom in/out and Zoom width/height settings would work in tandem and update each other as one or the other is changed.

- We need menus and tool buttons/boxes/inputs across the top
  - Editor menu item, with the following items:
    - Settings
    - Close

  - Map menu item, with the following items:
    - New
    - Open
    - Settings: edit map name, author, any other editable details. Show map path/location in the system. Include an "Open map location" button which opens system file manager
    - Validate: run validation on the map structure, show validation output, errors, etc
    - Save
    - Save as
    - Close

  - Tool menu item, with all tools listed underneath it, including their keyboard shortcuts

  - Zoom in/out buttons
  - Zoom width/height input boxes
  - Select tool
  - Spawn point tool
  - Flag tool
  - Wall tool
  - Circle tool
  - Texture drop-down selector
  - Other tools I'm forgetting
  - At the end a ? button which opens a help dialog with usage instructions, keyboard shortcuts, tool descriptions, anything that might go into a help section

  - Ideally, we can have all menu items in the top first row
  - Tool buttons/boxes/inputs will be underneath the menus in row 2
  - We also need a designated "Tool settings" area that shows additional settings related to each tool that is selected. Put this settings area in row 3.

## Settings dialog items

- Add a "Snap to grid" option, on by default

- Add a "Map boundary grid" line setting (currently red), with 3 options:
  - 1: on/off toggle
  - 2: opacity setting
  - 3: color selector

- Add a "General grid" line setting (currently white), with 3 options:
  - 1: on/off toggle
  - 2: opacity setting
  - 3: color selector

- Add a "Center grid" line setting (currently yellow), with 3 options:
  - 1: on/off toggle
  - 2: opacity setting
  - 3: color selector

- Add a "Room boundary grid" line setting (currently red), with 3 options:
  - 1: on/off toggle
  - 2: opacity setting
  - 3: color selector



## Add an in-game map editor to create new and edit existing maps
- New main menu item: 7. Map editor
- Bump current Help and Exit menu items down to 8 and 9
- Map editor will be a mouse and keyboard driven GUI
- Mouse driven for creating shapes, placing spawn points, flags, etc
- Keyboard shortcuts to change settings, choose tools, place items, etc
- Looking for a good keyboard/mouse blend that is intuitive, makes sense, and is easy to use
- Help section with instructions, kb shortcuts, etc
- Texture theme menu so we can preview the map using locally available themes
- Some kind of map validator to check for minimum map requirements, structural integrity, etc. This could be a manual or automated process (or both), whatever makes the most sense.
- See doc folder html pages for mapping tutorials and map specifications
- Ability to launch the map right from the editor for testing. Have a few pre-launch settings such as bot number and skill, maybe some other basic settings that make sense?
- Persistent map editor state, so when we come back to the editor it remains as we left it. This only applies to when we're running the game. If we fully quit out of the game, the map editor is back to a clean slate
- Status: large feature, being built in phases. Phase 1 (foundation: internal document model + map-text serializer + automated round-trip tests), Phase 2 (menu entry + read-only viewer: pick an existing map, pan/zoom it with the real minimap/HUD-consistent rendering, mouse coordinate readout), Phase 2.5 (two-column picker screen: live search filter with default focus, grouped map list, minimap preview + stats panel), and Phase 3 ("New map" button + core mutation: draw/move/resize/delete rectangular walls and ground areas, place/remove flags and spawn points with team cycling, save to disk, and a free structural validator) are done -- see "Done" below. Remaining: full geometry (triangles/circles) + texture theme preview, help screen, then test-launch.
- Decided: editor-saved maps go in the existing cmaps/ directory; "bot skill" in test-launch just means bot_ping, no separate concept needed
- Still open: how should "launch this map for testing" actually start a server on the exact map being edited -- new Server/ServerExternalSettings plumbing (forced start map + bot count/ping, bypassing the normal maps/ directory scan), or scripting the existing vote+/forcemap admin flow instead?


## Server Settings GUI

- Inside the 4 Local server menu, add a new 5 Server settings menu item
- Bump Start server, Play on the server, and Stop server down one to 6, 7, and 8
- New Server settings menu will provide a GUI to manage all the server settings found in the config/gamemod.txt file
- There are a lot of server settings, so grouping them into separate sub-menus would be helpful. The gamemod.txt file already has groupings that could be used as a guideline
- Most settings will be either input boxes, checkboxes, or sliders
- In map settings, it would be nice to show a preview of the map if it is known. If we don't know which map will be used (for example if map rotation is random), show some kind of generic preview instead



# Questions

- How are server generated maps created?


# Packaging

- Add AppImage release to GH


# Done

- ~~Map editor: make the map boundary outline thicker (2px instead of 1px)~~:
  follow-up to the opacity change below -- the 80%-opacity top/left edge
  lines in `Graphics::drawRoomBackground()` (`roomy == 0`/`roomx == 0`)
  now draw a second `hline`/`vline` one pixel inward, so the true map edge
  is 2px thick. The 40%-opacity internal room-to-room seams and the fine
  grid/center-cross lines are unchanged (still 1px), keeping the visual
  distinction between "true map edge" and "internal seam" from the
  opacity change intact and reinforcing it. Verified by pixel sampling:
  the top/left edge is now two consecutive pixels of R=204, while an
  internal seam remains a single pixel of R=112. Full clean rebuild and
  `mapeditor_roundtrip` (47/47) re-verified.

- ~~Map editor: dim the map-info grid lines to 40% opacity, keep the map
  boundary at 80%~~: the fine white grid, the yellow center-cross lines,
  and the red room-boundary lines drawn by `Graphics::drawRoomBackground()`
  when `mapInfoMode` is on (shared by both the map editor viewer and the
  in-game "show map info" player toggle -- one code path, no separate
  editor-only rendering to touch) were all previously drawn fully opaque,
  overpowering the actual wall/ground texture underneath. Wrapped the
  fine-grid/center-cross `hline`/`vline` calls in `set_trans_mode(102)`
  (255 * .40, matching the existing `set_trans_mode(120)` precedent used
  a few lines up for the respawn-area overlay) and restored `solid_mode()`
  afterward so the change doesn't leak into any later drawing.

  The room-boundary lines needed a second, brighter tier: each room only
  ever draws its own top+left edge (the bottom/right edges of the map's
  last row/column are covered "for free" by the next room wrapping around
  and drawing its own top/left edge there, the same wraparound trick
  `repeatMapX`/`repeatMapY` panning already relies on elsewhere) -- so a
  room at `roomy == 0` or `roomx == 0` is drawing the *actual* edge of the
  map data, not just an internal room-to-room seam. Kept those at 80%
  (`set_trans_mode(204)`, i.e. 255 * .80) while internal seams stay at the
  same 40% as the rest of the grid, so the map's true boundary stays
  visible even after panning it away from the screen's edge.

  Verified visually (2x2 "2 versus 2 (duel)" map, showing both an internal
  seam and all four true map edges at once) and, more rigorously, by
  sampling rendered pixel values directly: the top/left edge lines blend
  to exactly R=204 against the black background (255 * .80 + 0 * .20),
  and the internal boundary/fine-grid/center-cross lines all blend
  consistently with a 255 * .40 blend against their local background --
  confirming the actual on-screen alpha matches the intended values
  exactly, not just "looks dimmer". Full clean rebuild (`outgun`,
  `outgun-ded`) and the `mapeditor_roundtrip` suite (47/47, unaffected --
  this change doesn't touch the document model) re-verified afterward.

- ~~Map editor: Escape now returns to the map picker, with an
  unsaved-changes prompt~~: previously, Escape in the editor
  (`GuiClient::mapEditor_start()`) always unwound straight back to the main
  menu, silently discarding any in-progress edit. Now it goes back to the
  map picker instead (the picker's own Escape still leaves to the main
  menu, unchanged), and if `MapEditorState::dirty` is set (new field, set
  `true` inside `mapEditor_rebuildRenderMap()` on every successful
  committed edit -- the single choke point all ~7 edit types already go
  through -- and reset `false` on save success and on opening/creating a
  map), a new confirm dialog (`GuiClient::mapEditor_confirmDiscardDialog`/
  `Graphics::draw_mapeditor_confirm_discard_dialog`, modeled on the
  existing "New map" dialog's bespoke sub-loop) asks Save/Discard/Cancel
  first. `mapEditor_pickerScreen()` changed from `void` to `bool` (true =
  a map was opened, caller shows the viewer; false = escaped to the main
  menu or quitting), and `MCF_openMapEditorItem()` became an explicit loop
  alternating between the two instead of the old one-shot "call whichever
  one" logic -- an explicit loop rather than having the two functions
  tail-call each other, to avoid unbounded call-stack growth across many
  picker<->viewer round-trips in one long session. Discarding also resets
  `MapEditorState::everOpened = false`: without this, the existing "resume
  the viewer directly" shortcut on the next main-menu "Map editor" click
  would silently resume the exact discarded in-memory document instead of
  showing the picker, and spuriously re-prompt "unsaved changes" even
  though nothing new had been edited since.

  Two real bugs found and fixed while testing this (both pre-existing,
  newly exposed because this is the first time the viewer's own rendering
  and a subsequent fresh screen's rendering could ever share the same
  process run back-to-back): (1) `draw_mapeditor_picker()` (and, added
  defensively, the two other bespoke dialogs) could inherit a leftover
  Allegro drawing mode/clip rect from the viewer's last frame, corrupting
  the picker's first several frames after being re-entered -- fixed with a
  `solid_mode()`/full `set_clip_rect()` reset at the top of each, matching
  what `endPlayfieldDraw()` already resets elsewhere. (2)
  `update_minimap_background()`'s minimap-sizing math has a "+1 for
  safety" rounding step that could push `minimap_h` a pixel past its
  container for some map aspect ratios, which crashed an `nAssert` in
  `BackgroundMasker::addMask` (`graphics.cpp`) once enough picker<->viewer
  round-trips happened to hit it -- fixed by clamping both `minimap_w` and
  `minimap_h` to their container bounds after computing them, in both
  branches.

  Verified via synthetic X11 keyboard input (no mouse needed for the
  Escape/dialog flow itself, though the wall-drawing steps used the mouse
  helpers from Phase 3's own testing): opening a map with no edits and
  pressing Escape goes straight to the picker with no dialog; editing then
  pressing Escape shows the dialog; Cancel stays in the editor with the
  edit intact; Discard returns to the picker and the edit is confirmed
  absent both immediately and after a full process restart + reopen from
  the main menu (the `everOpened` regression case); Save returns to the
  picker and the edit persists to disk; the picker's own Escape still
  reaches the main menu unchanged. Stress-tested 35+ picker<->viewer
  round-trips (1600+ zoom changes) across both a square map and an extreme
  6:1 aspect-ratio map with no crash, confirming the minimap-clamp fix. A
  full local-server play session afterward confirmed normal gameplay is
  unaffected.

- ~~Map editor Phase 3: "New map" button + core mutation (rects,
  spawns/flags, save, validator)~~: turns the Phase 2 read-only viewer into
  an actual editor. `Ctrl+N` in the picker screen (`GuiClient::
  mapEditor_pickerScreen()`) opens a new `GuiClient::mapEditor_newMapDialog()`
  sub-loop -- width/height (`Left`/`Right`, clamped 1-16) and a title (same
  raw-keyboard idiom as the picker's own search box) -- backed by a new
  `EditorMap::initBlank()` (`mapeditor_doc.h`/`.cpp`) that builds a legal,
  empty width x height room grid (`Map::parse_file` only requires a
  non-empty title and w,h != 0, nothing about walls/flags/spawns existing).
  Confirming hands off into `mapEditor_start()` exactly like opening an
  existing map does, with the filename auto-derived from the title
  (`mapEditor_sanitizeFilename`/`mapEditor_uniqueMapName`, `guiclient.cpp`)
  rather than asked for separately.

  `mapEditor_start()` gained five tools (`1`-`5`: Select, Wall rect, Ground
  rect, Flag, Spawn), each mouse-driven: drag to draw a new rect (Wall/
  Ground) or click to place a point (Flag/Spawn, `Tab` cycles team --
  red/blue/wild for flags, red/blue for spawns, no wild-spawn concept
  exists); in Select mode, click to select an existing rect or point (new
  `Graphics::mapEditorHitTest`/`mapEditorHitTestCorner`, priority-ordered
  walls -> ground -> flags -> spawns), drag its body to move it or a corner
  to resize it, `Del` to delete it, `[`/`]` to cycle its texture. `Ctrl+S`
  saves. New `EditorRoom` mutators (`wallAt`/`groundAt`/`eraseWall`/
  `eraseGround`, `mapeditor_doc.h`) make in-place editing possible; they
  didn't exist before this phase (Phase 1 only had `addWall`/`addGround`
  and const readers).

  The one design decision that shapes everything else: every committed
  edit calls a new `GuiClient::mapEditor_rebuildRenderMap()`, which
  round-trips `doc` through the *same* `exportText()`/`Map::parse_file()`
  pair the Phase 1 round-trip test already exercises, rather than writing a
  second `EditorMap -> Map` converter. This doubles as the phase's
  validator for free -- `Map::parse_file` already rejects overlapping
  flags/spawns-on-walls and undersized respawn-area free space -- so a
  rejected edit is simply rolled back to a pre-edit `EditorMap` copy and
  reported via an on-screen status line, no separate validation pass
  needed. Not covered by this (accepted MVP gap, deferred): no minimum-
  spawn-per-team check, no editing of pre-existing respawn areas.

  New `Graphics` additions supporting all of this, kept as additive
  siblings of the Phase 2 methods they sit next to (`graphics.h`/`.cpp`):
  `screenToWorldClampedToRoom` (keeps an in-progress drag confined to the
  room it started in, instead of jumping to a neighboring/wrapped room
  when the mouse strays past an edge -- needed since nothing before this
  phase needed to *constrain* a screen->world conversion, only convert it);
  `mapEditorHitTest`/`mapEditorHitTestCorner`; `draw_mapeditor_edit_overlay`
  (status line + live drag-preview rect/point + persistent selection
  outline, two new `colour_def.inc` entries for the preview/selection
  colours); `draw_mapeditor_newmap_dialog`. Save
  (`GuiClient::mapEditor_save()`) mirrors `ServerWorld::generate_map()`'s
  existing write pattern, always targeting `whereuserdir/cmaps/<name>.txt`
  regardless of where the map was originally opened from, matching the
  already-decided "editor-saved maps go in cmaps/" policy and `Map::
  load()`'s existing "prefer whereuserdir copy" precedence.

  Two real bugs found and fixed while making this actually work: (1) the
  room-bitmap cache (`Graphics::BackgroundManager`) has no way to know the
  underlying `Map` object was swapped out for a new one with the same
  dimensions -- newly-drawn shapes silently didn't render at all until a
  `graphics.mapChanged()` call was added after every successful commit (and
  after opening a map, existing or new, for the same reason) to invalidate
  it; nothing before this phase ever mutated a `Map` object's content
  mid-session, so this was never exercised. (2) `mapEditor_rebuildRenderMap()`
  originally passed the real client `log` to the validating `parse_file()`
  call, so a *correctly rejected* edit (expected, handled, and already
  shown to the user via the on-screen status line) also logged a real
  error and surfaced in the scary exit-time "Errors:" summary dialog --
  switched to a local silent `LogSet(0, 0, 0)`, matching the pattern
  `mapeditor_roundtrip.cpp`'s own test already uses for the same reason.

  Verified via synthetic X11 input -- mouse click/drag synthesis
  (`Xlib.ext.xtest` `ButtonPress`/`ButtonRelease` + `root.warp_pointer` for
  motion; plain `MotionNotify` fake events turned out not to reach
  Allegro's mouse driver at all, a real gap in the established Phase 2/2.5
  testing methodology since no phase before this one ever needed to
  synthesize mouse movement) proven out for the first time this phase.
  Confirmed end-to-end: New Map dialog -> blank map opens; drawing a wall/
  ground rect renders immediately; Select/move/resize/delete all update the
  live view; flag/spawn placement with team cycling; `Ctrl+S` writes the
  exact expected map-text content to `whereuserdir/cmaps/`; reopening the
  same map via the picker after a full process restart shows identical
  content (preview, stats, and the viewer itself) -- a real save-to-disk
  round-trip, not just in-memory; deliberately overlapping a flag onto a
  wall is rejected with the shape never added and the wall unchanged
  (rollback correctness); exiting after a rejected edit produces no error
  dialog (bug (2) fix confirmed). A full local-server play session
  afterward confirmed normal gameplay is unaffected. Full clean rebuild and
  the Phase 1 `mapeditor_roundtrip` suite (extended with one new case for
  `initBlank`, now 47/47) both re-verified afterward.

- ~~Map editor Phase 2.5: two-column picker screen (search filter, minimap
  preview, stats)~~: replaced Phase 2's `Menu`/`TextTree`-based picker with
  a bespoke two-column full-screen loop,
  `GuiClient::mapEditor_pickerScreen()` (`guiclient.h`/`.cpp`), modeled on
  the same self-contained-loop pattern as `mapEditor_start()` itself rather
  than retrofitted onto the `Menu`/`Component` system, which has no concept
  of side-by-side columns or a live side-panel independent of what's
  receiving typed input. `Menu_mapEditor` (`client_menus.h`/`.cpp`) is
  deleted outright; "7. Map editor" is now a plain `Textarea` hook
  (`MCF_openMapEditorItem`), matching how `disconnect`/`exitOutgun` are
  already wired -- this also removes the one awkward part of Phase 2's
  design, the `.setHook()`-instead-of-`.setOpenHook()` workaround needed
  specifically because `MenuStack::open()` always shows a submenu once
  its open-hook has run.

  Left column: a search box (typed characters always go here, giving it
  "focus by default" with no separate focus state to manage -- same model
  as the in-game chat box's `talkbuffer` handling) above the map list,
  grouped into "Standard maps"/"Custom maps" headers with live match
  counts, filtered by case-insensitive substring match against the
  filename as you type; Up/Down skip group headers and clamp (don't wrap)
  at the ends. Right column: stats text (title, author, room size, flag
  counts broken down as `R:n B:n W:n` since red/blue/wild flags are
  naturally separate collections, and total spawn points) plus a live
  minimap preview bitmap, both refreshed on every selection change via a
  new `Graphics::update_minimap_preview()` (mirrors `save_map_picture`'s
  internal `minimap_place_w/h` override trick but targets a caller-owned
  bitmap instead of writing to disk) and a new
  `Graphics::draw_mapeditor_picker()`. All 45 maps (`maps/` + `cmaps/`,
  both `wheregamedir` and `whereuserdir` copies, deduplicated) are
  eagerly `Map::load()`ed once when the screen opens, matching the same
  eager-load pattern `Server::reset_settings()` already uses on every
  server startup. Enter opens the highlighted map in the existing Phase 2
  viewer using the already-loaded `Map` (no re-parse); selecting a map
  ends the picker screen entirely, so only the *viewer* resumes on a
  repeat "Map editor" visit via `MapEditorState::everOpened`, exactly
  matching Phase 2's established once-per-run picker behavior. "New map"
  button intentionally deferred -- nothing to wire it to until a future
  phase implements map creation/saving.

  Verified via synthetic X11 input, same methodology as Phase 2: correct
  two-column layout with accurate live group counts; typing narrows the
  list immediately with no click needed; Up/Down navigation and live
  preview/stats refresh; Backspace restores the unfiltered list; Enter
  opens the correct map in the viewer (confirmed visually correct
  rendering); Escape from the viewer and Escape from the picker itself
  (before selecting anything) both return cleanly to the main menu with
  no crash and no leftover artifacts; re-opening "Map editor" after
  having opened a map resumes the viewer directly without showing the
  picker again, and (on a separate fresh process) shows the picker again
  correctly when nothing was ever opened. A full local-server play
  session afterward confirmed normal gameplay HUD/minimap/scoreboard
  rendering is unaffected. No functional bugs found this phase -- a
  suspected "digit shortcut doesn't open the picker" issue during testing
  was traced to a test-harness input-timing race (input sent before the
  freshly launched window was ready to receive it), not a code defect,
  confirmed by direct runtime inspection of the hook wiring and by the
  identical input mechanism working reliably on every subsequent attempt.

- ~~Map editor Phase 2: menu entry + read-only map viewer~~: "7. Map
  editor" now opens (Help/Exit auto-renumber to 8/9, per `Menu::draw()`'s
  positional numbering -- no separate renumbering logic needed). New
  `Menu_mapEditor` (`src/client_menus.h`/`.cpp`), a flat two-group
  `TextTree` picker ("Standard maps" / "Custom maps", scanning both
  `wheregamedir` and `whereuserdir` copies of `maps/`/`cmaps/`,
  de-duplicated by name -- `Map::load()` still decides which copy actually
  loads). Selecting a map enters `GuiClient::mapEditor_start()`
  (`guiclient.cpp`), a self-contained loop modeled on the existing
  `language_selection_start()`: arrow keys pan a whole room at a time with
  wraparound, PgUp/PgDn zoom, a mouse cursor is shown with a world-coordinate
  readout (new `RoomLayoutManager::screenToWorld()`/`Graphics::screenToWorld()`
  in `graphics.h`/`.cpp`, the screen-to-world inverse of the existing
  `scale_x`/`scale_y`), and the view always renders with `mapInfoMode`
  forced on (grid + spawn/respawn markers) regardless of the player's
  Options setting. State (`GuiClient::MapEditorState`, new member) persists
  in memory for the process's lifetime: opening "Map editor" a second time
  in the same run skips the picker and resumes exactly where you left off
  (implemented as a bespoke `.setHook()` on the menu item, `MCF_openMapEditorItem`,
  since `MenuStack::open()` always shows a submenu once its open-hook has
  run -- the open-hook itself is the wrong place to skip it).

  Two real bugs found and fixed while making this actually render: (1)
  `Graphics::draw_background(const Map&, ...)` unconditionally reserves
  minimap screen space based on `show_minimap` (default on), but nothing
  in a fresh map-viewer session ever computes `minimap_w/h/x/y` the way a
  live game does, so the first render asserted
  (`x0 <= x1 && ... graphics.cpp:3371`) on garbage minimap bounds -- fixed
  by calling `graphics.update_minimap_background(map)` once per map open,
  the same call `draw_game_frame()` already makes on a map change, which
  gives the viewer a real, correctly positioned minimap instead of just
  papering over the crash. (2) Leaving the viewer left a stale ghost of
  its last frame (the minimap thumbnail, a coordinate-readout string)
  visible in the corner of the main menu afterward -- root cause is
  `Graphics::draw_screen()` alternating `drawbuf` between two physical
  video pages when page-flipping is active, so a single post-viewer menu
  frame only refreshes whichever page is current; fixed by drawing two
  extra blank-background frames when the viewer exits.

  Verified via synthetic X11 input injection (`python-xlib`, since no
  `xdotool`/similar was available) driving the real client end-to-end:
  correct item numbering; picker shows accurate counts (20 standard, 25
  custom maps, matching the Phase 1 round-trip test's own count) and
  correct filenames; a real map renders correctly (walls, floor texture,
  grid, team spawn markers, minimap, mouse-coordinate overlay);
  wraparound panning; Escape returns cleanly to the main menu with no
  crash and no leftover visual artifacts; re-opening the editor resumes
  instantly without showing the picker again; and a full local-server
  play session afterward confirmed normal gameplay HUD/minimap/scoreboard
  rendering is unaffected. Full clean rebuild and the Phase 1
  `mapeditor_roundtrip` suite (still 46/46) both re-verified afterward.

- ~~Map editor Phase 1: internal document model + map-text serializer +
  automated round-trip tests~~: no UI yet (see "Features" above for the
  remaining phases) -- this lands the foundation everything else depends
  on. `src/mapeditor_doc.h`/`.cpp` add a new, fully mutable
  `EditorMap`/`EditorRoom`/`EditorWall` document model, kept deliberately
  separate from `world.h`'s `Map`/`Room`/`WallBase` (those are
  read-mostly and used directly by rendering/collision/networking).
  `EditorMap::importFrom(const Map&)` builds a document from an
  already-parsed map; `EditorMap::exportText(ostream&)` writes it back out
  in the format `Map::parse_line` (`world.cpp`) reads -- always emitting
  `S 472 354` as the scale, which makes every written coordinate a
  conversion-free copy of the engine's internal unit (since the parser's
  own `value * plw/scalex` conversion becomes an identity multiplication
  at that scale). Also added `WallBase::alpha()` to `world.h` (previously
  write-only: parsed from the map file's optional alpha field but never
  read back anywhere in the engine), a prerequisite for the exporter to
  round-trip it faithfully.

  New automated test `src/tests/mapeditor_roundtrip.cpp`, wired into the
  existing (until now apparently never-run in this porting effort --
  found and left a separate, unrelated, pre-existing failure in
  `tests/binarybuffer.cpp` alone, confirmed via `git stash` to predate
  this work entirely) `tests/` harness (`make testsuite` /
  `make run_tests` / `make test_mapeditor_roundtrip`). For every map
  under `maps/` and `cmaps/`, plus a new hand-authored fixture
  (`src/tests/mapeditor_fixtures/alpha_respawn.txt`, since no shipped map
  exercises the alpha extension or point-form `V respawn` areas): parses
  it, imports it, exports it back to text, re-parses *that*, and
  structurally compares the two resulting `Map` objects (not `Map::crc`,
  which is computed over file bytes and isn't expected to match
  byte-for-byte re-emitted text). All 46 maps pass. Verified with a full
  clean rebuild of `outgun`/`outgun-ded` (unaffected -- nothing from this
  phase is wired into gameplay/menus yet) and a smoke-tested dedicated
  server run, confirming no regression from the `world.h` change.

- ~~Change default values for pups_drop_at_death (1), private_server (1),
  pup_deathbringer_time (4.0), time_limit (10), extra_time (5),
  sudden_death (1), game_end_delay (7), random_maprot (2)~~: updated both
  the C++ code-level defaults and the shipped `config/gamemod.txt` template
  (which overrides code defaults once seeded to `whereuserdir`), matching
  the pattern used for the earlier `bot_ping` default change.
  `src/gameserver_interface.h`: `ServerExternalSettings::privateserver`
  now defaults to `true`. `src/server_settings.cpp`: `game_end_delay`
  7 (was 5); `random_maprot` setter path now defaults to "2" (random
  first map) via `random_maprot = false` / `random_first_map = true`.
  `src/world.cpp` (`PowerupSettings`/`WorldSettings::reset()`):
  `pups_drop_at_death` true, `pup_deathbringer_time` 4.0, `time_limit`
  6000 and `extra_time` 3000 (both in frames — internally minutes are
  multiplied by `60 * 10`, so these are 10 and 5 minutes respectively),
  `sudden_death` true. `config/gamemod.txt` updated to match, with each
  setting's comment corrected to show its new default.

  While tracing `random_maprot`'s two-flag mapping (`random_maprot` +
  `random_first_map`), found `random_first_map` (`src/server.h`, read by
  `get_random_first_map()` in `src/server.cpp`) was never given an explicit
  value in `SettingManager::reset()` — it was only ever set via the
  gamemod.txt setter, so a server with no `random_maprot` line in its
  gamemod.txt was reading an uninitialized bool. Fixed by initializing it
  in `reset()` to `true`, matching the new default. Verified with a clean
  rebuild, a fresh-`whereuserdir` run (confirmed the seeded gamemod.txt
  matches the new template exactly), and a minimal-deployment run with no
  gamemod.txt at all (confirmed no crash from the previously-uninitialized
  read); no new coredumps in either case.

- ~~Remove date from release filenames~~: dropped the `<build date>`
  component from both `packaging/appimage/build-appimage.sh` and
  `packaging/linux-release/build-release.sh`. Outputs are now
  `Outgun-<version>-x86_64.AppImage` and
  `Outgun-<version>-linux-x86_64.tar.gz`.

- ~~Create Linux x86_64 release package for GH that includes both client and
  server files (make sure it bundles config/ alongside the binaries)~~:
  `packaging/linux-release/build-release.sh` builds `outgun` and
  `outgun-ded`, then packages them with `config/` and all the other
  read-only asset directories (`graphics/`, `sound/`, `fonts/`,
  `languages/`, `mapgen/`, `maps/`, `cmaps/`), plus `COPYING`, `README.txt`,
  and `doc/`, into a dated tarball
  (`Outgun-<version>-<build date>-linux-x86_64.tar.gz`, mirroring the
  AppImage's naming). Unlike the AppImage this is **not self-contained** —
  it relies on Allegro 4 and HawkNL being installed on the target system —
  so a generated `INSTALL.txt` explains that tradeoff and how to install
  them. Verified by extracting the tarball to a fresh directory and running
  both binaries from there: config seeding (`auth.txt`/`gamemod.txt`) and
  the dedicated server both worked correctly; the GUI client was confirmed
  alive with no crash/coredump, though a screen lock prevented a final
  visual screenshot this run (the identical binary was already visually
  verified multiple times earlier in unrelated tests).

- ~~Where is version number set? Update this version to 1.0.4~~:
  `GAME_RELEASED_VERSION_SHORT`/`GAME_RELEASED_VERSION` in
  `src/version.cpp`, now `1.0.4`. Also bumped the `VERSION` variable in
  `packaging/appimage/build-appimage.sh` to match (it's independent, not
  derived from the source). The network protocol version
  (`GAME_PROTOCOL` in `src/protocol.h`) is a separate, unrelated concept
  and was left unchanged.

- ~~Bot ping range in previous outgun versions was 1-500, now 1-2000, so
  defaults in gamemod files are very difficult~~: default `bot_ping`
  changed from 300 to 700 (`src/server_settings.cpp`), and the shipped
  `config/gamemod.txt` template — which sets `bot_ping 100` as an active
  setting, overriding the code default for anyone it gets seeded to —
  updated to `bot_ping 700` with its range/default comment corrected from
  the stale "0 to 500, default: 100" to "0 to 2000, default: 700".

- ~~No sound on Mint 22, but works in Manjaro~~: the AppImage was bundling
  `libasound.so.2` (ALSA's own runtime) built on the Arch/Manjaro host, but
  `libasound.so.2` itself dynamically loads a distro's real audio-routing
  plugin (e.g. PipeWire's ALSA shim) from a path baked into that specific
  build — confirmed via `strings` that the bundled copy hardcodes
  `/usr/lib/alsa-lib`, while Debian/Ubuntu-family systems (Mint included)
  use `/usr/lib/x86_64-linux-gnu/alsa-lib/` instead, so the bundled shim
  could never find Mint's real backend and device open failed silently.
  Removed the `cp .../libasound.so.2` line from
  `packaging/appimage/build-appimage.sh` — the loader now falls through to
  the host's own (correctly distro-configured) copy, which every
  mainstream desktop distro ships as a baseline dependency anyway. The two
  small Allegro ALSA driver plugins stay bundled; they don't have this
  problem. Also added `allegro_error` to the sound-init failure log
  (`src/sounds.cpp`) so any future audio issue is diagnosable from a log
  file instead of requiring this kind of manual root-causing. Verified the
  mechanism directly (confirmed via `/proc/<pid>/maps` that the rebuilt
  AppImage now loads `/usr/lib/libasound.so.2` from the host, not the
  bundle) — **not yet verified on actual Mint 22 hardware**, since none was
  available to test on; the evidence is strong but this needs a real
  confirmation.

- ~~Fix /bot ping to apply to bots created going forward, not all bots. Implement previously unfinished `/bot ping N all` command~~~

- ~~Set default "Show player names" setting to Always~~

- ~~Update dead.pcx to splat graphic~~: `graphics/Grass/dead.pcx` and
  `dead_alpha.pcx` replaced.

- ~~Clean up default config~~: `autoGetServerList` ("Get server list at
  startup") now defaults to off (`src/client_menus.cpp`), but the deeper fix
  was in `MasterSettings::load()` (`src/commont.cpp`): it unconditionally did
  *synchronous DNS resolution* for the master/ranking/bug-report server
  names on every launch, completely independent of `autoGetServerList` or
  any other setting — that's what was actually causing the delay/timeout,
  not the list-fetch itself. The hard-coded defaults (`koti.mbnet.fi`,
  `outgun.com.br`, `nix.dnsalias.net` — all long dead) are now blank, so
  the length checks skip resolution entirely and startup is instant; point
  `whereuserdir/config/master.txt` at a real master server to opt back in.
  Also: bug report policy now defaults to disabled on the first-run splash;
  default "Rooms on screen in each direction in game" is 1; "Scrolling"
  defaults off; default theme is Grass, default background theme is Metal;
  "Show favorite servers" now defaults on (all `src/client_menus.cpp`).
- ~~gamemod.txt gets created only when running via outgun-ded~~ /
  ~~client GUI-launched server shows "Can't open game mod file"~~: the real
  bug was that `seedUserConfigFileIfMissing()` (`src/main.cpp`) seeds
  `whereuserdir/config/gamemod.txt` from the shipped
  `wheregamedir/config/gamemod.txt` template — which silently does nothing
  if that template isn't present (e.g. a minimal deployment that only ships
  the binary, without the `config/` directory). A missing gamemod.txt was
  already handled gracefully (server runs fine on built-in defaults), but
  `Server::SettingManager::loadGamemod()` (`src/server_settings.cpp`) logged
  it as an *error*, which surfaced in the scary "Errors:" summary shown at
  exit. Downgraded to a plain informational log line. Verified by running
  `outgun-ded`/`outgun` from a directory containing only the binary (+
  `maps/`, required regardless) — no more error, either at startup or exit.
- ~~Add config paths to readme~~: see "File locations" above.
