# HD texture pack format (`d1-hdpack`, version 1)

An HD texture pack replaces individual sprite frames with higher-resolution PNG images. This document is the contract between the tools in `tools/assets/` (which build and validate packs) and a **future** engine loader. The engine does not load packs yet.

Packs are built **locally**, from the user's own game files. They contain upscaled Blizzard art, so they are never committed to this repository and never distributed with it.

## Layout

A pack is a directory:

```
<pack>/
  hdpack.json                                  # the manifest
  textures/
    monsters/zombie/zombiew.cl2/d0_f0000.png   # direction 0, frame 0
    monsters/zombie/zombiew.cl2/d0_f0001.png
    ...
    levels/l1data/l1.cel/d0_f0041.png          # dungeon micro-tile 42 (frame index 41)
```

The PNG paths follow the convention `textures/<asset>/d<direction>_f<frame:04>.png`. The loader must not depend on it: it reads each path from the manifest.

Install a pack by copying the directory into the DevilutionX mods folder:

```
~/Library/Application Support/diasurgical/devilution/mods/<pack-name>/
```

`hdpack.json` sits at the root of that folder.

## Manifest (`hdpack.json`)

```json
{
  "format": "d1-hdpack",
  "version": 1,
  "name": "my-hd-monsters",
  "description": "Real-ESRGAN x4, ramp-quantized",
  "entries": [
    {
      "asset": "monsters/zombie/zombiew.cl2",
      "direction": 0,
      "frame": 0,
      "scale": 4,
      "file": "textures/monsters/zombie/zombiew.cl2/d0_f0000.png",
      "source_sha256": "3f1c…64 hex digits…",
      "source_width": 128,
      "source_height": 96
    }
  ]
}
```

Top level:

| Key           | Type   | Required | Meaning                                    |
|---------------|--------|----------|--------------------------------------------|
| `format`      | string | yes      | Always `"d1-hdpack"`.                      |
| `version`     | int    | yes      | Always `1` for this document.              |
| `name`        | string | yes      | Non-empty pack name.                       |
| `description` | string | no       | Free text.                                 |
| `entries`     | list   | yes      | One object per replaced frame (see below). |

Entry:

| Key             | Type   | Meaning |
|-----------------|--------|---------|
| `asset`         | string | Path of the asset inside the MPQ, **lowercase with `/` separators**, e.g. `monsters/zombie/zombiew.cl2`. The engine's paths use `\`; compare them after the same normalization. Must end in `.cel`, `.cl2`, `.clx` or `.pcx`. |
| `direction`     | int ≥ 0 | Group index in a grouped CEL/CL2/CLX file: the direction for monsters and players. `0` for files without groups. |
| `frame`         | int ≥ 0 | 0-based frame index within the group. For level CELs (`levels/*/l?.cel`), frame `n` is the micro-tile that MIN files call `n + 1`. |
| `scale`         | int 1..16 | Integer scale factor of the PNG relative to the original frame. |
| `file`          | string | Pack-relative POSIX path to a PNG. No `..`, no leading `/`, no `\`. |
| `source_sha256` | string | SHA-256 (64 lowercase hex digits) of the **whole original asset file**, as stored in the MPQ after decompression and decryption. It identifies the game version the pack was made from. All entries of one asset carry the same value. |
| `source_width`, `source_height` | int > 0 | Size of the original frame in pixels. |

Rules (checked by `d1-hdpack validate`):

- Each `(asset, direction, frame, scale)` appears at most once. One pack may hold several scales of the same frame.
- Each PNG is exactly `source_width × scale` by `source_height × scale` pixels. Modes RGBA, RGB, P, LA and L are accepted. Alpha below 128 means transparent.
- Unknown keys produce warnings, so newer optional keys don't break older validators. Files under `textures/` that the manifest doesn't reference also produce warnings.

## Loader behaviour (for the future engine side)

1. Read `hdpack.json`; ignore the pack if `format` or `version` is not supported.
2. When an asset is loaded, hash its bytes once. Use only the entries whose `source_sha256` matches. A pack made from another game version (other language, Hellfire vs. Diablo, shareware) is then skipped, never mis-applied.
3. Pick the entry whose `scale` best fits the current output scale, and draw it in place of the original frame, at the same anchor point, scaled by `1 / scale` in world coordinates.
4. Gameplay never depends on a pack: it is purely cosmetic, so multiplayer and demos are unaffected.

Palette effects (lighting, colour cycling, TRN recolouring) work on palette indices. To keep them in a loader that draws through the palette, quantize the PNG back to the palette (`d1assets pack --quantize ramp`): then every HD pixel is a palette colour from the same light ramp as its source pixel, and colour-cycling indices stay in place.

## Validation

```bash
d1-hdpack validate path/to/pack                                  # format, PNG sizes, paths
d1-hdpack validate path/to/pack --mpq ~/Library/Application\ Support/diasurgical/devilution/DIABDAT.MPQ   # + source hashes
d1-hdpack hash --mpq DIABDAT.MPQ 'monsters\zombie\zombiew.cl2'   # print a source_sha256
```

The exit status is non-zero if there are errors, or warnings with `--strict`.
