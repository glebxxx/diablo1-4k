# Building and running on macOS

The app builds natively on Intel and Apple Silicon Macs with only Apple's Command Line Tools. CMake downloads and statically links every third-party library (see `NOTICE.md`), so Homebrew is optional.

## Requirements

- **Xcode Command Line Tools:** `xcode-select --install`.
- **CMake 3.22 or newer**, e.g. from cmake.org or `brew install cmake`.
- **gettext**, only for translations (the Russian UI included): `brew install gettext`. Without `msgfmt` the build still succeeds, but the game is English-only and `scripts/build.sh` prints a warning.
- **Internet access while configuring.** CMake fetches SDL2, libpng, Lua and the other libraries listed in `NOTICE.md`.
- Optional: `shellcheck` for linting the scripts, `innoextract` for unpacking a GOG installer.

**Do not** `brew install sdl2`. Homebrew's `sdl2` is now sdl2-compat on top of SDL3. The build uses its own pinned SDL2 instead.

## Build

```bash
scripts/build.sh                    # Release build -> build/Diablo 4K.app
scripts/build.sh RelWithDebInfo     # other build types
BUILD_DIR=/tmp/dvx scripts/build.sh # another build folder
scripts/build.sh Release -DUSE_SDL3=ON   # extra CMake arguments are passed through
```

The script:

