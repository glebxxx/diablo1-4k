#!/usr/bin/env bash
# Build DevilutionX (this fork) natively on macOS with only the Xcode Command Line Tools.
# All third-party dependencies are fetched by CMake and linked statically, so no Homebrew packages are needed.
# Usage: scripts/build.sh [Release|RelWithDebInfo|Debug] [extra cmake args...]
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

APP="$BUILD_DIR/devilutionx.app"
# Ad-hoc signature so the bundle has a valid seal on this machine.
codesign --force --deep --sign - "$APP"
"$APP/Contents/MacOS/devilutionx" --version
echo "Built: $APP"
