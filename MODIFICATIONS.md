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
- **`0003-layered-world-zoom.patch` — Independent world zoom** (layered renderer; issue #4, milestone M1).
  - With *Upscale* on, the game world is drawn on its own layer, separately from the interface, at a whole number of screen pixels per game pixel. The interface keeps the size set by *Resolution*, while the world zooms in crisp steps. The hero stays centred above the control panel at every level.
  - On a 5K iMac (5760×3240 pixels) the levels are ×2 to ×12. Automatic zoom aims at about 540 game rows: ×6 on 5K, ×3 at 1440p, ×2 at 1080p. When the interface is letterboxed, the world fills the bars.
  - Half-transparent interface parts (store backgrounds, the transparent automap, info boxes) show the world through them. Death and pause still tint both the world and the panels red.
  - New options in *Settings → Graphics* (`[Graphics]` in `diablo.ini`):
    - *Independent Zoom* (`Independent Zoom`, on by default) turns the layered renderer on. Off gives exactly the previous picture.
    - *World Zoom* (`World Zoom`, default 0 = automatic): screen pixels per game pixel. It is written back to `diablo.ini` when you zoom with the keys or the wheel.
    - *World Zoom Min Height* and *World Zoom Max Height* (`World Zoom Min Height` = 240, `World Zoom Max Height` = 1620): the range of world heights, in game pixels, that the zoom levels may give. The maximum is a performance cap (×2 or more on 5K).
  - Keys, in layered mode:
    - **+** / **−** zoom the world in and out when the automap is closed. With the automap open they zoom the map, as before.
    - **Ctrl + mouse wheel** and **Cmd + mouse wheel** do the same (macOS may reserve Ctrl + wheel for its own screen zoom). One trackpad or Magic Mouse swipe steps once instead of running through all levels.
    - The *Zoom* key action (**Z** by default) and the gamepad *Zoom* action toggle between the current level and about twice it. The old *Zoom* option is not used in layered mode.
  - Mouse picking is the exact inverse of the world drawing, so clicks hit the tile, monster, item or door under the cursor at every level. Item labels and floating damage numbers stay on their objects.
  - Active only on SDL2 builds with *Upscale* on. Never active in headless mode or while a demo is recorded or played back (the *Independent Zoom* setting itself is left as it is). SDL1 and SDL3 builds and `Upscale=0` keep upstream behaviour. The save format, multiplayer messages and demo determinism are unchanged.
  - Setting `DEVILUTIONX_PERF_LOG=1` in the environment logs the average and 95th-percentile times of world drawing, interface drawing, colour expansion, present and the whole frame every 300 layered frames.
  - Russian strings added to `Translations/ru.po`.
  - Tests:
    - `test/world_view_test.cpp`: zoom levels, UI placement identical to SDL2's `UpdateLogicalSize`, coordinate round trips, the wheel limiter.
    - `test/world_picking_test.cpp`: picking at every 5K level, standing and walking.
    - `test/ui_layer_test.cpp`: the keyed interface layer and half-transparency (no blend result turns into a key colour), `RedBack`.
    - `test/layered_compose_test.cpp`: the CPU reference of the GPU composite matches the single-surface picture.
  - Deliberate, verified deviations from the text of issue #4:
    - Picking (§3.4) also subtracts `TILE_WIDTH / 2` in `WorldPointToTile`. With the issue's formula a tile's centre would pick a neighbouring tile.
    - The M1-T1 round-trip bounds are swapped in the issue. The tests use `ceil(s_u / ws)` for world → UI → world and `ceil(ws / s_u)` for UI → world → UI.
    - The M1-T2 random-pixel check shifts the diamond by the walking prediction, which the issue's oracle leaves out. The hero's ±1 UI pixel bound only holds while a world pixel is not bigger than a UI pixel.
  - Known limitations (later milestones):
    - Screenshots taken in layered mode show only the interface layer.
    - Floating info boxes are lighter over the world (50 % darkening instead of 87.5 %).
    - With an interface 640 pixels wide or less, opening a side panel still moves the mouse by 160 pixels, although the view no longer shifts. An in-game error dialog at that size shows stray pixels in palette colours 1 and 2 behind it.
