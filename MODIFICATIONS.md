# Modifications

This repository is a **modified** version of [DevilutionX](https://github.com/diasurgical/DevilutionX).
Base: upstream `master` at commit `452eeccc7460d5bd427ca21c9e47f89b953d9e28` (see `UPSTREAM_COMMIT`), version 1.6.0-dev.

`devilutionx/` is generated: every engine change is kept as a patch in `patches/`. `scripts/sync-upstream.sh` builds the tree in three steps:

1. exports the upstream commit named in `UPSTREAM_COMMIT`;
2. removes the paths listed in `scripts/prune-list.txt`;
3. applies `patches/*.patch` in order.

`scripts/sync-upstream.sh --check` verifies that `devilutionx/` matches that recipe.

Every user-visible change made in this fork is listed below.

## Changes

### Removed from the upstream tree

- **Files for platforms other than macOS** (`scripts/prune-list.txt`): the Android and UWP projects, packaging for consoles, handhelds, Linux distributions and Windows installers, CMake toolchain files for other platforms, upstream CI and dev-container configs, Windows-only third-party glue (`3rdParty/tolk`, `3rdParty/find_steam_game`), and docs and tools for other operating systems or hosted services.
  - `Source/` is never pruned: platform code there is behind `#ifdef`, and our patches must keep applying to pristine upstream sources.
  - `LICENSE.md`, upstream's `README.md` and `Packaging/resources/` (third-party license texts) are kept.
- **`Packaging/resources/shareware-startup.wav`**: audio from the Diablo shareware release (Blizzard). Upstream uses it only for the Nintendo 3DS banner. We do not carry it.

### Engine changes (`patches/`)

- **`0001-hidpi-resolution-list.patch` — HiDPI/Retina resolution list** (fixes upstream issue [#4348](https://github.com/diasurgical/DevilutionX/issues/4348)).
  - On HiDPI displays (macOS Retina) with upscaling on, the *Settings → Graphics → Resolution* list was built from display modes multiplied by the pixel density. Entries came out doubled: on a 5K iMac the list ran up to "5760p", which rendered at 10240×5760 and showed a tiny picture.
  - The list is now built from the desktop size in output pixels: exact integer fractions of it, plus common heights at its aspect ratio. Nothing larger than the screen is offered, except the resolution already set in `diablo.ini`, which stays in the list so it remains selected. Entries that scale to the screen exactly carry their factor, e.g. `1920x1080 (x3)`, or `1080p (x3)` with *Fit to Screen*.
  - *Fit to Screen* together with *Integer Scaling* now computes the scale factor in output pixels instead of points. On SDL2 the pixel density is only known once the renderer exists, so the preferred window size is applied again at that point (the renderer is not recreated).
  - The list logic moved into a pure function, `BuildResolutionList` (`Source/utils/resolution_list.{hpp,cpp}`), covered by `test/resolution_list_test.cpp`. Non-HiDPI displays get the same list as before.
- **`0002-run-in-dungeons.patch` — Run in Dungeons** (single player).
  - New option *Settings → Gameplay → Run in Dungeons* (`[Game] Run in Dungeons` in `diablo.ini`, on by default). With it, the hero uses the fast "jogging" walk outside of town too. Upstream offers this only in town (*Run in Town*).
  - New key action *Toggle run in dungeons*, bound to **J** by default (rebind it in *Settings → Keyboard*). It shows "Running in dungeons enabled/disabled".
  - Single player only: the option has no effect in multiplayer games (every client simulates every player's walk speed) or while a demo is recorded or played back. Town behaviour, the save format and demo determinism are unchanged. Walking sounds are muted while running, as upstream does in town.
  - Russian strings added to `Translations/ru.po`. Covered by `test/player_test.cpp` (`IsRunningEnabledOnCurrentLevel`).

### Tools outside the engine

- **`tools/assets/` — graphics codecs and HD texture pack tools** (issue [#3](https://github.com/glebxxx/diablo1-4k/issues/3)). A new Python package, written for this fork; it contains no upstream code and no game data, and the engine is unchanged.
  - Reads and writes MPQ, PAL, TRN, CEL, CL2, CLX, level CEL/MIN/TIL and PCX. Quantizes upscaled images back to the palette, by nearest colour or per light ramp.
  - Defines the HD texture pack format (`tools/assets/HDPACK.md`) and ships a validator (`d1-hdpack`).
  - CI: `.github/workflows/tools-assets.yml` runs ruff and pytest.