1. configures `devilutionx/` with `-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3`, `-DBUILD_TESTING=OFF` and every `DEVILUTIONX_SYSTEM_*` library turned off;
2. builds with all CPU cores;
3. copies `build/devilutionx.app` to `build/Diablo 4K.app` (the name comes from the bundle's `CFBundleName`);
4. signs that bundle ad hoc (`codesign --sign -`), verifies the signature and checks its `Info.plist` with `scripts/check-app-bundle.sh`;
5. prints `devilutionx --version`.

### App name and bundle id

The fork's app is called **Diablo 4K** (`CFBundleName`, `CFBundleDisplayName`), with the bundle id `io.github.glebxxx.diablo1-4k`. Both are CMake cache variables:

```bash
scripts/build.sh Release -DDIABLO4K_BUNDLE_NAME=devilutionx -DDIABLO4K_BUNDLE_ID=com.diasurgical.devilutionx   # upstream values
```

The bundle inside the build tree is always `devilutionx.app`, and the executable is always `Contents/MacOS/devilutionx`. Settings, saves and MPQs stay in `~/Library/Application Support/diasurgical/devilution/`: that folder does not depend on the app name or bundle id. macOS treats a new bundle id as a new app, so permission prompts (e.g. Input Monitoring for controllers), "Saved Application State" and the Dock entry start fresh.

A clean build takes a few minutes. The app targets macOS 13.3 or newer and the architecture of the building Mac.

Check that translations were built:

```bash
ls "build/Diablo 4K.app/Contents/Resources/ru.gmo"
```

## Game data

The game needs **your own** copy of Diablo (and, for Hellfire, the expansion). No game files are in this repository.

1. Put `DIABDAT.MPQ` (and `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, `hfvoice.mpq`) into
   `~/Library/Application Support/diasurgical/devilution/`.
   For the GOG installer, `scripts/install-gog-data.sh /path/to/setup_diablo_*.exe` unpacks it with `innoextract`, without running it.
2. Optional, Russian voices: put DevilutionX's official [`ru.mpq`](https://github.com/diasurgical/devilutionx-assets/releases) into the same folder and set `[Language] Code=ru` in `diablo.ini`.

**Do not use `--data-dir` on macOS.** It replaces the base path, so the Hellfire mod bundled inside the app (`Contents/Resources/mods/hf`) is no longer found.

Settings live in `~/Library/Application Support/diasurgical/devilution/diablo.ini`. The game rewrites this file when it quits, so edit it only while the game is closed.

## Run

```bash
open "build/Diablo 4K.app"       # like double-clicking it
scripts/run.sh                    # same, from the terminal
scripts/run.sh --list             # list display profiles
scripts/run.sh --profile 1080p-x3 # run with a display profile
```

`scripts/run.sh --help` lists every option.

### Display profiles (`config/*.ini`)

A profile is a small `diablo.ini` fragment. By default, `run.sh --profile NAME`:

- copies your main `diablo.ini` once into `<PrefPath>/profiles/NAME/diablo.ini`;
- applies the fragment to that copy;
- starts the game with `--config-dir` pointing there.

Saves and MPQs stay shared, and your main `diablo.ini` is not modified. `--global` writes the fragment into the main `diablo.ini` instead, after making a timestamped backup.

| Profile | Game resolution | Scale | For |
|---|---|---|---|
| `1080p-x3` | 1920×1080 | ×3 | 5K iMac, display set to "looks like 2880 × 1620" (5760 × 3240 pixels). Recommended. |
| `810p-x4` | 1440×810 | ×4 | same display, bigger pixels |
| `540p-x6` | 960×540 | ×6 | same display, closest to the original view |
| `540p-x2` | 960×540 | ×2 | external 1920 × 1080 monitor |

All of them use fullscreen, *Integer Scaling* and nearest-pixel scaling, so every game pixel becomes an exact square of screen pixels. With integer scaling the factor follows from the pixels macOS renders: at the 5K iMac's default "looks like 2560 × 1440" (5120 × 2880 pixels), 1920×1080 only fits ×2 and gets black borders. On that setting, pick a resolution marked `(x2)`, `(x4)` and so on in *Settings → Graphics → Resolution*. That list comes from `patches/0001`.

## Tests

Unit tests use GoogleTest, which the build downloads (`DEVILUTIONX_SYSTEM_GOOGLETEST=OFF`; by default CMake looks for an installed copy). On macOS, test binaries load assets from `devilutionx.app/Contents/Resources` in the same build folder, so build the `devilutionx` target too:

```bash
cmake -S devilutionx -B build-tests -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 -DDEVILUTIONX_SYSTEM_SDL2=OFF -DDEVILUTIONX_SYSTEM_SDL_IMAGE=OFF \
  -DDEVILUTIONX_SYSTEM_LIBPNG=OFF -DDEVILUTIONX_SYSTEM_LIBSODIUM=OFF -DDEVILUTIONX_SYSTEM_LUA=OFF \
  -DDEVILUTIONX_SYSTEM_GOOGLETEST=OFF -DDEVILUTIONX_SYSTEM_BENCHMARK=OFF
cmake --build build-tests -j "$(sysctl -n hw.ncpu)" \
  --target devilutionx resolution_list_test player_test timedemo_test
ctest --test-dir build-tests -R 'ResolutionListTest|Player\.|Timedemo' --output-on-failure
```

Known upstream issues at `452eeccc` on macOS:

- `crawl_test`, `ini_test` and `path_test` do not link: they need gmock, which the vendored GoogleTest build does not provide. Build specific targets as above, or link them with `GTest::gmock`.
- Tests that need game assets (UI tests, for example) require the shareware `spawn.mpq` in the build folder. Download it as upstream CI does, and never commit it:
  `curl -fLO --output-dir build-tests https://github.com/diasurgical/devilutionx-assets/releases/download/v2/spawn.mpq`
- `ctest -R <target name>` matches nothing, because tests are registered by their GoogleTest names (`Suite.Test`).

## Updating the engine source

`devilutionx/` is generated from three inputs:

1. the upstream commit in `UPSTREAM_COMMIT`;
2. the prune list `scripts/prune-list.txt` (files for other platforms);
3. the patches in `patches/`, applied in name order.

```bash
scripts/sync-upstream.sh           # regenerate devilutionx/
scripts/sync-upstream.sh --check   # verify it is in sync (exit 1 if not)
UPSTREAM_REPO=/path/to/DevilutionX scripts/sync-upstream.sh   # use a local clone instead of GitHub
```

Without `UPSTREAM_REPO`, the script fetches the commit once into `work/upstream.git`, which git ignores.

**To change engine code**, edit `devilutionx/`, then save the change as a new patch:

```bash
git add -A devilutionx
git diff --cached --relative=devilutionx > patches/0003-short-name.patch
scripts/sync-upstream.sh --check   # must say "in sync"
```

List the change in `MODIFICATIONS.md`.

**To move to a newer upstream commit:**

1. put the new hash into `UPSTREAM_COMMIT`;
2. run `scripts/sync-upstream.sh`;
3. if a patch no longer applies, rebase it by hand;
4. if a prune entry no longer exists upstream, update `scripts/prune-list.txt`. `--allow-missing` skips such entries for one run.

Before committing, run `scripts/check-no-game-data.sh`.
