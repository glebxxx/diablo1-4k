#!/usr/bin/env bash
# Check the name, bundle id and executable in an app bundle's Info.plist.
#
# Usage:
#   scripts/check-app-bundle.sh APP_OR_INFO_PLIST
#
# Environment (defaults: the fork's values, see DIABLO4K_BUNDLE_* in devilutionx/CMakeLists.txt):
#   EXPECT_NAME          CFBundleName (default: Diablo 4K)
#   EXPECT_DISPLAY_NAME  CFBundleDisplayName (default: EXPECT_NAME)
#   EXPECT_ID            CFBundleIdentifier (default: io.github.glebxxx.diablo1-4k)
#
# Uses plutil on macOS and python3 elsewhere.
set -euo pipefail

[ $# -eq 1 ] || {
	sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' >&2
	exit 64
}
PLIST="$1"
[ -d "$PLIST" ] && PLIST="$PLIST/Contents/Info.plist"
[ -f "$PLIST" ] || {
	echo "check-app-bundle: no Info.plist at $PLIST" >&2
	exit 1
}

EXPECT_NAME="${EXPECT_NAME:-Diablo 4K}"
EXPECT_DISPLAY_NAME="${EXPECT_DISPLAY_NAME:-$EXPECT_NAME}"
EXPECT_ID="${EXPECT_ID:-io.github.glebxxx.diablo1-4k}"

read_key() {
	if command -v plutil >/dev/null; then
		plutil -extract "$1" raw -o - "$PLIST"
	else
		python3 -c 'import plistlib, sys; print(plistlib.load(open(sys.argv[1], "rb"))[sys.argv[2]])' "$PLIST" "$1"
	fi
}

fail=0
check() {
	local actual
	actual="$(read_key "$1" 2>/dev/null || true)"
	if [ "$actual" = "$2" ]; then
		echo "ok    $1 = $actual"
	else
		echo "FAIL  $1 = '$actual', expected '$2'" >&2
		fail=1
	fi
}

check CFBundleName "$EXPECT_NAME"
check CFBundleDisplayName "$EXPECT_DISPLAY_NAME"
check CFBundleIdentifier "$EXPECT_ID"
check CFBundleExecutable devilutionx
exit "$fail"
