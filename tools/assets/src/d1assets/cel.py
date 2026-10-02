"""CEL sprites (Diablo's original RLE format).

CEL frames do not store their width: the caller supplies it, either one width
for all frames or one per frame (see ``Source/utils/cel_to_clx.cpp``).

Frame data, rows bottom-up, runs never cross rows:

* ``0x01..0x7F``: ``n`` opaque pixels follow;
* ``0x80..0xFF``: ``-int8(n)`` (1..128) transparent pixels.

A frame may start with an optional 10-byte header: ``uint16 10`` followed by
four ``uint16`` offsets (from the frame start) of rows 32, 64, 96 and 128
(0 when the frame is not that tall). The engine detects it by the first word
being 10.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

import numpy as np

from .container import read_container, write_container
from .image import TRANSPARENT, IndexedImage, as_indexed

FRAME_HEADER_SIZE = 10
_BLOCK_ROWS = 32


def has_frame_header(frame: bytes) -> bool:
    return len(frame) >= 2 and struct.unpack_from("<H", frame, 0)[0] == FRAME_HEADER_SIZE


def decode_frame(frame: bytes, width: int, frame_header: bool | None = None) -> IndexedImage:
    """Decodes one CEL frame. ``frame_header=None`` uses the engine's detection."""
    if width <= 0:
        raise ValueError("width must be positive")
    if frame_header is None:
        frame_header = has_frame_header(frame)
    pos = FRAME_HEADER_SIZE if frame_header else 0
    rows: list[list[int]] = []
    end = len(frame)
    while pos < end:
        row: list[int] = []
        while len(row) < width:
            if pos >= end:
                raise ValueError("truncated CEL row")
            control = frame[pos]
            pos += 1
            if control >= 0x80:
                row.extend([TRANSPARENT] * (256 - control))
            else:
                if control == 0 or pos + control > end:
                    raise ValueError(f"invalid CEL run 0x{control:02x} at offset {pos - 1}")
                row.extend(frame[pos : pos + control])
                pos += control
        if len(row) != width:
            raise ValueError(f"CEL row overruns width {width}; wrong frame width?")
        rows.append(row)
    if not rows:
        return np.zeros((0, width), dtype=np.int16)
    return np.asarray(rows[::-1], dtype=np.int16)


def _encode_row(row: np.ndarray, out: bytearray) -> None:
    x = 0
    width = len(row)
    while x < width:
        if row[x] == TRANSPARENT:
            n = 1
            while x + n < width and n < 0x80 and row[x + n] == TRANSPARENT:
                n += 1
            out.append(256 - n)
        else:
            n = 1
            while x + n < width and n < 0x7F and row[x + n] != TRANSPARENT:
                n += 1
            out.append(n)
            out += row[x : x + n].astype(np.uint8).tobytes()
        x += n


def encode_frame(image: IndexedImage, frame_header: bool = False) -> bytes:
    """Encodes one indexed image as a CEL frame."""
    image = as_indexed(image)
    height = image.shape[0]
    out = bytearray(FRAME_HEADER_SIZE if frame_header else 0)
    block_offsets = [0, 0, 0, 0]
    for y, row in enumerate(image[::-1]):
        if frame_header and y and y % _BLOCK_ROWS == 0 and y // _BLOCK_ROWS <= 4:
            block_offsets[y // _BLOCK_ROWS - 1] = len(out)
        _encode_row(row, out)
    if frame_header:
        if height == 0:
            raise ValueError("a CEL frame with a header needs at least one row")
        struct.pack_into("<5H", out, 0, FRAME_HEADER_SIZE, *block_offsets)
    return bytes(out)


def _widths(width: int | Sequence[int], count: int) -> list[int]:
    if isinstance(width, int):
        return [width] * count
    widths = list(width)
    if len(widths) < count:
        raise ValueError(f"{count} frames but only {len(widths)} widths")
    return widths


def decode(
    data: bytes,
    width: int | Sequence[int],
    grouped: bool | None = None,
    frame_header: bool | None = None,
) -> list[list[IndexedImage]]:
    """Decodes a CEL file into groups of frames.

    ``width`` is one width for every frame or a per-frame list (indexed by the
    frame number within its group, as in the engine).
    """
    groups = read_container(data, grouped)
    result: list[list[IndexedImage]] = []
    for frames in groups:
        widths = _widths(width, len(frames))
        result.append([decode_frame(f, w, frame_header) for f, w in zip(frames, widths)])
    return result


def encode(
    groups: Sequence[Sequence[IndexedImage]],
    grouped: bool | None = None,
    frame_header: bool = False,
) -> bytes:
    """Encodes groups of frames as a CEL file (a single group is not grouped by default)."""
    return write_container(
        [[encode_frame(img, frame_header) for img in frames] for frames in groups], grouped
    )
