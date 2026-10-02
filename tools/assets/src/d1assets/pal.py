"""PAL palettes: 256 RGB triplets, 768 bytes, 8 bits per channel."""

from __future__ import annotations

import numpy as np

from .image import Palette

PAL_SIZE = 768


def decode(data: bytes) -> Palette:
    """Decodes a 768-byte PAL file into a ``(256, 3)`` uint8 array."""
    if len(data) != PAL_SIZE:
        raise ValueError(f"PAL file must be {PAL_SIZE} bytes, got {len(data)}")
    return np.frombuffer(data, dtype=np.uint8).reshape(256, 3).copy()


def encode(palette: Palette) -> bytes:
    arr = np.asarray(palette)
    if arr.shape != (256, 3):
        raise ValueError(f"palette must have shape (256, 3), got {arr.shape}")
    if ((arr < 0) | (arr > 255)).any():
        raise ValueError("palette values must be in 0..255")
    return arr.astype(np.uint8).tobytes()


def grayscale() -> Palette:
    """A synthetic grey ramp palette, handy for previews and tests."""
    ramp = np.arange(256, dtype=np.uint8)
    return np.stack([ramp, ramp, ramp], axis=1)
