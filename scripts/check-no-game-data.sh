#!/usr/bin/env bash
# Fails if game data is tracked or staged in this repository.
# Blizzard's game files must never be committed: no MPQ archives, and no extracted, upscaled or recorded assets.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail=0
files="$( { git ls-files; git diff --cached --name-only; } | sort -u)"

# 1. MPQ archives anywhere.
if grep -iE '\.mpq$' <<<"$files"; then
	echo "ERROR: MPQ archives must not be committed." >&2
	fail=1
fi

# 2. Working folders for extracted/upscaled art.
if grep -E '^(work|gamedata|extracted|upscaled|hd-packs?)/' <<<"$files"; then
	echo "ERROR: extracted or upscaled game assets must not be committed." >&2
	fail=1
fi

# 3. Saves and demos outside upstream test fixtures.
if grep -iE '\.(sv|hsv|dmo)$' <<<"$files" | grep -v '^devilutionx/test/fixtures/'; then
	echo "ERROR: savegames/demos are only allowed under devilutionx/test/fixtures/." >&2
	fail=1
fi

# 4. Unexpectedly large files (> 5 MB).
while IFS= read -r f; do
	[ -f "$f" ] || continue
	# wc -c works on both macOS and GNU/Linux (GNU `stat -f` prints file-system info, not the file size).
	size=$(wc -c <"$f" | tr -d ' ')
	if [ "$size" -gt 5242880 ]; then
		echo "ERROR: $f is larger than 5 MB ($size bytes); game data?" >&2
		fail=1
	fi
done <<<"$files"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "OK: no game data found."
