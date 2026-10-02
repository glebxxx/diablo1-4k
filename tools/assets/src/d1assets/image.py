"""Indexed images: the in-memory picture format shared by all codecs.

An indexed image is a 2-D ``numpy.int16`` array of palette indices (0..255).
The value :data:`TRANSPARENT` (-1) marks a fully transparent pixel. Row 0 is the
top row, as in PNG; the Diablo formats store rows bottom-up and the codecs flip.
"""

from __future__ import annotations

from typing import TypeAlias

import numpy as np
import numpy.typing as npt
from PIL import Image

TRANSPARENT = -1

IndexedImage: TypeAlias = npt.NDArray[np.int16]
Palette: TypeAlias = npt.NDArray[np.uint8]  # shape (256, 3)


def new_image(width: int, height: int, fill: int = TRANSPARENT) -> IndexedImage:
    return np.full((height, width), fill, dtype=np.int16)


def as_indexed(pixels: npt.ArrayLike) -> IndexedImage:
    """Validates and converts ``pixels`` to an :data:`IndexedImage`."""
    arr = np.asarray(pixels)
    if arr.ndim != 2:
        raise ValueError(f"indexed image must be 2-D, got shape {arr.shape}")
    if arr.size and (arr.min() < TRANSPARENT or arr.max() > 255):
        raise ValueError("indexed image values must be in -1..255")
    return arr.astype(np.int16, copy=False)


def to_rgba(image: IndexedImage, palette: Palette) -> npt.NDArray[np.uint8]:
    """Converts an indexed image to an ``(h, w, 4)`` RGBA array."""
    image = as_indexed(image)
    rgba = np.zeros((*image.shape, 4), dtype=np.uint8)
    opaque = image != TRANSPARENT
    rgba[opaque, :3] = np.asarray(palette, dtype=np.uint8)[image[opaque]]
    rgba[opaque, 3] = 255
    return rgba


def to_pil(image: IndexedImage, palette: Palette) -> Image.Image:
    """Converts an indexed image to an RGBA Pillow image."""
    return Image.fromarray(to_rgba(image, palette), "RGBA")


def from_rgba_exact(
    rgba: npt.NDArray[np.uint8], palette: Palette, alpha_threshold: int = 128
) -> IndexedImage:
    """Converts RGBA pixels that use palette colours exactly back to indices.

    Raises ``ValueError`` for a colour that is not in the palette (use
    :mod:`d1assets.quantize` for arbitrary images). If a colour occurs several
    times in the palette, the lowest index wins.
    """
    rgba = np.asarray(rgba, dtype=np.uint8)
    if rgba.ndim != 3 or rgba.shape[2] != 4:
        raise ValueError("expected an (h, w, 4) RGBA array")
    pal = np.asarray(palette, dtype=np.uint8)
    keys = (pal[:, 0].astype(np.int32) << 16) | (pal[:, 1].astype(np.int32) << 8) | pal[:, 2]
    lookup: dict[int, int] = {}
    for index in range(len(keys) - 1, -1, -1):
        lookup[int(keys[index])] = index
    out = new_image(rgba.shape[1], rgba.shape[0])
    opaque = rgba[:, :, 3] >= alpha_threshold
    pix = rgba[opaque].astype(np.int32)
    pix_keys = (pix[:, 0] << 16) | (pix[:, 1] << 8) | pix[:, 2]
    uniq, inverse = np.unique(pix_keys, return_inverse=True)
    mapped = np.empty(len(uniq), dtype=np.int16)
    for i, key in enumerate(uniq):
        found = lookup.get(int(key))
        if found is None:
            raise ValueError(f"colour #{int(key):06x} is not in the palette; quantize the image first")
        mapped[i] = found
    out[opaque] = mapped[inverse.reshape(-1)]
    return out


def load_png_indexed(path: str, palette: Palette | None = None) -> IndexedImage:
    """Loads a PNG as an indexed image.

    A paletted (``P`` mode) PNG keeps its indices; transparency comes from its
    ``tRNS`` chunk. Any other PNG is converted to RGBA and mapped exactly to
    ``palette``.
    """
    with Image.open(path) as im:
        if im.mode == "P" and palette is None:
            indices = np.asarray(im, dtype=np.int16).copy()
            transparency = im.info.get("transparency")
            if isinstance(transparency, int):
                indices[indices == transparency] = TRANSPARENT
            elif isinstance(transparency, bytes):
                for index, alpha in enumerate(transparency):
                    if alpha < 128:
                        indices[indices == index] = TRANSPARENT
            return indices
        if palette is None:
            raise ValueError(f"{path}: a palette is required to read a non-paletted PNG")
        return from_rgba_exact(np.asarray(im.convert("RGBA")), palette)
