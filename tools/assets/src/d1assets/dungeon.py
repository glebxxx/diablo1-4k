"""Dungeon tiles: level CEL micro-tiles, MIN pieces and TIL mega-tiles.

References: ``Source/levels/dun_tile.hpp``, ``reencode_dun_cels.cpp`` and
``dun_tile_data.cpp``.

* A level CEL (e.g. ``levels\\l1data\\l1.cel``) is a plain frame list whose
  frames are 32x32 *micro-tiles*. A frame does not say how it is encoded: the
  encoding (:class:`TileType`) is stored in the MIN file that references it.
* A MIN file lists *pieces* (dungeon squares). Each piece is ``blocks``
  ``uint16`` values (10 for most levels, 16 for town and hell), stored top row
  first, left then right column; a piece image is 64 px wide and
  ``blocks / 2 * 32`` px tall, with the floor in the bottom 32 rows. Each
  value is ``(type << 12) | frame``, where ``frame`` is 1-based (0 = empty).
* A TIL file lists *mega-tiles*: four ``uint16`` 0-based piece indices (top,
  right, left, bottom).

All micro-tile encodings store rows bottom-up.
"""

from __future__ import annotations

import struct
from collections.abc import Mapping, Sequence
from enum import IntEnum

import numpy as np
import numpy.typing as npt

from .container import read_frame_list, write_frame_list
from .image import TRANSPARENT, IndexedImage, as_indexed, new_image

MICRO_WIDTH = 32
MICRO_HEIGHT = 32
PIECE_WIDTH = 64
TILE_WIDTH = 128


class TileType(IntEnum):
    SQUARE = 0
    """32x32 opaque pixels."""
    TRANSPARENT_SQUARE = 1
    """32x32 with transparency; per-row runs of int8 (>0 opaque count, <0 transparent count)."""
    LEFT_TRIANGLE = 2
    """Right-aligned rows of 2..32..2 px (31 rows, top row empty); 2 pad bytes before even rows."""
    RIGHT_TRIANGLE = 3
    """Left-aligned rows of 2..32..2 px; 2 pad bytes after even rows."""
    LEFT_TRAPEZOID = 4
    """Lower half of LEFT_TRIANGLE, then 16 full 32 px rows."""
    RIGHT_TRAPEZOID = 5
    """Lower half of RIGHT_TRIANGLE, then 16 full 32 px rows."""


# Triangle row widths, bottom-up: 2, 4, ..., 32, 30, ..., 2.
_TRIANGLE_WIDTHS = [2 * (i + 1) if i <= 15 else 2 * (31 - i) for i in range(31)]


def _rows(tile_type: TileType) -> list[tuple[int, int]]:
    """Bottom-up list of ``(x_start, width)`` for each stored row of a fixed-shape type."""
    if tile_type == TileType.SQUARE:
        return [(0, MICRO_WIDTH)] * MICRO_HEIGHT
    if tile_type in (TileType.LEFT_TRIANGLE, TileType.RIGHT_TRIANGLE):
        widths = _TRIANGLE_WIDTHS
    elif tile_type in (TileType.LEFT_TRAPEZOID, TileType.RIGHT_TRAPEZOID):
        widths = _TRIANGLE_WIDTHS[:16] + [MICRO_WIDTH] * 16
    else:
        raise ValueError(f"{tile_type!r} has no fixed shape")
    right_aligned = tile_type in (TileType.LEFT_TRIANGLE, TileType.LEFT_TRAPEZOID)
    return [((MICRO_WIDTH - w) if right_aligned else 0, w) for w in widths]


def _padding(tile_type: TileType, row: int) -> tuple[int, int]:
    """Pad bytes ``(before, after)`` stored around bottom-up ``row`` in the original data."""
    if tile_type == TileType.SQUARE or row % 2:
        return 0, 0
    if tile_type in (TileType.LEFT_TRAPEZOID, TileType.RIGHT_TRAPEZOID) and row >= 16:
        return 0, 0
    if tile_type in (TileType.LEFT_TRIANGLE, TileType.LEFT_TRAPEZOID):
        return 2, 0
    return 0, 2


def shape_mask(tile_type: TileType) -> npt.NDArray[np.bool_]:
    """Pixels a micro-tile of ``tile_type`` can cover (all of them for transparent squares)."""
    mask = np.zeros((MICRO_HEIGHT, MICRO_WIDTH), dtype=bool)
    if tile_type == TileType.TRANSPARENT_SQUARE:
        mask[:] = True
        return mask
    for i, (x, w) in enumerate(_rows(tile_type)):
        mask[MICRO_HEIGHT - 1 - i, x : x + w] = True
    return mask


