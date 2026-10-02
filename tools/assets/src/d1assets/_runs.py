"""CL2/CLX pixel-run commands (``Source/utils/clx_encode.hpp``, ``clx_decode.hpp``).

Commands, applied to a bottom-up stream of pixels that may cross rows:

* ``0x01..0x7F``: ``n`` transparent pixels.
* ``0x80..0xBE``: fill run of ``0xBF - n`` (1..63) pixels; one colour byte follows.
* ``0xBF..0xFF``: ``256 - n`` (1..65) literal pixels follow.
"""

from __future__ import annotations

from itertools import pairwise

import numpy as np
import numpy.typing as npt

from .image import TRANSPARENT

MIN_FILL_RUN_LENGTH = 3


def append_transparent_run(width: int, out: bytearray) -> None:
    while width >= 0x7F:
        out.append(0x7F)
        width -= 0x7F
    if width:
        out.append(width)


def append_fill_run(color: int, width: int, out: bytearray) -> None:
    while width >= 0x3F:
        out += bytes((0x80, color))
        width -= 0x3F
    if width:
        out += bytes((0xBF - width, color))


def append_pixels_run(pixels: bytes, out: bytearray) -> None:
    pos = 0
    width = len(pixels)
    while width >= 0x41:
        out.append(0xBF)
        out += pixels[pos : pos + 0x41]
        pos += 0x41
        width -= 0x41
    if width:
        out.append(256 - width)
        out += pixels[pos : pos + width]


def append_pixels_or_fill_run(pixels: bytes, out: bytearray) -> None:
    """Port of ``AppendClxPixelsOrFillRun``: literal runs, with fill runs for repeats."""
    begin = 0
    prev_begin = 0
    prev_len = 1
    prev = pixels[0]
    for pos in range(1, len(pixels)):
        color = pixels[pos]
        if color == prev:
            prev_len += 1
            continue
        if prev_len >= MIN_FILL_RUN_LENGTH:
            append_pixels_run(pixels[begin:prev_begin], out)
            append_fill_run(prev, prev_len, out)
            begin = pos
        prev_begin = pos
        prev_len = 1
        prev = color
    if prev_len >= 2:
        append_pixels_run(pixels[begin:prev_begin], out)
        append_fill_run(prev, prev_len, out)
    else:
        append_pixels_run(pixels[begin : prev_begin + prev_len], out)


def encode_stream(stream: npt.NDArray[np.int16], out: bytearray) -> None:
    """Encodes a flat pixel stream (``TRANSPARENT`` allowed) into run commands."""
    n = len(stream)
    if n == 0:
        return
    opaque = stream != TRANSPARENT
    # Boundaries of maximal opaque / transparent segments.
    change = np.flatnonzero(opaque[1:] != opaque[:-1]) + 1
    bounds = [0, *change.tolist(), n]
    for begin, end in pairwise(bounds):
        if opaque[begin]:
            append_pixels_or_fill_run(stream[begin:end].astype(np.uint8).tobytes(), out)
        else:
            append_transparent_run(end - begin, out)


def decode_stream(data: bytes, start: int = 0, end: int | None = None) -> npt.NDArray[np.int16]:
    """Decodes run commands into a flat pixel stream."""
    if end is None:
        end = len(data)
    out: list[int] = []
    pos = start
    while pos < end:
        control = data[pos]
        pos += 1
        if control == 0:
            raise ValueError(f"invalid CL2/CLX command 0x00 at offset {pos - 1}")
        if control < 0x80:
            out.extend([TRANSPARENT] * control)
        elif control <= 0xBE:
            if pos >= end:
                raise ValueError("truncated fill run")
            out.extend([data[pos]] * (0xBF - control))
            pos += 1
        else:
            width = 256 - control
            if pos + width > end:
                raise ValueError("truncated pixel run")
            out.extend(data[pos : pos + width])
            pos += width
    return np.asarray(out, dtype=np.int16)
