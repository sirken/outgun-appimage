#!/usr/bin/env bash
#
# Builds Outgun from source and packages the client + dedicated server, plus
# all the assets they need, into a plain (non-self-contained) release tarball
# for GitHub releases. Unlike the AppImage (packaging/appimage/), this does
# NOT bundle Allegro/HawkNL/etc. -- it relies on the end user's system having
# them installed. Use the AppImage for a fully portable, dependency-free
# option; use this for a lighter package aimed at users who'll install (or
# already have) the runtime dependencies themselves.
#
# Requires (Arch Linux package names in parentheses):
#   - g++ / make                    (base-devel)
#   - Allegro 4 headers/libs        (allegro4, from the official 'extra' repo)
#   - HawkNL headers/libs           (hawknl-git, from the AUR)
#
# Usage: ./packaging/linux-release/build-release.sh [--dev]
# Output: linux-release-build/Outgun-<version>-linux-x86_64.tar.gz
#         (or Outgun-<version>-<short-hash>[-dirty]-linux-x86_64.tar.gz with --dev)
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

BUILD_DIR="$REPO_ROOT/linux-release-build"
PKG_NAME="Outgun-${VERSION}${BUILD_SUFFIX}-linux-${ARCH}"
STAGE_DIR="$BUILD_DIR/$PKG_NAME"
TARBALL="$BUILD_DIR/${PKG_NAME}.tar.gz"

log() { echo ">>> $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

# -- 1. Sanity-check the build dependencies --

command -v g++ >/dev/null 2>&1 || die "g++ not found. Install base-devel (or your distro's equivalent)."
command -v make >/dev/null 2>&1 || die "make not found. Install base-devel (or your distro's equivalent)."
command -v allegro-config >/dev/null 2>&1 || die "allegro-config not found. Install Allegro 4 (Arch: 'pacman -S allegro4')."
[ -f /usr/include/nl.h ] || [ -f /usr/local/include/nl.h ] || die "nl.h (HawkNL) not found. Install HawkNL (Arch AUR: 'hawknl-git')."

# -- 2. Build the game --

log "Building Outgun (client + dedicated server)..."
DEVBUILD_MAKEARG=""
[ "$DEV_BUILD" = "1" ] && DEVBUILD_MAKEARG="DEVBUILD=1"
make -C "$REPO_ROOT/src" -f Makefile.common LINUX=1 clean
make -C "$REPO_ROOT/src" -f Makefile.common LINUX=1 $DEVBUILD_MAKEARG outgun outgun-ded

# -- 3. Assemble the release directory --

log "Assembling release package..."
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
cp "$REPO_ROOT/outgun" "$REPO_ROOT/outgun-ded" "$STAGE_DIR/"

# Read-only bundled assets, same set as the AppImage -- wheregamedir is resolved
# relative to the executable at runtime, so these must live alongside the binaries.
# config/ is also included here (unlike the AppImage's usr/bin layout it's identical):
# it's what seedUserConfigFileIfMissing() (src/main.cpp) seeds into $XDG_DATA_HOME/outgun
# on first run (notably auth.txt and gamemod.txt, which either can't gracefully default
# or carry valuable shipped documentation -- see README.md's "File locations" section).
for d in graphics sound fonts languages mapgen maps cmaps config; do
    cp -r "$REPO_ROOT/$d" "$STAGE_DIR/"
done
rm -rf "$STAGE_DIR/maps/generated"  # never ship generated-map litter from a local test run

cp "$REPO_ROOT/COPYING" "$REPO_ROOT/README.txt" "$STAGE_DIR/"
cp -r "$REPO_ROOT/doc" "$STAGE_DIR/"

cat > "$STAGE_DIR/INSTALL.txt" <<EOF
Outgun ${VERSION}${BUILD_SUFFIX} -- Linux ${ARCH} release package
====================================================

This package is NOT self-contained: it needs Allegro 4 and HawkNL installed
on your system. If you'd rather not install anything, use the AppImage
release instead, which bundles everything it needs.

On Arch Linux:
    sudo pacman -S allegro4
    yay -S hawknl-git   # from the AUR

On Debian/Ubuntu-family systems, install Allegro 4 and HawkNL via your
package manager or build them from source; exact package names vary by
release. See https://github.com/dfyx/HawkNL for HawkNL source if it isn't
packaged for your distro.

Once the dependencies are installed, run either binary directly from this
directory (do not move them without the folders alongside them):

    ./outgun                       # GUI client
    ./outgun-ded -priv -port 25000 # dedicated server

Settings, logs, replays, and downloaded/generated maps are stored under
\$XDG_DATA_HOME/outgun (~/.local/share/outgun by default), created
automatically on first run. See README.md in the source repository for
more detail.
EOF

# -- 4. Package the tarball --

log "Creating tarball..."
rm -f "$TARBALL"
tar -C "$BUILD_DIR" -czf "$TARBALL" "$PKG_NAME"

log "Done: $TARBALL"
