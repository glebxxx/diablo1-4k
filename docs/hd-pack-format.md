# HD texture packs (engine format)

This is the contract between the engine (OpenGL renderer, `DEVILUTIONX_GL1_RENDERER=ON`) and whatever builds HD packs. It follows the maintainer's requirements in issue #10: one scale (4x), WebP images, one `manifest.json` per asset, frames found by a hash of their 1x pixels.

> **Packs contain upscaled Blizzard art.** Build them locally from your own game files. Never commit them to this repository and never publish them.

## Where packs live

```
<config dir>/hd/<pack>/...
```

On macOS that is `~/Library/Application Support/diasurgical/devilution/hd/<pack>/`.

- Every subfolder of `hd/` is a pack. Packs are read in alphabetical order; if two packs have the same frame, the first one wins.
- Packs are **not** mods: they need no `init.lua` and do not show up under *Settings → Mods*. They are purely cosmetic and never affect multiplayer, saves or demos.
- Turn them on with *Settings → Graphics → HD Textures* (`[Graphics] HD Textures=1` in `diablo.ini`). The folder is scanned the first time an HD frame is needed; turning the option off and on again rescans it.

## Layout of a pack

```
<pack>/
  <asset path>/
    manifest.json
    <group>_<frame>.webp
    ...
```

The engine only looks for files named `manifest.json`, at any depth (up to 16 levels). The folder names are free; mirroring the asset path inside the MPQ (`monsters/zombie/zombiew.cl2/`) keeps packs readable.

## `manifest.json`

```json
{
  "asset": "monsters/zombie/zombiew.cl2",
  "archive": "DIABDAT.MPQ",
  "scale": 4,
  "frames": [
    {
      "group": 0,
      "frame": 0,
      "width": 128,
      "height": 96,
      "bbox": [176, 52, 140, 296],
      "sha256": "3f1c…64 hex digits…",
      "file": "0_0.webp"
    }
  ]
}
```

| Key | Required | Meaning |
|---|---|---|
| `scale` | yes | Must be `4`. Manifests with any other scale are ignored. |
| `frames` | yes | One object per frame. |
| `asset`, `archive` | no | Informational, for tools and people. The engine does not use them. |

Frame:

| Key | Required | Meaning |
|---|---|---|
| `sha256` | yes | The frame key (see below), 64 hex digits. **This is how the engine finds the image.** |
| `width`, `height` | yes | Size of the original 1x frame. Must match the frame the engine draws. |
| `file` | yes | Image path relative to the folder of this `manifest.json`, with `/` separators. No `..`, no leading `/`, no `\`. |
| `bbox` | no | `[x, y, w, h]` in 4x pixels: where the image goes in the `4·width × 4·height` canvas. Lets a builder crop frames to their opaque area. The image must be exactly `w × h`. Default: the whole canvas (`[0, 0, 4·width, 4·height]`). |
| `group`, `frame` | no | Informational (direction and frame index in the asset). |

Several frames may point to the same `file` (identical frames deduplicated by the builder). Unknown keys are ignored. Invalid frames are skipped; the rest of the manifest is still used.

## Images

- WebP, RGBA with straight (not premultiplied) alpha. Lossless or lossy; lossy with lossless alpha keeps edges clean.
- Exactly `4·width × 4·height`, or exactly the `bbox` size when a `bbox` is given.
- Same anchor as the 1x frame: pixel `(x, y)` of the 1x frame covers 4x pixels `(4x … 4x+3, 4y … 4y+3)`.

When the engine loads an image it:

1. places it into an empty `4·width × 4·height` canvas at `bbox`;
2. sets alpha to 0 wherever the 1x frame has no opaque pixel within one 1x pixel (the 1x silhouette dilated by one pixel), so upscaler halos never spread beyond where the sprite can be clicked. Mouse picking keeps using the 1x frame;
3. for wall tiles with a diagonal transparency mask, multiplies alpha by the 1x mask;
4. premultiplies alpha, uploads with linear filtering and mipmaps.

## The frame key

The key identifies a frame by its 1x content, so it works without asset paths (the renderer does not know them) and gives recoloured variants their own keys.

```
key = SHA-256( u16le(width) ‖ u16le(height) ‖ u16le(pixel) for every pixel )
```

- Pixels go row by row from the **top-left** corner.
- `pixel` = the palette index **after** any colour translation (TRN) the game applied, or `0x0100` for a transparent pixel.
- `key` is written as 64 lowercase hex digits (upper case is accepted when reading).

In Python:

```python
import hashlib, struct
def frame_key(width, height, pixels):  # pixels: palette index, or None for transparent
    data = struct.pack('<HH', width, height)
    data += b''.join(struct.pack('<H', 0x100 if p is None else p) for p in pixels)
    return hashlib.sha256(data).hexdigest()
```

`devilutionx/test/fixtures/hd_pack/key_vectors.json` has test vectors; `test/hd_key_test.cpp` checks the engine against them.

What the engine hashes:

- **Sprites** (CEL/CL2/CLX frames: monsters, players, objects, items, missiles, UI): the decoded frame, `width × height` as stored.
- **Dungeon tiles** (level CEL micro-tiles): 32 pixels wide, decoded as the OpenGL renderer draws them. Height is 32 for squares and trapezoids, 31 for triangles. Pixels outside the tile's shape are transparent. The light table is **not** part of the key: one HD tile serves every light level and is darkened when drawn.

### The safest way to get keys: dump mode

Start the game (OpenGL renderer) with `DVX_HD_DUMP=<dir>`. Every frame the renderer uploads is written once:

```
<dir>/1x/<first two hex digits>/<key>.png     # 1x RGBA, colours of the level palette
<dir>/manifest.tsv                            # key, kind, width, height, tile_type, trn, level_type
```

These are exactly the frames and keys the loader looks up. A pack builder can upscale `<key>.png` to `<key>.webp` at 4x and list it with `"sha256": "<key>"`, without reimplementing the decoders. Walk through the areas you want covered: only frames that were drawn are dumped.

## What stays 1x in this version

- **Colour-cycled frames.** Frames using palette indices that colour cycling animates on the current level (caves, hell, crypt: 1–31; nest: 1–15; nothing in town and the cathedral) keep the 1x texture, because a fixed RGBA image cannot follow the animation.
- **Palette variants.** The key contains palette indices, not colours. The same frame drawn with another palette (for example the random dungeon palettes) uses the same HD image.
- **Brightness setting.** HD images ignore *Brightness Correction*. Fades and lighting work (they are drawn on top / as vertex colour).
- Frames not in any pack, frames whose image fails to load (logged once), and everything in the software renderer.

## Memory

HD textures are loaded on first use and kept while there is room in *Settings → Graphics → HD Texture Memory* (`[Graphics] HD Texture Memory MB`, default 1024). Beyond that the least recently drawn are freed; they are loaded again from disk when needed.

## Differences from `tools/assets/HDPACK.md` (PR #16)

PR #16's `d1-hdpack` format has one `hdpack.json` per pack, PNG files, several scales, and a `source_sha256` of the whole asset file used together with the asset path. This loader follows the later maintainer requirements in issue #10 (4x only, WebP, `manifest.json` per asset, per-frame pixel hash), because the renderer only has frame contents, not asset paths. The `d1-hdpack` tools need an update to write this format.
