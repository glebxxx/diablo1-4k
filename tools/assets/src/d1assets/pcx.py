"""PCX images, 8 bits per pixel, one plane, with a 256-colour palette.

Layout: a 128-byte header (``Source/utils/pcx.hpp``), RLE scanlines
(``0xC0 | n`` repeats the next byte ``n`` times, 1..63; any other byte below
0xC0 is a literal), then ``0x0C`` and 768 palette bytes.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from .image import Palette

HEADER_SIZE = 128
PALETTE_SEPARATOR = 0x0C
_HEADER = struct.Struct("<4B6H48sBBHHHH54s")


@dataclass
class PcxImage:
    pixels: npt.NDArray[np.uint8]  # (height, width) palette indices; PCX has no transparency
    palette: Palette | None


def decode(data: bytes) -> PcxImage:
    if len(data) < HEADER_SIZE:
        raise ValueError("truncated PCX header")
    fields = _HEADER.unpack_from(data, 0)
    manufacturer, _version, encoding, bpp, xmin, ymin, xmax, ymax = fields[:8]
    nplanes, bytes_per_line = fields[12], fields[13]
    if manufacturer != 0x0A or encoding != 1:
        raise ValueError("not an RLE PCX file")
    if bpp != 8 or nplanes != 1:
        raise ValueError(f"only 8-bit single-plane PCX is supported (bpp={bpp}, planes={nplanes})")
    width, height = xmax - xmin + 1, ymax - ymin + 1
    if width <= 0 or height <= 0 or bytes_per_line < width:
        raise ValueError("invalid PCX dimensions")
    total = bytes_per_line * height
    out = bytearray()
    pos = HEADER_SIZE
    while len(out) < total:
        if pos >= len(data):
            raise ValueError("truncated PCX image data")
        byte = data[pos]
        pos += 1
        if byte >= 0xC0:
            if pos >= len(data):
                raise ValueError("truncated PCX run")
            out += bytes([data[pos]]) * (byte & 0x3F)
            pos += 1
        else:
            out.append(byte)
    pixels = np.frombuffer(bytes(out[:total]), dtype=np.uint8).reshape(height, bytes_per_line)[:, :width]
    palette = None
    if len(data) - pos >= 769 and data[pos] == PALETTE_SEPARATOR:
        palette = np.frombuffer(data[pos + 1 : pos + 769], dtype=np.uint8).reshape(256, 3).copy()
    return PcxImage(pixels.copy(), palette)


def _encode_line(line: bytes, out: bytearray) -> None:
    pos = 0
    n = len(line)
    while pos < n:
        byte = line[pos]
        run = 1
        while pos + run < n and run < 0x3F and line[pos + run] == byte:
            run += 1
        if run > 1 or byte >= 0xC0:
            out += bytes((0xC0 | run, byte))
        else:
            out.append(byte)
        pos += run


def encode(pixels: npt.ArrayLike, palette: Palette | None) -> bytes:
    arr = np.asarray(pixels)
    if arr.ndim != 2 or arr.shape[0] == 0 or arr.shape[1] == 0:
        raise ValueError("PCX pixels must be a non-empty 2-D array")
    if arr.min() < 0 or arr.max() > 255:
        raise ValueError("PCX pixels must be in 0..255")
    height, width = arr.shape
    bytes_per_line = width + (width & 1)
    header = _HEADER.pack(
        *(0x0A, 5, 1, 8),  # manufacturer, version, RLE encoding, bits per pixel
        *(0, 0, width - 1, height - 1, 72, 72),  # window, DPI
        bytes(48),  # 16-colour palette (unused)
        *(0, 1, bytes_per_line, 1, 0, 0),  # reserved, planes, bytes per line, palette info, screen size
        bytes(54),
    )
    out = bytearray(header)
    padded = np.zeros((height, bytes_per_line), dtype=np.uint8)
    padded[:, :width] = arr
    for row in padded:
        _encode_line(row.tobytes(), out)
    if palette is not None:
        pal = np.asarray(palette, dtype=np.uint8)
        if pal.shape != (256, 3):
            raise ValueError("palette must have shape (256, 3)")
        out.append(PALETTE_SEPARATOR)
        out += pal.tobytes()
    return bytes(out)
