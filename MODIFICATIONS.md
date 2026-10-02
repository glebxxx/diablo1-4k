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
