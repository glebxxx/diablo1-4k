# diablo1-4k

**Diablo 1 + Hellfire for macOS in 4K/5K**: a macOS-focused fork of [DevilutionX](https://github.com/diasurgical/DevilutionX), the open-source Diablo engine.

> 🇷🇺 **Diablo 1 + Hellfire на macOS в 4K/5K.** Форк открытого движка DevilutionX под macOS: нативная сборка, Retina/5K, крупный интерфейс, зум, бег героя, полностью русская версия.
> **Файлов игры в репозитории нет.** Нужна своя копия Diablo (например, [GOG](https://www.gog.com/game/diablo)).

**Status:** early work in progress. The base engine builds and runs natively on macOS (Intel and Apple Silicon), using only the Xcode Command Line Tools.

## Goals

| Feature | Status |
|---|---|
| Native macOS build (CLT only, all deps vendored, no Homebrew) | ✅ works |
| Retina / 5K resolution list with integer-scale hints (upstream bug #4348) | 🚧 in progress |
| Run in dungeons (single-player) | 🚧 in progress |
| Multi-level world zoom, with on-screen buttons on the side | 📝 designed |
| UI scaling: the interface fills the screen | 📝 designed |
| Pixel-art post-filters (Scale2x / MMPX) | 📝 planned |
| GPU real-time upscaling ("DLSS-like": FSR1 / MetalFX) | 🔬 research |
| Optional HD texture packs, built locally from your own game files | 🔬 research |
| Full Russian version (UI, texts, voice) | ✅ via DevilutionX `ru` + official `ru.mpq` voice pack |

## Build (macOS)

You need the Xcode Command Line Tools (`xcode-select --install`) and CMake 3.22 or newer.

```bash
./scripts/build.sh            # -> build/devilutionx.app
```

## Game data

Copy `DIABDAT.MPQ` (and, for Hellfire, `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, `hfvoice.mpq`) from **your own** copy of the game into:

```
~/Library/Application Support/diasurgical/devilution/
```

The GOG installer can be unpacked on a Mac with `innoextract` (`brew install innoextract`), without running it. See `scripts/install-gog-data.sh`.

For Russian text set `[Language] Code=ru` in `diablo.ini`. For Russian voices, also put DevilutionX's official [`ru.mpq`](https://github.com/diasurgical/devilutionx-assets/releases) in the same folder.

## Repository layout

- `devilutionx/` — engine source (DevilutionX at the commit in `UPSTREAM_COMMIT`, plus this fork's changes).
- `scripts/` — build and data helpers.
- `MODIFICATIONS.md` — list of changes made in this fork.
- `CLAUDE.md` / `AGENTS.md` — rules for AI coding agents working on this repo.

## Credits, license, disclaimer

- Engine: **DevilutionX** by the diasurgical team and contributors. This fork is based on their work.
- License: **Sustainable Use License v1.0** (see `LICENSE.md`). Use and distribution are free of charge and non-commercial only. This fork is modified; see `MODIFICATIONS.md`.
- Not affiliated with, endorsed by, or connected to Blizzard Entertainment or the DevilutionX project. *Diablo* and *Hellfire* are trademarks of their respective owners.
- **No game assets are included or will be accepted** (MPQs, extracted or upscaled art, audio, saves).
