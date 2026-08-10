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
# Usage: ./packaging/appimage/build-appimage.sh [--dev]
# Output: appimage-build/Outgun-<version>-x86_64.AppImage
#         (or appimage-build/Outgun-<version>-<short-hash>[-dirty]-x86_64.AppImage with --dev)
#
# --dev stamps the current short git commit hash (plus a -dirty suffix if the working
# tree has uncommitted changes) into the output filename, and builds with DEVBUILD=1
# (see src/debugconfig.h) for extra runtime diagnostics.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

VERSION="1.0.4"
ARCH="x86_64"

DEV_BUILD=0
if [ "${1:-}" = "--dev" ]; then
    DEV_BUILD=1
    shift
fi

BUILD_SUFFIX=""
if [ "$DEV_BUILD" = "1" ]; then
    GIT_HASH="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    if [ -n "$(git -C "$REPO_ROOT" status --porcelain 2>/dev/null)" ]; then
        GIT_HASH="${GIT_HASH}-dirty"
    fi
    BUILD_SUFFIX="-${GIT_HASH}"
fi

TOOLS_DIR="$REPO_ROOT/appimage-tools"
BUILD_DIR="$REPO_ROOT/appimage-build"
APPDIR="$BUILD_DIR/AppDir"
APPIMAGE_OUT="$BUILD_DIR/Outgun-${VERSION}${BUILD_SUFFIX}-${ARCH}.AppImage"

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
DEVBUILD_MAKEARG=""
[ "$DEV_BUILD" = "1" ] && DEVBUILD_MAKEARG="DEVBUILD=1"
make -C "$REPO_ROOT/src" -f Makefile.common LINUX=1 clean
make -C "$REPO_ROOT/src" -f Makefile.common LINUX=1 $DEVBUILD_MAKEARG outgun outgun-ded

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
#
# Deliberately NOT bundling libasound.so.2 (ALSA's own runtime), even though
# alleg-alsadigi.so links against it: unlike the plugin .so files above, libasound
# itself dynamically discovers and loads a distro's actual PCM routing backend (e.g.
# PipeWire's ALSA compatibility plugin) from a path baked into ITS build -- confirmed
# via `strings` that an Arch-built libasound.so.2 hardcodes /usr/lib/alsa-lib, while
# Debian/Ubuntu-family systems (Mint, etc.) use /usr/lib/x86_64-linux-gnu/alsa-lib/
# instead. Bundling our build's libasound.so.2 silently broke sound on other distros
# (worked on Manjaro, the build host's own family; silent no-sound on Mint 22) because
# the bundled shim could never find the target system's real audio routing plugin.
# Leaving libasound.so.2 unbundled lets the dynamic linker fall through to the host's
# own (correctly distro-configured) copy, which every mainstream desktop distro ships
# as a baseline dependency -- same reasoning as relying on the host's libc/libX11
# rather than bundling those. Do not re-add this "for safety"; it broke things.
log "Bundling Allegro sound driver plugins..."
mkdir -p "$APPDIR/usr/lib/allegro/$ALLEG_MODULE_VERSION"
cp "$ALLEG_MODULE_DIR/alleg-alsadigi.so" "$ALLEG_MODULE_DIR/alleg-alsamidi.so" "$ALLEG_MODULE_DIR/modules.lst" \
    "$APPDIR/usr/lib/allegro/$ALLEG_MODULE_VERSION/"

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
