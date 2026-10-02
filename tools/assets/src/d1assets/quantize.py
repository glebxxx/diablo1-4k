"""Map RGBA images (e.g. AI-upscaled sprites) back to Diablo's 256-colour palette.

Two strategies:

* :func:`quantize_nearest`: each pixel takes the closest palette colour.
* :func:`quantize_ramp`: each pixel takes the closest colour *within the light
  ramp of its source pixel* in the original low-resolution image. Diablo shades
  by shifting indices along a ramp (``Source/engine/light_tables.cpp``), so a
  pixel that leaves its source's ramp would shade into the wrong hue. Pixels
  whose source is in a colour-cycling range (``Source/engine/palette.cpp``,
  ``Source/lighting.cpp``) keep their source index, because their colour is
  animated at runtime; no other pixel may use a cycling index.

Light ramps, from ``MakeLightTable``: indices 0-127 in 16-colour ramps,
128-159 in 8-colour ramps, 160-254 in 16-colour ramps (240-254 is the last
one), and 255 (white, which the light tables turn black) on its own.
"""

from __future__ import annotations

from collections.abc import Iterable, Sequence

import numpy as np
import numpy.typing as npt

from .image import TRANSPARENT, IndexedImage, Palette, as_indexed

_RAMP_STEPS = (16, 16, 16, 16, 16, 16, 16, 16, 8, 8, 8, 8, 16, 16, 16, 16, 16, 16)

# Colour-cycling ranges per dungeon type (inclusive index ranges).
CYCLING_RANGES: dict[str, tuple[tuple[int, int], ...]] = {
    "none": (),
    "town": (),
    "cathedral": (),
    "catacombs": (),
    "caves": ((1, 31),),  # palette_update_caves: lava/water
    "hell": ((1, 31),),  # lighting_color_cycling: blood walls
    "nest": ((1, 8), (9, 15)),  # palette_update_hive: bubbles and waves
    "crypt": ((1, 15), (16, 31)),  # palette_update_crypt: lava and glow
}

METRICS = ("rgb", "redmean")


def light_ramps() -> list[range]:
    """The palette's light ramps, in index order."""
    ramps: list[range] = []
    start = 0
    for steps in _RAMP_STEPS:
        ramps.append(range(start, start + steps))
        start += steps
    last = ramps.pop()
    return [*ramps, range(last.start, 255), range(255, 256)]


def ramp_ids() -> npt.NDArray[np.int16]:
    """For each palette index, the number of the light ramp that contains it."""
    ids = np.zeros(256, dtype=np.int16)
    for number, ramp in enumerate(light_ramps()):
        ids[ramp.start : ramp.stop] = number
    return ids


def reserved_indices(cycling: str | Iterable[tuple[int, int]]) -> frozenset[int]:
    """Palette indices reserved for colour cycling (a preset name or inclusive ranges)."""
    ranges = CYCLING_RANGES[cycling] if isinstance(cycling, str) else tuple(cycling)
    return frozenset(i for lo, hi in ranges for i in range(lo, hi + 1))


def _distances(colors: npt.NDArray[np.int32], candidates: npt.NDArray[np.int32], metric: str) -> np.ndarray:
    diff = colors[:, None, :] - candidates[None, :, :]
    if metric == "rgb":
        return (diff * diff).sum(axis=2)
    if metric == "redmean":
        rmean = (colors[:, None, 0] + candidates[None, :, 0]) // 2
        sq = diff * diff
        return ((512 + rmean) * sq[:, :, 0] >> 8) + 4 * sq[:, :, 1] + ((767 - rmean) * sq[:, :, 2] >> 8)
    raise ValueError(f"metric must be one of {METRICS}")