def decode_micro(data: bytes, tile_type: TileType | int, padded: bool = True) -> IndexedImage:
    """Decodes one 32x32 micro-tile.

    Trailing bytes after the last row (alignment padding) are ignored.

    ``padded=False`` reads DevilutionX's re-encoded form, where triangle and
    trapezoid pad bytes have been removed.
    """
    tile_type = TileType(tile_type)
    image = new_image(MICRO_WIDTH, MICRO_HEIGHT)
    pos = 0
    if tile_type == TileType.TRANSPARENT_SQUARE:
        for i in range(MICRO_HEIGHT):
            y = MICRO_HEIGHT - 1 - i
            x = 0
            while x < MICRO_WIDTH:
                if pos >= len(data):
                    raise ValueError("truncated transparent micro-tile")
                run = struct.unpack_from("<b", data, pos)[0]
                pos += 1
                if run > 0:
                    if x + run > MICRO_WIDTH or pos + run > len(data):
                        raise ValueError("transparent micro-tile run overflows its row")
                    image[y, x : x + run] = np.frombuffer(data, np.uint8, run, pos)
                    pos += run
                    x += run
                elif run < 0:
                    x -= run
                else:
                    raise ValueError("zero-length run in transparent micro-tile")
            if x != MICRO_WIDTH:
                raise ValueError("transparent micro-tile run crosses a row boundary")
        return image
    for i, (x, w) in enumerate(_rows(tile_type)):
        before, after = _padding(tile_type, i) if padded else (0, 0)
        pos += before
        if pos + w > len(data):
            raise ValueError(f"truncated {tile_type.name} micro-tile")
        image[MICRO_HEIGHT - 1 - i, x : x + w] = np.frombuffer(data, np.uint8, w, pos)
        pos += w + after
    return image


def encode_micro(image: IndexedImage, tile_type: TileType | int, padded: bool = True) -> bytes:
    """Encodes a 32x32 micro-tile. Pixels outside the type's shape are ignored.

    With ``padded=True`` (the default) the output matches Diablo's level CEL
    files byte for byte, including zero pad bytes.

    Raises ``ValueError`` if a pixel inside a fixed shape is transparent.
    """
    tile_type = TileType(tile_type)
    image = as_indexed(image)
    if image.shape != (MICRO_HEIGHT, MICRO_WIDTH):
        raise ValueError(f"micro-tiles are {MICRO_WIDTH}x{MICRO_HEIGHT}, got {image.shape[::-1]}")
    out = bytearray()
    if tile_type == TileType.TRANSPARENT_SQUARE:
        for row in image[::-1]:
            x = 0
            while x < MICRO_WIDTH:
                opaque = row[x] != TRANSPARENT
                n = 1
                while x + n < MICRO_WIDTH and (row[x + n] != TRANSPARENT) == opaque:
                    n += 1
                if opaque:
                    out.append(n)
                    out += row[x : x + n].astype(np.uint8).tobytes()
                else:
                    out.append(256 - n)
                x += n
        if padded:
            # Diablo's level CELs pad transparent micro-tiles with zeros to a multiple of 4 bytes.
            out += bytes(-len(out) % 4)
        return bytes(out)
    for i, (x, w) in enumerate(_rows(tile_type)):
        pixels = image[MICRO_HEIGHT - 1 - i, x : x + w]
        if (pixels == TRANSPARENT).any():
            raise ValueError(f"{tile_type.name} micro-tile has a transparent pixel inside its shape")
        before, after = _padding(tile_type, i) if padded else (0, 0)
        out += bytes(before) + pixels.astype(np.uint8).tobytes() + bytes(after)
    return bytes(out)


# --- MIN -----------------------------------------------------------------------------------------------


def block_frame(block: int) -> int:
    """1-based level CEL frame of a MIN block value (0 = empty)."""
    return block & 0xFFF


def block_type(block: int) -> TileType:
    return TileType((block >> 12) & 0x7)


def make_block(frame: int, tile_type: TileType | int) -> int:
    if not 0 <= frame <= 0xFFF:
        raise ValueError("frame must be in 0..4095")
    return (int(TileType(tile_type)) << 12) | frame


def decode_min(data: bytes, blocks: int) -> list[list[int]]:
    """Splits a MIN file into pieces of ``blocks`` values, in file order."""
    if blocks <= 0 or blocks % 2:
        raise ValueError("blocks per piece must be a positive even number")
    if len(data) % (2 * blocks):
        raise ValueError(f"MIN size {len(data)} is not a multiple of {2 * blocks}")
    values = struct.unpack(f"<{len(data) // 2}H", data)
    return [list(values[i : i + blocks]) for i in range(0, len(values), blocks)]


