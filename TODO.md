
# Bugs


# Features

- Create map editor


# Questions

- Where is version number set? So we can increment version numbers as we make changes

- How are server generated maps created?


# Packaging

- Add AppImage release to GH

- Create Linux x86_64 release package for GH (make sure it bundles `config/`
  alongside the binaries — see the `gamemod.txt`/`auth.txt` note below)


# Done

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
