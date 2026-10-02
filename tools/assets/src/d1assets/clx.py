"""CLX sprites: DevilutionX's runtime format (``Source/engine/clx_sprite.hpp``).

CLX uses the CL2 pixel commands but a 6-byte frame header that stores the
frame size, so no external widths are needed::

    uint16 header size (6)
    uint16 width
    uint16 height

A CLX *sheet* is a grouped container (one sprite list per group). The engine
converts CEL and CL2 to CLX when loading (``cel_to_clx.cpp``, ``cl2_to_clx.cpp``);
:func:`from_cel` and :func:`from_cl2` do the same here.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

import numpy as np

from . import _runs, cel, cl2
from .container import read_container, write_container
from .image import IndexedImage, as_indexed

FRAME_HEADER_SIZE = 6


def frame_size(frame: bytes) -> tuple[int, int]:
    """Returns ``(width, height)`` from a CLX frame header."""
    if len(frame) < FRAME_HEADER_SIZE:
        raise ValueError("truncated CLX frame header")
    _, width, height = struct.unpack_from("<3H", frame, 0)
    return int(width), int(height)


def decode_frame(frame: bytes) -> IndexedImage:
    header_size = struct.unpack_from("<H", frame, 0)[0] if len(frame) >= 2 else 0
    if header_size < FRAME_HEADER_SIZE or header_size > len(frame):
        raise ValueError(f"invalid CLX frame header size {header_size}")
    width, height = frame_size(frame)
    stream = _runs.decode_stream(frame, header_size)
    if len(stream) != width * height:
        raise ValueError(f"CLX frame has {len(stream)} pixels, expected {width}x{height}")
    return stream.reshape(height, width)[::-1].copy()


def encode_frame(image: IndexedImage) -> bytes:
    image = as_indexed(image)
    height, width = image.shape
    if width > 0xFFFF or height > 0xFFFF:
        raise ValueError("CLX frames are limited to 65535x65535")
    out = bytearray(struct.pack("<3H", FRAME_HEADER_SIZE, width, height))
    _runs.encode_stream(image[::-1].reshape(-1), out)
    return bytes(out)


def decode(data: bytes, grouped: bool | None = None) -> list[list[IndexedImage]]:
    """Decodes a CLX sprite list (one group) or sheet (several groups)."""
    return [[decode_frame(f) for f in frames] for frames in read_container(data, grouped)]


def encode(groups: Sequence[Sequence[IndexedImage]], grouped: bool | None = None) -> bytes:
    """Encodes groups of frames as a CLX list (one group) or sheet."""
    return write_container([[encode_frame(np.asarray(img)) for img in frames] for frames in groups], grouped)


def from_cel(data: bytes, width: int | Sequence[int], grouped: bool | None = None) -> bytes:
    """Converts a CEL file to CLX, like the engine's ``CelToClx``."""
    groups = cel.decode(data, width, grouped)
    return encode(groups, grouped=len(groups) != 1 or bool(grouped))


def from_cl2(data: bytes, width: int | Sequence[int], grouped: bool | None = None) -> bytes:
    """Converts a CL2 file to CLX, like the engine's ``Cl2ToClx``."""
    groups = cl2.decode(data, width, grouped)
    return encode(groups, grouped=len(groups) != 1 or bool(grouped))
