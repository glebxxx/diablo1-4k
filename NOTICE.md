# Third-party notices

The engine in `devilutionx/` is [DevilutionX](https://github.com/diasurgical/DevilutionX), released under the Sustainable Use License (`LICENSE.md`). This fork's changes are listed in `MODIFICATIONS.md`.

DevilutionX builds on the libraries below. **None of them is stored in this repository.** CMake downloads them while configuring the build (FetchContent). The exact source URL and commit of each one are in `devilutionx/3rdParty/<name>/CMakeLists.txt`. Each library stays under its own license, and its full license text comes with the downloaded source.

The licenses below were checked against the `LICENSE`/`COPYING` files of the downloaded sources at upstream commit `452eeccc`.

## Linked into the macOS app built by `scripts/build.sh`

| Component | Version / source | License |
|---|---|---|
| [SDL2](https://github.com/libsdl-org/SDL) | 2.32.8 release | zlib |
| [SDL_image](https://github.com/libsdl-org/SDL_image) | 2.0.5 | zlib |
| [SDL_audiolib](https://github.com/realnc/SDL_audiolib) | commit `cc1bb6a` | LGPL-3.0-or-later |
| [libsmackerdec](https://github.com/diasurgical/libsmackerdec) | diasurgical fork, commit `0aaaf8c` | LGPL-2.1-or-later |
| [libpng](https://github.com/pnggroup/libpng) | 1.6.58 (commit `3061454`) | PNG Reference Library License v2 (libpng-2.0) |
| [libsodium](https://github.com/jedisct1/libsodium) | via [libsodium-cmake](https://github.com/robinlinden/libsodium-cmake) `a8ac450` | ISC (libsodium and the CMake wrapper) |
| [Lua](https://www.lua.org/) | 5.4.7, via the [walterschell/Lua](https://github.com/walterschell/Lua) CMake wrapper `3ed55a5` | MIT |
| [sol2](https://github.com/ThePhD/sol2) | diasurgical fork, commit `832ac77` | MIT |
| [magic_enum](https://github.com/Neargye/magic_enum) | 0.9.7 | MIT |
| [SheenBidi](https://github.com/Tehreer/SheenBidi) | 2.9.0 | Apache-2.0 |
| [unordered_dense](https://github.com/martinus/unordered_dense) | 4.4.0 | MIT |
| [mpqfs](https://github.com/diasurgical/mpqfs) | commit `de99526` | MIT |
| [asio](https://github.com/chriskohlhoff/asio) | diasurgical fork, commit `112f011` | BSL-1.0 (Boost Software License) |
| [libzt](https://github.com/zerotier/libzt) | diasurgical fork, commit `1a9d83b` | Business Source License 1.1, Change License Apache-2.0. See below. |

**LGPL components.** SDL_audiolib and libsmackerdec are linked statically. Their complete source is at the URLs above. You can rebuild the app against a modified copy of either library with CMake's standard override, e.g. `scripts/build.sh Release -DFETCHCONTENT_SOURCE_DIR_SDL_AUDIOLIB=/path/to/SDL_audiolib` (or `-DFETCHCONTENT_SOURCE_DIR_LIBSMACKERDEC=...`).

**libzt** (ZeroTier, used for online multiplayer):
- libzt's `LICENSE.txt` is the Business Source License 1.1 with Change Date **2026-01-01**. The ZeroTier One core it bundles has Change Date 2025-01-01. On the Change Date the code becomes available under the Change License, the **Apache License 2.0**. Both dates have passed.
- Third-party code bundled in libzt keeps its own license, listed in its `ext/THIRDPARTY.txt`. Examples: lwIP (BSD-3-Clause), LZ4 (BSD-2-Clause), concurrentqueue (BSD-2-Clause).

## Used only for tests or optional builds

| Component | Version / source | License | When |
|---|---|---|---|
| [GoogleTest](https://github.com/google/googletest) | 1.15.2 | BSD-3-Clause | `-DBUILD_TESTING=ON` |
| [Google Benchmark](https://github.com/google/benchmark) | 1.8.5 | Apache-2.0 | `-DBUILD_TESTING=ON` |
| [SDL3](https://github.com/libsdl-org/SDL), [SDL3_image](https://github.com/libsdl-org/SDL_image), [SDL3_mixer](https://github.com/libsdl-org/SDL_mixer) | SDL3 commit `570f722`, SDL3_image 3.2.4, SDL3_mixer 3.2.0 | zlib | `-DUSE_SDL3=ON` |
| [zlib](https://zlib.net/), [bzip2](https://gitlab.com/bzip2/bzip2) | 1.3, 1.0.8 | zlib, bzip2 license | Only for static builds. On macOS the app links the system `libz`/`libbz2` from the SDK. |
| Discord Game SDK | 3.2.1 | proprietary (Discord) | Only with `-DDISCORD_INTEGRATION=ON`. Off by default and never used by this fork. |

## Bundled in `devilutionx/` (from upstream)

- `3rdParty/PicoSHA2/picosha2.h`: [PicoSHA2](https://github.com/okdshin/PicoSHA2), MIT.
- `3rdParty/tl/function_ref.hpp`: [tl::function_ref](https://github.com/TartanLlama/function_ref), CC0-1.0.
- `3rdParty/SDL_image/IMG.c`: adapted from SDL_image, zlib.
- `assets/lua/inspect.lua`: [inspect.lua](https://github.com/kikito/inspect.lua) 3.1.0, MIT.
- `Packaging/resources/`: license texts that upstream ships with its packages: SIL Open Font License 1.1 (Charis SIL), Creative Commons Attribution 4.0, zlib, and the SDL README.

## Game data

No Blizzard game data is included: no MPQ archives, art, audio or video. *Diablo* and *Hellfire* are trademarks of Blizzard Entertainment, Inc. This project is not affiliated with or endorsed by Blizzard Entertainment or the DevilutionX project.
