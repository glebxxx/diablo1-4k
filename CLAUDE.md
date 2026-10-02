# Rules for AI agents working on diablo1-4k

This repo is a macOS-focused fork of DevilutionX, the open-source engine for Diablo 1 + Hellfire.

- **Engine code:** `devilutionx/`.
- **Upstream base:** `UPSTREAM_COMMIT`.
- **Product goals:** see `README.md`.
- **Maintainer workflow:** a human plays on an Intel iMac with a Retina 5K panel and real game data. Cloud agents never have game data.

## Hard rules

### Game data and licensing
- **Never commit game data.** Forbidden:
  - `*.mpq` / `*.MPQ`;
  - art extracted from the game;
  - AI-upscaled game art;
  - audio from the game;
  - saves from real games.

  Game assets are Blizzard's copyright, and this repo is public.
- **For tests use the shareware data**, exactly like upstream CI:
  `wget -qnc https://github.com/diasurgical/devilutionx-assets/releases/download/v2/spawn.mpq -P <build-dir>`
  Never commit it.
- **Run `scripts/check-no-game-data.sh` before every commit.**
- **License:** DevilutionX "Sustainable Use License" (`LICENSE.md`).
  - Keep upstream headers and notices.
  - List every user-visible modification in `MODIFICATIONS.md`.
  - No GPL code (e.g. xBRZ) in this repo.

### Engine code
- Keep diffs minimal and in upstream style: `devilutionx/.clang-format`, C++23, keep `#ifdef USE_SDL3` symmetry.
- **New behaviour goes behind options** in `Source/options.{h,cpp}`, with translatable `N_()` strings.
- Default and non-HiDPI behaviour must stay identical unless the task says otherwise.
- **Tests:** ship tests where feasible (`devilutionx/test/`, gtest), and keep the existing suite green. That includes the timedemo fixture `test/fixtures/timedemo/WarriorLevel1to2`.
- **Multiplayer safety:** gameplay changes must be single-player only (`gbIsMultiplayer`). They must not change the save format or demo determinism.

## Build and test

**Linux (cloud):** use the upstream CI as reference (`devilutionx/.github/workflows/Linux_x86_64_test.yml`).

```bash
cmake -S devilutionx -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build
wget -qnc https://github.com/diasurgical/devilutionx-assets/releases/download/v2/spawn.mpq -P build
ctest --test-dir build --output-on-failure
```

**macOS:** use `scripts/build.sh`. It runs with Command Line Tools only, and all dependencies are vendored.
- Never `brew install sdl2`: it is sdl2-compat over SDL3.
- Always pass `-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3`.

## Workflow

- **One task = one branch = one PR** against `main`. The branch is named `agent/<issue-number>-<slug>`, and the PR references the issue.
- **Every PR description says how to test it on a Mac:** which option or key to use, and what should be visible on screen.
- **Do not** touch unrelated files or reformat upstream code.

## macOS facts worth knowing

- **Config, saves and MPQs** live in `~/Library/Application Support/diasurgical/devilution/`.
- **Never use `--data-dir` on macOS.** It replaces BasePath, so the bundled Hellfire mod (`Contents/Resources/mods/hf`) is no longer found.
- **Retina:** SDL2 reports display modes in points, while rendering and integer scaling work in backing pixels. Upstream issue #4348: the resolution list is multiplied by the DPI factor.
