# d1assets — Diablo graphics codecs and HD pack tools

A typed Python 3.10+ package (dependencies: numpy and Pillow) that reads **and writes** Diablo 1 / Hellfire graphics formats. It quantizes upscaled images back to the game palette and builds HD texture packs.

It ships **no game data**. The tests use synthetic images generated in code. An optional integration test runs against the shareware `spawn.mpq`, downloaded at test time like upstream DevilutionX CI does.

## Install

```bash
python3 -m venv .venv && source .venv/bin/activate     # Python 3.10 or newer
pip install -e 'tools/assets[dev]'
```

On macOS, the `python3` of the Xcode Command Line Tools is 3.9, which is too old. Use the installer from python.org or `brew install python`. Python from Homebrew is fine; the "no Homebrew SDL" rule is about the engine build only.

## Formats

| Module | Format | Decode | Encode | Notes |
|---|---|:-:|:-:|---|
| `mpq` | MPQ v0/v1 | ✅ | ✅ | Encrypted hash/block tables, hi-block table, encrypted files (incl. `FIX_KEY`), single-unit and sectored files; sector compression PKWARE implode, zlib, bzip2. The writer exists for tests and small mods. |
| `pkware` | PKWARE DCL implode | ✅ | ✅ | Binary and ASCII literal modes, 1/2/4 KiB dictionaries. |
| `pal` | PAL (768 bytes) | ✅ | ✅ | |
| `trn` | TRN (256 bytes) | ✅ | ✅ | `trn.apply()` recolours an image. |
| `cel` | CEL | ✅ | ✅ | Frame widths come from the caller (one width or one per frame); optional 10-byte frame headers; grouped files. |
| `cl2` | CL2 | ✅ | ✅ | Groups (directions); widths from the caller; exact 32-row block offsets. |
| `clx` | CLX (DevilutionX) | ✅ | ✅ | Same run encoder as `Source/utils/clx_encode.hpp`; `from_cel` / `from_cl2` mirror the engine's converters. |
| `dungeon` | Level CEL micro-tiles, MIN, TIL | ✅ | ✅ | All 6 micro-tile encodings (`Source/levels/dun_tile.hpp`), with or without the pad bytes that `reencode_dun_cels.cpp` strips; piece and mega-tile assembly. |
| `pcx` | PCX (8-bit, RLE) | ✅ | ✅ | With or without the trailing 256-colour palette. |
| `quantize` | RGBA → palette | | | Nearest colour, or per light ramp (below). |
| `hdpack` | HD pack manifest | ✅ | ✅ | Format spec: [HDPACK.md](HDPACK.md). |

In memory, an image is a 2-D `numpy.int16` array of palette indices, with `-1` for transparent pixels. Row 0 is the top row.

Round trips are property-tested with Hypothesis: `decode(encode(x)) == x` for every codec. Against the shareware data, re-encoding gives byte-identical files for every micro-tile of the town and L1 level CELs (all 6 encodings) and for `data\char.cel`.

```python
from d1assets import cl2, pal
from d1assets.image import to_pil
from d1assets.mpq import MpqArchive

mpq = MpqArchive("DIABDAT.MPQ")                       # your own copy
palette = pal.decode(mpq.read("levels\\towndata\\town.pal"))
directions = cl2.decode(mpq.read("monsters\\zombie\\zombiew.cl2"), width=128)
to_pil(directions[0][0], palette).save("zombie.png")
```

## Quantization

`d1assets.quantize` maps RGBA pixels (e.g. from an AI upscaler) back to the 256-colour palette:

- **nearest**: each pixel takes the closest palette colour (RGB or "redmean" distance).
- **ramp**: each pixel takes the closest colour *in the light ramp of its source pixel* in the original frame. Diablo darkens a colour by moving along its ramp (`Source/engine/render/light_tables.cpp`: ramps of 16 colours for 0–127 and 160–254, of 8 colours for 128–159; 255 alone). A pixel that leaves its source's ramp would shade to the wrong hue in the dark.
- **Colour cycling**: indices that the engine animates are kept as they are, and no other pixel may use them. Use `--cycling caves|hell|nest|crypt`: caves and hell cycle 1–31, the Hellfire nest 1–15, the crypt 1–31 (`Source/engine/palette.cpp`, `Source/lighting.cpp`).

## The HD pipeline, on your Mac

Everything happens locally, with **your own** game files. Nothing you extract or upscale may be uploaded to this repository.

```bash
DATA=~/Library/Application\ Support/diasurgical/devilution
mkdir -p ~/d1hd && cd ~/d1hd

# 1. Extract: decode frames from YOUR OWN MPQ into PNGs plus a scale-1 manifest.
d1assets export 'monsters\zombie\zombiew.cl2' --mpq "$DATA/DIABDAT.MPQ" \
    --palette 'levels\towndata\town.pal' --width 128 -o work
d1assets export 'levels\l1data\l1.cel' --mpq "$DATA/DIABDAT.MPQ" \
    --palette 'levels\l1data\l1.pal' --min 'levels\l1data\l1.min' --blocks 10 -o work

# 2. Upscale the PNGs under work/textures/ locally (Topaz Gigapixel, Real-ESRGAN, ...) by 4x.
#    Write the results to upscaled/textures/, keeping the same relative paths and file names.

# 3. Quantize (each asset with the palette it was exported with) and pack.
d1assets pack work upscaled -o packs/my-hd --scale 4 --name my-hd --quantize ramp

# 4. Validate, then install into the mods folder.
d1-hdpack validate packs/my-hd --mpq "$DATA/DIABDAT.MPQ"
cp -R packs/my-hd "$DATA/mods/"
```

- `export` writes `work/hdpack.json`, the PNGs, and `work/source.npz`. The `.npz` holds the exact palette indices of each frame and the palette of each asset, which `pack --quantize` uses. Use `--cycling caves` (etc.) when quantizing assets of levels with colour cycling.
- `pack` checks that each upscaled image is exactly `--scale` times its source. Pass `--resize` to fit images that are not.
- Without `--quantize` the pack keeps full-colour PNGs.
- The engine does not load HD packs yet; [HDPACK.md](HDPACK.md) is the format a future loader will read.
- Diablo's `DIABDAT.MPQ` has no `(listfile)`, so `d1assets mpq list` needs `--listfile` with candidate names. `mpq extract` with explicit names always works.

## Other commands

```bash
d1assets mpq list ARCHIVE [--listfile names.txt]
d1assets mpq extract ARCHIVE 'data\char.cel' -o out/
d1assets quantize big.png --palette town.pal --mode ramp --source small.png -o big-q.png
d1assets encode f0.png f1.png ... --format clx --palette town.pal --groups 8 -o sprite.clx
d1assets tiles --mpq DIABDAT.MPQ --cel 'levels\l1data\l1.cel' --min 'levels\l1data\l1.min' \
    --til 'levels\l1data\l1.til' --palette 'levels\l1data\l1.pal' -o tiles/
d1-hdpack hash --mpq DIABDAT.MPQ 'monsters\zombie\zombiew.cl2'
```

Run `d1assets <command> --help` for all options.

## Development

```bash
cd tools/assets
ruff check . && ruff format --check .
pytest                                            # synthetic data only
HYPOTHESIS_PROFILE=ci pytest                      # more examples, as in CI
wget -qnc https://github.com/diasurgical/devilutionx-assets/releases/download/v2/spawn.mpq -P /tmp
D1ASSETS_SPAWN_MPQ=/tmp/spawn.mpq pytest          # + real-format checks (never commit spawn.mpq)
```

CI: `.github/workflows/tools-assets.yml`.
