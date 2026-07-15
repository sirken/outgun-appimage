# Outgun

Outgun is a 2D, 32-player multiplayer capture-the-flag game, originally released
by Niko Ritari, Jani Rivinoja, and others. This is the 1.0.3 development source
snapshot, updated to build on current Linux distributions and packaged as a
self-contained AppImage.

See [`README.txt`](README.txt) for the original project history and credits, and
[`doc/`](doc/) for the full original game/server/map documentation. This file
only covers building the modernized source.

## Status of this port

The original source (circa 2008–2011) doesn't compile with modern GCC out of the
box, and its file-handling assumed the install directory itself was writable —
neither of which holds up on current systems or inside a read-only AppImage.
Concretely, this fork:

- Pins `-std=gnu++98` in `src/Makefile.common` so the codebase's pervasive
  dynamic exception specifications and `std::auto_ptr` usage stay legal under a
  modern compiler (GCC's default is C++20, which removes both).
- Fixes two real two-phase-lookup compile errors in `src/menu.h` that older,
  more lenient compilers silently accepted.
- Fixes a crash on first run (an uninitialized `menusel` read by the "choose
  language" screen before it was ever set).
- Splits read-only bundled assets from writable per-user data: `graphics/`,
  `sound/`, `fonts/`, `languages/`, `mapgen/`, and shipped `maps/` stay next to
  the executable (`wheregamedir`), while config, logs, replays, stats,
  downloaded/generated maps now live under `$XDG_DATA_HOME/outgun`
  (`~/.local/share/outgun` by default, a new `whereuserdir`). This is what
  makes the AppImage (and any other read-only install) actually work — the
  game auto-creates and seeds that directory on first run.

## Dependencies

On Arch Linux:

```sh
sudo pacman -S allegro4    # official 'extra' repo
yay -S hawknl-git          # AUR; if it fails to build, see https://github.com/dfyx/HawkNL
```

Allegro 4 (not 5) and HawkNL are the only non-standard dependencies. Everything
else (pthreads, X11 client libs) is standard on any Linux desktop.

## Building from source

```sh
make -C src -f Makefile.common LINUX=1 outgun outgun-ded
```

This produces `outgun` (GUI client) and `outgun-ded` (dedicated server, no
Allegro dependency) at the repo root, alongside the asset directories they
expect (`graphics/`, `sound/`, etc.). Run either directly from there:

```sh
./outgun
./outgun-ded -priv -port 25000
```

Other useful `make` targets: `tools` (srvmonit, relay, watchserver), `all`,
`testsuite`, `clean`. See `src/Makefile.common` for the full list.

## Building the AppImage

```sh
./packaging/appimage/build-appimage.sh
```

This builds the game, assembles an AppDir, bundles Allegro/HawkNL and their
runtime-loaded ALSA sound driver plugins (which aren't relocatable by default —
see the comments in the script), and packages everything with `linuxdeploy` +
`appimagetool`. Both tools are downloaded automatically on first run into
`appimage-tools/` (not tracked in git). The result lands at
`appimage-build/Outgun-<version>-x86_64.AppImage`.

The script requires the same dependencies as a normal build (Allegro 4,
HawkNL) plus `curl`. It's written for Arch's Allegro 4 package layout
(`/usr/lib/allegro/<version>/`) but should work on any distro with an
equivalent layout.

Packaging source assets (`.desktop` file, icon) live in `packaging/appimage/`
and are tracked in git; the generated `AppDir/` and `.AppImage` output are not.

## File locations

Read-only bundled assets (`graphics/`, `sound/`, `fonts/`, `languages/`,
`mapgen/`, shipped `maps/`) are loaded relative to the running executable
(`wheregamedir`) and never written to.

Everything writable — settings, logs, replays, stats, downloaded/generated
maps — lives under `$XDG_DATA_HOME/outgun` (`whereuserdir`), which defaults
to:

```
~/.local/share/outgun/
├── config/          # client.cfg, gamemod.txt, auth.txt, master.txt, ...
├── log/
├── replay/
├── maps/generated/  # server-generated random maps
├── cmaps/           # client-downloaded maps
├── screens/         # screenshots
├── client_stats/
└── server_stats/
```

`config/auth.txt` and `config/gamemod.txt` are seeded from the shipped
templates on first run; everything else is created fresh as needed. See
`src/platform_unix.cpp` (`platInitAfterAllegro`) and `src/main.cpp`
(`seedUserConfigFileIfMissing`) for the actual logic.

## License

GPL-2.0-or-later. See [`COPYING`](COPYING).


## TODO

- limited to 99 maps? (unconfirmed — no hardcoded 99-map limit found in the
  server rotation or client map-list code; needs a concrete repro)

### Packaging

- Add AppImage release to GH
- Create Linux x86_64 release package for GH (make sure it bundles `config/`
  alongside the binaries — see the `gamemod.txt`/`auth.txt` note below)

### Done

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
