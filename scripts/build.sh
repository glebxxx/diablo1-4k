#!/usr/bin/env bash
# Build DevilutionX (this fork) natively on macOS with only the Xcode Command Line Tools.
# All third-party dependencies are fetched by CMake and linked statically, so no Homebrew packages are needed.
# Usage: scripts/build.sh [Release|RelWithDebInfo|Debug] [extra cmake args...]
# Output: build/Diablo 4K.app (see DIABLO4K_BUNDLE_NAME in devilutionx/CMakeLists.txt).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${1:-Release}"
shift || true
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Translations (.gmo, including Russian) are only compiled when gettext's msgfmt is available.
# Command Line Tools do not ship it; without it the game is English-only.
if ! command -v msgfmt >/dev/null; then
	echo "WARNING: msgfmt not found - translations (e.g. Russian) will NOT be built." >&2
	echo "         Install it with: brew install gettext" >&2
fi

cmake -S "$ROOT/devilutionx" -B "$BUILD_DIR" \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
	-DBUILD_TESTING=OFF \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 \
	-DDEVILUTIONX_SYSTEM_SDL2=OFF \
	-DDEVILUTIONX_SYSTEM_SDL_IMAGE=OFF \
	-DDEVILUTIONX_SYSTEM_LIBPNG=OFF \
	-DDEVILUTIONX_SYSTEM_LIBSODIUM=OFF \
	-DDEVILUTIONX_SYSTEM_LUA=OFF \
	"$@"
cmake --build "$BUILD_DIR" -j "$JOBS"

# CMake builds devilutionx.app; its Info.plist already carries the fork's name and bundle id
# (DIABLO4K_BUNDLE_NAME / DIABLO4K_BUNDLE_ID). Package it under that name, e.g. "Diablo 4K.app".
# The build tree copy stays, so incremental builds and tests keep working.
BUILT="$BUILD_DIR/devilutionx.app"
APP_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleName' "$BUILT/Contents/Info.plist")"
APP="$BUILD_DIR/$APP_NAME.app"
if [ "$APP" != "$BUILT" ]; then
	rm -rf "$APP"
	ditto "$BUILT" "$APP"
fi
# Ad-hoc signature so the bundle has a valid seal on this machine.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
# The Info.plist must match what CMake was configured with (the fork's values unless overridden).
cache_value() { sed -n "s/^$1:[A-Z]*=//p" "$BUILD_DIR/CMakeCache.txt"; }
EXPECT_NAME="$(cache_value DIABLO4K_BUNDLE_NAME)"
EXPECT_ID="$(cache_value DIABLO4K_BUNDLE_ID)"
EXPECT_DISPLAY_NAME="$EXPECT_NAME"
[ "$EXPECT_NAME" != devilutionx ] || EXPECT_DISPLAY_NAME=DevilutionX # upstream's display name
EXPECT_NAME="$EXPECT_NAME" EXPECT_DISPLAY_NAME="$EXPECT_DISPLAY_NAME" EXPECT_ID="$EXPECT_ID" \
	"$ROOT/scripts/check-app-bundle.sh" "$APP"
"$APP/Contents/MacOS/devilutionx" --version
echo "Built: $APP"
