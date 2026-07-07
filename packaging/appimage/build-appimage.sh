#!/usr/bin/env bash
#
# Builds Outgun from source and packages it as a self-contained x86_64 AppImage.
#
# Requires (Arch Linux package names in parentheses):
#   - g++ / make                    (base-devel)
#   - Allegro 4 headers/libs        (allegro4, from the official 'extra' repo)
#   - HawkNL headers/libs           (hawknl-git, from the AUR)
#   - curl                          (curl)
# linuxdeploy and appimagetool are downloaded automatically on first run.
#
# Usage: ./packaging/appimage/build-appimage.sh
# Output: appimage-build/Outgun-<version>-x86_64.AppImage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

VERSION="1.0.3"
ARCH="x86_64"

TOOLS_DIR="$REPO_ROOT/appimage-tools"
BUILD_DIR="$REPO_ROOT/appimage-build"
APPDIR="$BUILD_DIR/AppDir"
APPIMAGE_OUT="$BUILD_DIR/Outgun-${VERSION}-${ARCH}.AppImage"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"

log() { echo ">>> $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

# -- 1. Sanity-check the build dependencies --

command -v g++ >/dev/null 2>&1 || die "g++ not found. Install base-devel (or your distro's equivalent)."
command -v make >/dev/null 2>&1 || die "make not found. Install base-devel (or your distro's equivalent)."
command -v curl >/dev/null 2>&1 || die "curl not found. Install curl."
command -v allegro-config >/dev/null 2>&1 || die "allegro-config not found. Install Allegro 4 (Arch: 'pacman -S allegro4')."
ALLEG_LIBDIR="$(allegro-config --libdir 2>/dev/null || true)"
[ -f /usr/include/nl.h ] || [ -f /usr/local/include/nl.h ] || die "nl.h (HawkNL) not found. Install HawkNL (Arch AUR: 'hawknl-git')."

# Allegro 4's Unix sound/MIDI driver modules live in a version-numbered directory
# (e.g. /usr/lib/allegro/4.4.3/) that isn't exposed by allegro-config; find it directly.
ALLEG_MODULE_DIR="$(find /usr/lib/allegro /usr/local/lib/allegro -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1 || true)"
[ -n "$ALLEG_MODULE_DIR" ] || die "Couldn't find Allegro's module directory (expected e.g. /usr/lib/allegro/4.4.3/). Is allegro4 installed correctly?"
ALLEG_MODULE_VERSION="$(basename "$ALLEG_MODULE_DIR")"
log "Found Allegro modules at $ALLEG_MODULE_DIR"

# -- 2. Fetch linuxdeploy / appimagetool if not already present --

mkdir -p "$TOOLS_DIR"
if [ ! -x "$LINUXDEPLOY" ]; then
    log "Downloading linuxdeploy..."
    curl -fL -o "$LINUXDEPLOY" "$LINUXDEPLOY_URL"
    chmod +x "$LINUXDEPLOY"
fi
if [ ! -x "$APPIMAGETOOL" ]; then
    log "Downloading appimagetool..."
    curl -fL -o "$APPIMAGETOOL" "$APPIMAGETOOL_URL"
    chmod +x "$APPIMAGETOOL"
fi

# -- 3. Build the game --

log "Building Outgun (client + dedicated server)..."
make -C "$REPO_ROOT/src" -f Makefile.common LINUX=1 clean
make -C "$REPO_ROOT/src" -f Makefile.common LINUX=1 outgun outgun-ded

# -- 4. Assemble the AppDir --

log "Assembling AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
cp "$REPO_ROOT/outgun" "$REPO_ROOT/outgun-ded" "$APPDIR/usr/bin/"

# Read-only bundled assets live next to the executable, matching the Makefile's own
# TARGETBINDIR=".." convention -- wheregamedir is resolved from /proc/self/exe at
# runtime, so this also works correctly once mounted inside the AppImage.
for d in graphics sound fonts languages mapgen maps cmaps; do
    cp -r "$REPO_ROOT/$d" "$APPDIR/usr/bin/"
done
rm -rf "$APPDIR/usr/bin/maps/generated"  # never ship generated-map litter from a local test run

log "Running linuxdeploy..."
NO_STRIP=1 "$LINUXDEPLOY" \
    --appdir="$APPDIR" \
    --executable="$APPDIR/usr/bin/outgun" \
    --executable="$APPDIR/usr/bin/outgun-ded" \
    --desktop-file="$SCRIPT_DIR/outgun.desktop" \
    --icon-file="$SCRIPT_DIR/outgun.png"

# Allegro 4's Unix digital/MIDI sound drivers are dlopen'd from a path baked into
# liballeg.so at ITS OWN build time, which is not relocatable and not picked up by
# linuxdeploy's ldd-based scan. Bundle them and override the search path via
# ALLEGRO_MODULES in a custom AppRun (see below). JACK is skipped deliberately: it
# pulls in libjack/libdb and is a niche pro-audio setup; ALSA covers virtually
# everything else.
log "Bundling Allegro sound driver plugins..."
mkdir -p "$APPDIR/usr/lib/allegro/$ALLEG_MODULE_VERSION"
cp "$ALLEG_MODULE_DIR/alleg-alsadigi.so" "$ALLEG_MODULE_DIR/alleg-alsamidi.so" "$ALLEG_MODULE_DIR/modules.lst" \
    "$APPDIR/usr/lib/allegro/$ALLEG_MODULE_VERSION/"
cp /usr/lib/libasound.so.2 "$APPDIR/usr/lib/"

log "Writing custom AppRun..."
rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<'APPRUN_EOF'
#!/bin/sh
# Custom AppRun: Allegro 4's Unix sound/MIDI driver modules are dlopen'd from a path
# baked into liballeg.so at its own build time (see modules.lst next to them), which is
# not relocatable via LD_LIBRARY_PATH alone. ALLEGRO_MODULES overrides that search path.
HERE="$(dirname "$(readlink -f "${0}")")"
export ALLEGRO_MODULES="${HERE}/usr/lib/allegro/ALLEG_MODULE_VERSION_PLACEHOLDER"
export LD_LIBRARY_PATH="${HERE}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${HERE}/usr/bin/outgun" "$@"
APPRUN_EOF
sed -i "s/ALLEG_MODULE_VERSION_PLACEHOLDER/$ALLEG_MODULE_VERSION/" "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

# -- 5. Package the final AppImage --

log "Running appimagetool..."
mkdir -p "$BUILD_DIR"
rm -f "$APPIMAGE_OUT"
ARCH="$ARCH" "$APPIMAGETOOL" "$APPDIR" "$APPIMAGE_OUT"

log "Done: $APPIMAGE_OUT"
