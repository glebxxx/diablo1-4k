#!/usr/bin/env bash
# Extract the game data from YOUR OWN GOG "Diablo + Hellfire" installer and install it for DevilutionX on macOS.
# The installer is NOT executed: innoextract only unpacks it.
# Usage: scripts/install-gog-data.sh "/path/to/setup_diablo_1.09_hellfire_v4_(78466).exe"
set -euo pipefail

INSTALLER="${1:?usage: $0 /path/to/setup_diablo_*.exe}"
DEST="${DEST:-$HOME/Library/Application Support/diasurgical/devilution}"

command -v innoextract >/dev/null || { echo "innoextract not found: brew install innoextract" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

innoextract -e -p0 -m -d "$TMP" \
	-I /DIABDAT.MPQ \
	-I /hellfire/hellfire.mpq -I /hellfire/hfmonk.mpq -I /hellfire/hfmusic.mpq -I /hellfire/hfvoice.mpq \
	"$INSTALLER"

mkdir -p "$DEST"
cp "$TMP/DIABDAT.MPQ" "$DEST/"
for f in hellfire hfmonk hfmusic hfvoice; do
	if [ -f "$TMP/hellfire/$f.mpq" ]; then
		cp "$TMP/hellfire/$f.mpq" "$DEST/"
	fi
done

# Known-good MD5 of DIABDAT.MPQ (GOG / CD 1.09): 011bc6518e6166206231080a4440b373 (or 68f049866b44688a7af65ba766bef75a).
md5 -r "$DEST"/*.MPQ "$DEST"/*.mpq 2>/dev/null || true
echo "Installed game data into: $DEST"