def encode_min(pieces: Sequence[Sequence[int]]) -> bytes:
    if pieces and len({len(p) for p in pieces}) != 1:
        raise ValueError("all pieces must have the same number of blocks")
    flat = [v for piece in pieces for v in piece]
    return struct.pack(f"<{len(flat)}H", *flat)


def frame_types(pieces: Sequence[Sequence[int]]) -> dict[int, TileType]:
    """Maps each referenced 1-based level CEL frame to its encoding (first reference wins)."""
    types: dict[int, TileType] = {}
    for piece in pieces:
        for block in piece:
            frame = block_frame(block)
            if frame and frame not in types:
                types[frame] = block_type(block)
    return types


def decode_level_cel(data: bytes, pieces: Sequence[Sequence[int]]) -> dict[int, IndexedImage]:
    """Decodes the micro-tiles of a level CEL that ``pieces`` reference, keyed by 1-based frame."""
    frames = read_frame_list(data)
    result: dict[int, IndexedImage] = {}
    for frame, tile_type in sorted(frame_types(pieces).items()):
        if frame > len(frames):
            raise ValueError(f"MIN references frame {frame}, but the CEL has {len(frames)}")
        result[frame] = decode_micro(frames[frame - 1], tile_type)
    return result


def encode_level_cel(micros: Sequence[tuple[IndexedImage, TileType | int]]) -> bytes:
    """Encodes micro-tiles (frame 1 first) as a level CEL."""
    return write_frame_list([encode_micro(img, t) for img, t in micros])


def render_piece(piece: Sequence[int], micros: Mapping[int, IndexedImage]) -> IndexedImage:
    """Assembles a piece image (64 x ``blocks / 2 * 32``) from its micro-tiles."""
    rows = len(piece) // 2
    image = new_image(PIECE_WIDTH, rows * MICRO_HEIGHT)
    for k, block in enumerate(piece):
        frame = block_frame(block)
        if not frame:
            continue
        micro = micros[frame]
        y, x = (k // 2) * MICRO_HEIGHT, (k % 2) * MICRO_WIDTH
        target = image[y : y + MICRO_HEIGHT, x : x + MICRO_WIDTH]
        opaque = micro != TRANSPARENT
        target[opaque] = micro[opaque]
    return image


def split_piece(image: IndexedImage, types: Sequence[TileType | int]) -> list[IndexedImage | None]:
    """Cuts a piece image into micro-tiles (file order); a fully transparent one becomes None.

    ``types`` gives the encoding for each block; pixels outside its shape are
    dropped. This is the inverse of :func:`render_piece` for pieces whose
    micro-tiles fill their shapes.
    """
    image = as_indexed(image)
    rows = image.shape[0] // MICRO_HEIGHT
    if image.shape != (rows * MICRO_HEIGHT, PIECE_WIDTH) or len(types) != 2 * rows:
        raise ValueError("piece image size does not match the number of blocks")
    result: list[IndexedImage | None] = []
    for k, tile_type in enumerate(types):
        y, x = (k // 2) * MICRO_HEIGHT, (k % 2) * MICRO_WIDTH
        micro = image[y : y + MICRO_HEIGHT, x : x + MICRO_WIDTH].copy()
        micro[~shape_mask(TileType(tile_type))] = TRANSPARENT
        result.append(None if (micro == TRANSPARENT).all() else micro)
    return result


# --- TIL -----------------------------------------------------------------------------------------------


def decode_til(data: bytes) -> list[tuple[int, int, int, int]]:
    """Reads mega-tiles as ``(top, right, left, bottom)`` 0-based piece indices."""
    if len(data) % 8:
        raise ValueError(f"TIL size {len(data)} is not a multiple of 8")
    return [struct.unpack_from("<4H", data, i) for i in range(0, len(data), 8)]


def encode_til(tiles: Sequence[Sequence[int]]) -> bytes:
    out = bytearray()
    for tile in tiles:
        if len(tile) != 4:
            raise ValueError("a mega-tile has exactly 4 pieces")
        out += struct.pack("<4H", *tile)
    return bytes(out)


def render_megatile(tile: Sequence[int], piece_images: Sequence[IndexedImage]) -> IndexedImage:
    """Assembles a mega-tile image (128 x piece height + 32), farthest piece first."""
    top, right, left, bottom = tile
    height = piece_images[top].shape[0]
    image = new_image(TILE_WIDTH, height + MICRO_HEIGHT)
    for index, x, y in ((top, 32, 0), (left, 0, 16), (right, 64, 16), (bottom, 32, 32)):
        piece = piece_images[index]
        target = image[y : y + piece.shape[0], x : x + PIECE_WIDTH]
        opaque = piece != TRANSPARENT
        target[opaque] = piece[opaque]
    return image
