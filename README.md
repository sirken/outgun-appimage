# Outgun

Outgun is a 2D, 32-player multiplayer capture-the-flag game, originally released
by Niko Ritari, Jani Rivinoja, and others. This fork is based on the 1.0.3
development source snapshot, updated to build on current Linux distributions
and packaged as a self-contained AppImage. The packaged/reported game version
has moved on from that snapshot and is currently `1.0.4` (`src/version.cpp`).

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
`appimage-build/Outgun-<version>-<build date>-x86_64.AppImage`.

The script deliberately does *not* bundle `libasound.so.2` (ALSA's own
runtime) even though the ALSA driver plugin links against it — unlike a
plain leaf library, `libasound.so.2` itself dynamically discovers a distro's
real audio backend (e.g. PipeWire's ALSA compatibility plugin) from a path
baked into that specific build, so bundling one distro's copy breaks sound
on others. It's left to resolve from the target system's own (correctly
configured) copy instead. See the comments in the script for the full
reasoning.

The script requires the same dependencies as a normal build (Allegro 4,
HawkNL) plus `curl`. It's written for Arch's Allegro 4 package layout
(`/usr/lib/allegro/<version>/`) but should work on any distro with an
equivalent layout.

Packaging source assets (`.desktop` file, icon) live in `packaging/appimage/`
and are tracked in git; the generated `AppDir/` and `.AppImage` output are not.

## Building the Linux release package

```sh
./packaging/linux-release/build-release.sh
```

Builds `outgun`/`outgun-ded` and packages them with `config/` (needed for
`seedUserConfigFileIfMissing()` to seed `auth.txt`/`gamemod.txt` on first
run) and the other read-only asset directories, plus `COPYING`, `README.txt`,
and `doc/`, into a dated tarball:
`linux-release-build/Outgun-<version>-<build date>-linux-x86_64.tar.gz`.

Unlike the AppImage, this is **not self-contained** — it relies on Allegro 4
and HawkNL being installed on whatever system it's run on. Use it for a
lighter package aimed at users who'll install those themselves; use the
AppImage for a fully portable, dependency-free option. A generated
`INSTALL.txt` inside the tarball documents the runtime dependency and basic
usage.

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
