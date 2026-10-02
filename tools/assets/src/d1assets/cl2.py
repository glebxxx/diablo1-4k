"""CL2 sprites (Diablo's run-length format for monsters, players and missiles).

Like CEL, frames do not store their width; the caller supplies it. Groups
usually hold one direction each (8 directions for monsters and players).

Each frame begins with a 10-byte header: ``uint16`` header size (10) and four
``uint16`` offsets (from the frame start) of the commands that begin rows 32,
64, 96 and 128 (0 when the frame is not that tall). The pixel commands follow
(see :mod:`d1assets._runs`); runs may cross rows, rows are stored bottom-up.
The encoder never lets a run cross a 32-row block boundary, so the offsets in
the header are exact.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

import numpy as np

from . import _runs
from .cel import _widths
from .container import read_container, write_container
from .image import IndexedImage, as_indexed

FRAME_HEADER_SIZE = 10
_BLOCK_ROWS = 32


def decode_frame(frame: bytes, width: int) -> IndexedImage:
    if width <= 0:
        raise ValueError("width must be positive")
    if len(frame) < 2:
        raise ValueError("truncated CL2 frame")
    header_size = struct.unpack_from("<H", frame, 0)[0]
    if header_size > len(frame):
        raise ValueError(f"CL2 frame header size {header_size} exceeds frame size {len(frame)}")
    stream = _runs.decode_stream(frame, header_size)
    if len(stream) % width:
        raise ValueError(f"CL2 frame has {len(stream)} pixels, not a multiple of width {width}")
    return stream.reshape(-1, width)[::-1].copy()


def encode_frame(image: IndexedImage) -> bytes:
    image = as_indexed(image)
    height = image.shape[0]
    bottom_up = image[::-1]
    out = bytearray(FRAME_HEADER_SIZE)
    offsets = [0, 0, 0, 0]
    for block_start in range(0, height, _BLOCK_ROWS):
        block = block_start // _BLOCK_ROWS
        if 1 <= block <= 4:
            offsets[block - 1] = len(out)
        _runs.encode_stream(bottom_up[block_start : block_start + _BLOCK_ROWS].reshape(-1), out)
    struct.pack_into("<5H", out, 0, FRAME_HEADER_SIZE, *offsets)
    return bytes(out)


def decode(data: bytes, width: int | Sequence[int], grouped: bool | None = None) -> list[list[IndexedImage]]:
    """Decodes a CL2 file into groups (directions) of frames."""
    groups = read_container(data, grouped)
    return [[decode_frame(f, w) for f, w in zip(frames, _widths(width, len(frames)))] for frames in groups]


def encode(groups: Sequence[Sequence[IndexedImage]], grouped: bool | None = None) -> bytes:
    """Encodes groups of frames as a CL2 file."""
    return write_container([[encode_frame(np.asarray(img)) for img in frames] for frames in groups], grouped)
