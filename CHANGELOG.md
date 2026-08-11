## Changelog for 1.0.4

**Porting to modern Linux**
- Fixed the build for modern GCC (was written for ~2008-era compilers)
- Split game assets from user data — settings/logs/replays/maps now live in `~/.local/share/outgun` instead of next to the binary, which is what makes packaged installs work at all
- Fixed a crash on first run

**New packaging**
- Self-contained AppImage
- Lighter Linux x86_64 release tarball (relies on system-installed Allegro4/HawkNL)
- New `--dev` build option for both: stamps the git commit hash into the filename and enables extra diagnostic logging
- Every build now reports its exact git commit in the version string

**Bug fixes**
- Fixed ~30-60s startup delay (was doing DNS lookups against long-dead master servers on every launch, regardless of settings)
- Fixed a spurious "can't open game mod file" error shown on exit
- Fixed `/bot ping` — changing it used to silently reset every existing bot's ping instead of just new ones; added the (previously unimplemented) explicit `all` option
- Fixed no-sound-on-some-distros bug in the AppImage (bundled ALSA library broke audio routing on Debian/Ubuntu-family systems)
- Fixed the "random first map" setting being read without ever being initialized when a server's gamemod.txt didn't set `random_maprot` explicitly

**Default settings tweaks (better out of the box)**
- "Get server list at startup" and bug-report policy now off by default
- "Show player names" and "Show favorite servers" now on by default
- Sensible defaults for rooms-on-screen, scrolling, theme/background, and bot ping (range grew 5x since the old default was set)
- Servers are private by default, drop powerups on death, and use a 10-minute time limit with 5 minutes of extra time and sudden death enabled
- Map rotation now defaults to picking a random first map; game-end delay bumped to 7s and deathbringer time to 4s
