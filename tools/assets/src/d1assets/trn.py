"""TRN colour translations: 256 bytes mapping palette index to palette index.

TRN files recolour sprites (e.g. monster variants, ``plrgfx\\*.trn``); see
``Source/engine/trn.cpp``.
"""

from __future__ import annotations

from typing import TypeAlias

import numpy as np
import numpy.typing as npt

from .image import TRANSPARENT, IndexedImage, as_indexed

TRN_SIZE = 256

Translation: TypeAlias = npt.NDArray[np.uint8]


def decode(data: bytes) -> Translation:
    if len(data) != TRN_SIZE:
        raise ValueError(f"TRN file must be {TRN_SIZE} bytes, got {len(data)}")
    return np.frombuffer(data, dtype=np.uint8).copy()


def encode(table: Translation) -> bytes:
    arr = np.asarray(table)
    if arr.shape != (TRN_SIZE,):
        raise ValueError(f"translation must have shape (256,), got {arr.shape}")
    if ((arr < 0) | (arr > 255)).any():
        raise ValueError("translation values must be in 0..255")
    return arr.astype(np.uint8).tobytes()


def identity() -> Translation:
    return np.arange(TRN_SIZE, dtype=np.uint8)


def apply(image: IndexedImage, table: Translation) -> IndexedImage:
    """Applies a translation to an indexed image, keeping transparent pixels."""
    image = as_indexed(image)
    out = image.copy()
    opaque = image != TRANSPARENT
    out[opaque] = np.asarray(table, dtype=np.int16)[image[opaque]]
    return out
