#!/usr/bin/env bash
# Launch the locally built game, optionally with a display profile from config/.
#
# Usage:
#   scripts/run.sh [options] [-- extra game arguments]
#
# Options:
#   --profile NAME|FILE  apply config/NAME.ini (or any .ini fragment) first
#   --global             write the profile into your main diablo.ini (a timestamped
#                        backup is made first) instead of a separate profile folder
#   --app PATH           app bundle to run (default: build/Diablo 4K.app)
#   --dry-run            print what would happen, change nothing, do not launch
#   --list               list available profiles
#
# By default a profile gets its own config folder,
#   <PrefPath>/profiles/<NAME>/diablo.ini
# seeded once from your main diablo.ini, and the game is started with
# --config-dir pointing there. Saves and MPQs stay shared in PrefPath, and your
# main diablo.ini is never modified.
#
# Examples:
#   scripts/run.sh --profile 1080p-x3
#   scripts/run.sh --profile 540p-x6 -- -n --hellfire
#
# Environment: DVX_PREF_DIR overrides PrefPath (default:
#   ~/Library/Application Support/diasurgical/devilution).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/Diablo 4K.app"
PREF="${DVX_PREF_DIR:-$HOME/Library/Application Support/diasurgical/devilution}"
profile=""
global=0
dry_run=0
game_args=()

die() {
	echo "run: $*" >&2
	exit 1
}

while [ $# -gt 0 ]; do
	case "$1" in
	--profile)
		[ $# -ge 2 ] || die "--profile needs a name"
		profile="$2"
		shift 2
		;;
	--global)
		global=1
		shift
		;;
	--app)
		[ $# -ge 2 ] || die "--app needs a path"
		APP="$2"
		shift 2
		;;
	--dry-run)
		dry_run=1
		shift
		;;
	--list)
		for f in "$ROOT"/config/*.ini; do
			[ -e "$f" ] || continue
			n="$(basename "$f" .ini)"
			d="$(sed -n 's/^; *diablo1-4k display profile: *//p' "$f" | head -1)"
			printf '  %-12s %s\n' "$n" "$d"
		done
		exit 0
		;;
	-h | --help)
		sed -n '2,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	--)
		shift
		game_args+=("$@")
		break
		;;
	*)
		game_args+=("$1")
		shift
		;;
	esac
done

BIN="$APP/Contents/MacOS/devilutionx"
[ -x "$BIN" ] || die "no game binary at $BIN; build first with scripts/build.sh"

if [ "${#game_args[@]}" -gt 0 ]; then
	for a in "${game_args[@]}"; do
		if [ "$a" = "--data-dir" ]; then
			echo "run: warning: --data-dir replaces the base path, so the bundled Hellfire mod is not found." >&2
			echo "run: warning: put the MPQs into: $PREF" >&2
		fi
	done
fi

# Validate an ini fragment: sections, key=value lines, ';' comments only;
# booleans strictly 0/1 (any other value makes the game abort at start-up).
validate_fragment() {
	awk '
		function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t\r]+$/, "", s); return s }
		BEGIN {
			split("Fullscreen|Upscale|Fit to Screen|Integer Scaling|Zoom|Per-pixel Lighting|Color Cycling|Show FPS|Hardware Cursor|Hardware Cursor For Items|Alternate nest art", b, "|")
			for (i in b) isbool[b[i]] = 1
			split("Width|Height|Scaling Quality|Frame Rate Control|Hardware Cursor Maximum Size", n, "|")
			for (i in n) isint[n[i]] = 1
			bad = 0; sec = ""
		}
		{ line = trim($0) }
		line == "" || line ~ /^;/ { next }
		line ~ /^\[[^]]+\]$/ { sec = line; next }
		{
			eq = index(line, "=")
			if (eq == 0 || sec == "") { printf "line %d: not a key=value inside a [section]: %s\n", NR, line; bad = 1; next }
			k = trim(substr(line, 1, eq - 1)); v = trim(substr(line, eq + 1))
			if ((k in isbool) && v != "0" && v != "1") { printf "line %d: %s must be 0 or 1, got \"%s\"\n", NR, k, v; bad = 1 }
			if ((k in isint) && v !~ /^[0-9]+$/) { printf "line %d: %s must be a number, got \"%s\"\n", NR, k, v; bad = 1 }
		}
		END { exit bad }
	' "$1"
}

