
# Bugs


# Features

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
