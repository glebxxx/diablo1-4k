#!/usr/bin/env bash
# Recreate devilutionx/ from upstream DevilutionX at the pinned commit:
#   1. export the commit named in UPSTREAM_COMMIT (git archive, no history);
#   2. delete every path listed in scripts/prune-list.txt;
#   3. apply patches/*.patch in lexical order (git apply, all-or-nothing);
#   4. copy the result into devilutionx/, touching only files whose content
#      changed, so incremental builds stay incremental.
#
# The result depends only on (UPSTREAM_COMMIT, prune list, patches), so the
# script is idempotent: a second run reports "already in sync".
#
# Usage:
#   scripts/sync-upstream.sh            # update devilutionx/
#   scripts/sync-upstream.sh --check    # exit 1 if devilutionx/ is out of date
#   scripts/sync-upstream.sh --allow-missing   # tolerate prune entries that
#                                              # no longer exist upstream
#
# Environment:
#   UPSTREAM_REPO  local clone or URL of DevilutionX
#                  (default: https://github.com/diasurgical/DevilutionX.git).
#                  A local clone that already has the commit avoids network use.
#   KEEP_STAGE=1   keep the temporary staging tree for inspection.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_REPO="${UPSTREAM_REPO:-https://github.com/diasurgical/DevilutionX.git}"
PRUNE_LIST="$ROOT/scripts/prune-list.txt"
PATCH_DIR="$ROOT/patches"
DEST="$ROOT/devilutionx"
CACHE_GIT="$ROOT/work/upstream.git"

mode=sync
allow_missing=0
for arg in "$@"; do
	case "$arg" in
	--check) mode=check ;;
	--allow-missing) allow_missing=1 ;;
	-h | --help)
		sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		echo "unknown argument: $arg" >&2
		exit 64
		;;
	esac
done

die() {
	echo "sync-upstream: $*" >&2
	exit 1
}

command -v git >/dev/null || die "git not found"
command -v rsync >/dev/null || die "rsync not found"

COMMIT="$(tr -d '[:space:]' <"$ROOT/UPSTREAM_COMMIT")"
[[ "$COMMIT" =~ ^[0-9a-f]{40}$ ]] || die "UPSTREAM_COMMIT must hold a full 40-character commit hash"
[ -f "$PRUNE_LIST" ] || die "missing $PRUNE_LIST"

# Stage outside the repository: `git apply` run inside another repository's
# work tree silently skips paths outside the current directory.
STAGE_PARENT="$(mktemp -d "${TMPDIR:-/tmp}/dvx-sync.XXXXXX")"
cleanup() {
	if [ "${KEEP_STAGE:-0}" = 1 ]; then
		echo "staging tree kept at: $STAGE_PARENT"
	else
		rm -rf "$STAGE_PARENT"
	fi
}
trap cleanup EXIT
STAGE="$STAGE_PARENT/devilutionx"
mkdir -p "$STAGE"
export GIT_CEILING_DIRECTORIES="$STAGE_PARENT"

# --- 1. export the pinned upstream commit -----------------------------------
archive_from() { git -C "$1" archive --format=tar "$COMMIT" | tar -x -C "$STAGE"; }

if [ -d "$UPSTREAM_REPO" ] && git -C "$UPSTREAM_REPO" cat-file -e "$COMMIT^{commit}" 2>/dev/null; then
	echo "upstream: $COMMIT from local clone $UPSTREAM_REPO"
	archive_from "$UPSTREAM_REPO"
else
	echo "upstream: fetching $COMMIT from $UPSTREAM_REPO"
	if [ ! -d "$CACHE_GIT" ]; then
		git init --quiet --bare "$CACHE_GIT"
	fi
	if ! git -C "$CACHE_GIT" cat-file -e "$COMMIT^{commit}" 2>/dev/null; then
		git -C "$CACHE_GIT" fetch --quiet --depth 1 "$UPSTREAM_REPO" "$COMMIT"
	fi
	archive_from "$CACHE_GIT"
fi
[ -f "$STAGE/CMakeLists.txt" ] || die "export of $COMMIT looks empty"

# --- 2. prune ---------------------------------------------------------------
pruned=0
missing=0
while IFS= read -r line || [ -n "$line" ]; do
	entry="${line%%#*}"
	entry="${entry%"${entry##*[![:space:]]}"}"
	entry="${entry#"${entry%%[![:space:]]*}"}"
	[ -n "$entry" ] || continue
	case "$entry" in
	/* | *..* | *'*'* | *'?'* | *'['*) die "unsafe prune entry: '$entry'" ;;
	esac
	target="$STAGE/${entry%/}"
	if [ -e "$target" ] || [ -L "$target" ]; then
		rm -rf "$target"
		pruned=$((pruned + 1))
	else
		echo "warning: prune entry not found upstream: $entry" >&2
		missing=$((missing + 1))
	fi
done <"$PRUNE_LIST"
if [ "$missing" -gt 0 ] && [ "$allow_missing" -eq 0 ]; then
	die "$missing prune entries no longer exist upstream; update scripts/prune-list.txt or pass --allow-missing"
fi
echo "pruned: $pruned entries"

# --- 3. patches -------------------------------------------------------------
patches=()
if [ -d "$PATCH_DIR" ]; then
	while IFS= read -r p; do
		patches+=("$p")
	done < <(find "$PATCH_DIR" -maxdepth 1 -type f -name '*.patch' | LC_ALL=C sort)
fi
if [ "${#patches[@]}" -gt 0 ]; then
	for p in "${patches[@]}"; do
		(cd "$STAGE" && git apply --check "$p") || die "patch does not apply: ${p#"$ROOT"/}"
		(cd "$STAGE" && git apply --whitespace=nowarn "$p")
		echo "applied: ${p#"$ROOT"/}"
	done
else
	echo "patches: none"
fi

# --- 4. compare / install ---------------------------------------------------
if [ "$mode" = check ]; then
	if [ -d "$DEST" ] && diff -rq "$STAGE" "$DEST" >/dev/null; then
		echo "devilutionx/ is in sync with $COMMIT + prune list + ${#patches[@]} patch(es)"
		exit 0
	fi
	echo "devilutionx/ is OUT OF SYNC; differences:" >&2
	diff -rq "$STAGE" "$DEST" >&2 || true
	exit 1
fi

if [ -d "$DEST" ] && diff -rq "$STAGE" "$DEST" >/dev/null; then
	echo "devilutionx/ already in sync, nothing to do"
	exit 0
fi

mkdir -p "$DEST"
# -c: compare by content; no -t: changed files get a fresh mtime so that
# make/ninja rebuild them (archive mtimes are older than build products).
rsync -rlpc --delete "$STAGE/" "$DEST/"
echo "devilutionx/ updated to $COMMIT + prune list + ${#patches[@]} patch(es)"

# Upstream's own devilutionx/.gitignore must not hide upstream files from git.
if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	hidden="$(git -C "$ROOT" ls-files --others --ignored --exclude-standard -- devilutionx | head -20)"
	if [ -n "$hidden" ]; then
		echo "warning: these synced files are ignored by .gitignore and would not be committed:" >&2
		echo "$hidden" >&2
	fi
fi