# Merge an ini fragment into an ini file (keys replaced in place, missing keys
# appended to their section, missing sections appended). Keeps the target's
# line endings (DevilutionX writes CRLF).
merge_ini() {
	local fragment="$1" target="$2" tmp
	tmp="$(mktemp "${target}.XXXXXX")"
	awk '
		function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t\r]+$/, "", s); return s }
		function flush(s,   i, k) {
			for (i = 1; i <= nkeys[s]; i++) {
				k = keys[s, i]
				if (!((s, k) in done)) { printf "%s=%s%s", k, val[s, k], eol; done[s, k] = 1 }
			}
		}
		FNR == NR {
			line = trim($0)
			if (line == "" || line ~ /^;/) next
			if (line ~ /^\[[^]]+\]$/) {
				s = substr(line, 2, length(line) - 2)
				if (!(s in seen)) { seen[s] = 1; order[++nsec] = s }
				next
			}
			eq = index(line, "=")
			k = trim(substr(line, 1, eq - 1))
			if (!((s, k) in val)) keys[s, ++nkeys[s]] = k
			val[s, k] = trim(substr(line, eq + 1))
			next
		}
		FNR == 1 { eol = ($0 ~ /\r$/) ? "\r\n" : "\n" }
		{
			raw = $0; line = trim(raw)
			if (line == "") { pending = pending raw "\n"; next }
			if (line ~ /^\[[^]]+\]$/) {
				if (cur != "") flush(cur)
				printf "%s", pending; pending = ""
				cur = substr(line, 2, length(line) - 2); present[cur] = 1
				print raw; next
			}
			printf "%s", pending; pending = ""
			eq = index(line, "=")
			if (cur != "" && eq > 0 && line !~ /^;/) {
				k = trim(substr(line, 1, eq - 1))
				if ((cur, k) in val) { printf "%s=%s%s", k, val[cur, k], eol; done[cur, k] = 1; next }
			}
			print raw
		}
		END {
			if (eol == "") eol = "\n"
			if (cur != "") flush(cur)
			printf "%s", pending
			for (i = 1; i <= nsec; i++) {
				s = order[i]
				if (!(s in present)) { printf "[%s]%s", s, eol; flush(s) }
			}
		}
	' "$fragment" "$target" >"$tmp"
	mv "$tmp" "$target"
}

config_args=()
if [ -n "$profile" ]; then
	if [ -f "$profile" ]; then
		fragment="$profile"
		name="$(basename "$profile" .ini)"
	else
		fragment="$ROOT/config/$profile.ini"
		name="$profile"
		[ -f "$fragment" ] || die "unknown profile '$profile' (try --list)"
	fi
	[[ "$name" =~ ^[A-Za-z0-9._-]+$ ]] || die "profile name must be [A-Za-z0-9._-]: $name"
	validate_fragment "$fragment" || die "invalid profile: $fragment"

	if [ "$global" -eq 1 ]; then
		target_dir="$PREF"
	else
		target_dir="$PREF/profiles/$name"
		config_args=(--config-dir "$target_dir")
	fi
	target="$target_dir/diablo.ini"

	if [ "$dry_run" -eq 1 ]; then
		echo "would apply $fragment -> $target"
	else
		mkdir -p "$target_dir"
		if [ "$global" -eq 1 ] && [ -f "$target" ]; then
			backup="$target.bak-$(date +%Y%m%d-%H%M%S)"
			cp -p "$target" "$backup"
			echo "backup: $backup"
		fi
		if [ ! -f "$target" ]; then
			if [ "$global" -eq 0 ] && [ -f "$PREF/diablo.ini" ]; then
				cp -p "$PREF/diablo.ini" "$target"
			else
				: >"$target"
			fi
		fi
		merge_ini "$fragment" "$target"
		echo "profile '$name' applied to $target"
	fi
fi

if [ -f "./diablo.ini" ]; then
	echo "run: warning: ./diablo.ini exists in the current folder; DevilutionX then runs in portable mode and uses it." >&2
fi

cmd=("$BIN")
if [ "${#config_args[@]}" -gt 0 ]; then
	cmd+=("${config_args[@]}")
fi
if [ "${#game_args[@]}" -gt 0 ]; then
	cmd+=("${game_args[@]}")
fi
if [ "$dry_run" -eq 1 ]; then
	printf 'would run:'
	printf ' %q' "${cmd[@]}"
	printf '\n'
	exit 0
fi
exec "${cmd[@]}"