def _nearest(
    rgb: npt.NDArray[np.uint8], palette: Palette, candidates: Sequence[int], metric: str
) -> npt.NDArray[np.int16]:
    """Nearest of ``candidates`` for each ``(N, 3)`` colour; ties go to the lowest index."""
    cand = np.asarray(sorted(candidates), dtype=np.int16)
    if len(cand) == 0:
        raise ValueError("no candidate colours")
    if len(rgb) == 0:
        return np.zeros(0, dtype=np.int16)
    packed = (rgb[:, 0].astype(np.int32) << 16) | (rgb[:, 1].astype(np.int32) << 8) | rgb[:, 2]
    uniq, inverse = np.unique(packed, return_inverse=True)
    colors = np.stack([(uniq >> 16) & 0xFF, (uniq >> 8) & 0xFF, uniq & 0xFF], axis=1).astype(np.int32)
    pal = np.asarray(palette, dtype=np.int32)[cand]
    best = np.empty(len(uniq), dtype=np.int16)
    chunk = max(1, (1 << 20) // len(cand))
    for i in range(0, len(uniq), chunk):
        best[i : i + chunk] = cand[np.argmin(_distances(colors[i : i + chunk], pal, metric), axis=1)]
    return best[inverse.reshape(-1)]


def _check_rgba(rgba: npt.ArrayLike) -> npt.NDArray[np.uint8]:
    arr = np.asarray(rgba)
    if arr.ndim != 3 or arr.shape[2] != 4:
        raise ValueError(f"expected an (h, w, 4) RGBA array, got shape {arr.shape}")
    return arr.astype(np.uint8, copy=False)


def quantize_nearest(
    rgba: npt.ArrayLike,
    palette: Palette,
    *,
    exclude: Iterable[int] = (),
    alpha_threshold: int = 128,
    metric: str = "rgb",
) -> IndexedImage:
    """Maps each pixel to the nearest palette colour not in ``exclude``.

    Pixels with alpha below ``alpha_threshold`` become transparent.
    """
    arr = _check_rgba(rgba)
    excluded = set(exclude)
    candidates = [i for i in range(256) if i not in excluded]
    out = np.full(arr.shape[:2], TRANSPARENT, dtype=np.int16)
    opaque = arr[:, :, 3] >= alpha_threshold
    out[opaque] = _nearest(arr[opaque][:, :3], palette, candidates, metric)
    return out


def upscale_nearest(image: IndexedImage, scale: int) -> IndexedImage:
    """Nearest-neighbour upscale of an indexed image by an integer factor."""
    if scale < 1:
        raise ValueError("scale must be >= 1")
    return np.repeat(np.repeat(as_indexed(image), scale, axis=0), scale, axis=1)


def quantize_ramp(
    rgba: npt.ArrayLike,
    palette: Palette,
    source: IndexedImage,
    *,
    cycling: str | Iterable[tuple[int, int]] = "none",
    mask: str = "alpha",
    alpha_threshold: int = 128,
    metric: str = "rgb",
) -> IndexedImage:
    """Maps each pixel to the nearest colour in the light ramp of its source pixel.

    ``rgba`` must be ``source`` scaled by an integer factor (the same on both
    axes); pixel ``(y, x)`` has source pixel ``(y // scale, x // scale)``.

    ``mask="source"`` takes transparency from the source image (scaled up);
    ``mask="alpha"`` takes it from the RGBA alpha channel. An opaque pixel with
    a transparent source falls back to the nearest non-reserved colour.
    """
    arr = _check_rgba(rgba)
    src = as_indexed(source)
    height, width = arr.shape[:2]
    if src.shape[0] == 0 or src.shape[1] == 0:
        raise ValueError("source image is empty")
    scale = height // src.shape[0]
    if scale < 1 or (height, width) != (src.shape[0] * scale, src.shape[1] * scale):
        source_size = f"{src.shape[1]}x{src.shape[0]}"
        raise ValueError(f"image size {width}x{height} is not an integer multiple of source {source_size}")
    if mask not in ("alpha", "source"):
        raise ValueError("mask must be 'alpha' or 'source'")

    reserved = reserved_indices(cycling)
    up = upscale_nearest(src, scale)
    opaque = (up != TRANSPARENT) if mask == "source" else (arr[:, :, 3] >= alpha_threshold)
    out = np.full((height, width), TRANSPARENT, dtype=np.int16)

    ramps = light_ramps()
    ids = ramp_ids()
    group = np.where(up == TRANSPARENT, -1, ids[np.clip(up, 0, 255)])
    reserved_arr = np.zeros(256, dtype=bool)
    reserved_arr[list(reserved)] = True
    keep = opaque & (up != TRANSPARENT) & reserved_arr[np.clip(up, 0, 255)]
    out[keep] = up[keep]

    free = [i for i in range(256) if i not in reserved]
    todo = opaque & ~keep
    for number in np.unique(group[todo]).tolist():
        sel = todo & (group == number)
        candidates = free if number < 0 else [i for i in ramps[number] if i not in reserved]
        if not candidates:
            out[sel] = up[sel]
            continue
        out[sel] = _nearest(arr[sel][:, :3], palette, candidates, metric)
    return out
