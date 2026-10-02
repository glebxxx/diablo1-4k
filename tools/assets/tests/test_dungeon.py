"""Level CEL micro-tiles (6 encodings), MIN pieces and TIL mega-tiles."""

from __future__ import annotations

import numpy as np
import pytest
from hypothesis import given
from hypothesis import strategies as st

from conftest import indexed_images
from d1assets import dungeon as D
from d1assets.image import TRANSPARENT

FIXED_TYPES = [t for t in D.TileType if t != D.TileType.TRANSPARENT_SQUARE]


@st.composite
def micro_tiles(draw: st.DrawFn, tile_type: D.TileType) -> np.ndarray:
    transparent = tile_type == D.TileType.TRANSPARENT_SQUARE
    image = draw(indexed_images(32, 32, 32, 32, transparent=transparent))
    image[~D.shape_mask(tile_type)] = TRANSPARENT
    return image


@pytest.mark.parametrize(
    ("tile_type", "padded_size", "unpadded_size"),
    [
        (D.TileType.SQUARE, 1024, 1024),
        (D.TileType.LEFT_TRIANGLE, 544, 512),
        (D.TileType.RIGHT_TRIANGLE, 544, 512),
        (D.TileType.LEFT_TRAPEZOID, 800, 784),
        (D.TileType.RIGHT_TRAPEZOID, 800, 784),
    ],
)
def test_fixed_shape_sizes(tile_type: D.TileType, padded_size: int, unpadded_size: int) -> None:
    # Sizes from dun_tile.hpp: ReencodedTriangleFrameSize = 544 - 32, ReencodedTrapezoidFrameSize = 800 - 16.
    image = np.where(D.shape_mask(tile_type), 3, TRANSPARENT).astype(np.int16)
    assert len(D.encode_micro(image, tile_type)) == padded_size
    assert len(D.encode_micro(image, tile_type, padded=False)) == unpadded_size


@pytest.mark.parametrize("tile_type", list(D.TileType))
@given(data=st.data(), padded=st.booleans())
def test_micro_round_trip(tile_type: D.TileType, data: st.DataObject, padded: bool) -> None:
    image = data.draw(micro_tiles(tile_type))
    encoded = D.encode_micro(image, tile_type, padded=padded)
    np.testing.assert_array_equal(D.decode_micro(encoded, tile_type, padded=padded), image)


def test_shapes() -> None:
    left = D.shape_mask(D.TileType.LEFT_TRIANGLE)
    right = D.shape_mask(D.TileType.RIGHT_TRIANGLE)
    assert not left[0].any() and not right[0].any()  # the top row is empty
    assert left[31, 30:].all() and not left[31, :30].any()  # bottom row: 2 px on the right
    assert right[31, :2].all() and not right[31, 2:].any()  # bottom row: 2 px on the left
    assert left[16].all() and right[16].all()  # the widest row
    np.testing.assert_array_equal(left, right[:, ::-1])
    trapezoid = D.shape_mask(D.TileType.LEFT_TRAPEZOID)
    assert trapezoid[:16].all()
    np.testing.assert_array_equal(trapezoid[16:], left[16:])
    assert D.shape_mask(D.TileType.SQUARE).all()


def test_left_triangle_padding_comes_before_even_rows() -> None:
    image = np.where(D.shape_mask(D.TileType.LEFT_TRIANGLE), 9, TRANSPARENT).astype(np.int16)
    left = D.encode_micro(image, D.TileType.LEFT_TRIANGLE)
    assert left[:4] == bytes([0, 0, 9, 9])  # row 0: pad, 2 px
    right = D.encode_micro(image[:, ::-1], D.TileType.RIGHT_TRIANGLE)
    assert right[:4] == bytes([9, 9, 0, 0])  # row 0: 2 px, pad


def test_transparent_square_encoding() -> None:
    image = np.full((32, 32), TRANSPARENT, dtype=np.int16)
    image[31, :3] = [1, 2, 3]
    encoded = D.encode_micro(image, D.TileType.TRANSPARENT_SQUARE)
    assert encoded[:5] == bytes([3, 1, 2, 3, 256 - 29])
    assert encoded[5 : 5 + 31] == bytes([256 - 32]) * 31
    assert len(encoded) % 4 == 0  # padded with zeros, like Diablo's files
    assert len(D.encode_micro(image, D.TileType.TRANSPARENT_SQUARE, padded=False)) == 36


def test_hole_in_fixed_shape_is_an_error() -> None:
    image = np.zeros((32, 32), dtype=np.int16)
    image[20, 10] = TRANSPARENT
    with pytest.raises(ValueError):
        D.encode_micro(image, D.TileType.SQUARE)


def test_block_values() -> None:
    block = D.make_block(0x123, D.TileType.RIGHT_TRAPEZOID)
    assert block == 0x5123
    assert D.block_frame(block) == 0x123
    assert D.block_type(block) == D.TileType.RIGHT_TRAPEZOID


@given(st.lists(st.lists(st.integers(0, 0xFFFF), min_size=10, max_size=10), max_size=20))
def test_min_round_trip(pieces: list[list[int]]) -> None:
    assert D.decode_min(D.encode_min(pieces), 10) == pieces


@given(st.lists(st.tuples(*[st.integers(0, 0xFFFF)] * 4), max_size=20))
def test_til_round_trip(tiles: list[tuple[int, int, int, int]]) -> None:
    assert D.decode_til(D.encode_til(tiles)) == tiles


@st.composite
def levels(draw: st.DrawFn, blocks: int = 10) -> tuple[list[tuple[np.ndarray, D.TileType]], list[list[int]]]:
    """A synthetic level CEL (list of micro-tiles) and a MIN that uses each frame once."""
    count = draw(st.integers(1, 12))
    micros = []
    for _ in range(count):
        tile_type = draw(st.sampled_from(list(D.TileType)))
        micros.append((draw(micro_tiles(tile_type)), tile_type))
    frames = [D.make_block(i + 1, t) for i, (_, t) in enumerate(micros)]
    frames += [0] * (-len(frames) % blocks)
    pieces = [frames[i : i + blocks] for i in range(0, len(frames), blocks)]
    return micros, pieces


@given(levels())
def test_level_cel_round_trip(level: tuple[list[tuple[np.ndarray, D.TileType]], list[list[int]]]) -> None:
    micros, pieces = level
    data = D.encode_level_cel(micros)
    decoded = D.decode_level_cel(data, pieces)
    assert sorted(decoded) == list(range(1, len(micros) + 1))
    for frame, (image, _) in enumerate(micros, start=1):
        np.testing.assert_array_equal(decoded[frame], image)


@given(levels(blocks=16), st.sampled_from([10, 16]))
def test_piece_assembly_round_trip(level: tuple, blocks: int) -> None:
    micros, pieces = level
    pieces = [p[:blocks] for p in pieces]
    decoded = D.decode_level_cel(D.encode_level_cel(micros), pieces)
    for piece in pieces:
        image = D.render_piece(piece, decoded)
        assert image.shape == (blocks // 2 * 32, 64)
        split = D.split_piece(image, [D.block_type(b) for b in piece])
        for block, micro in zip(piece, split):
            if D.block_frame(block) == 0 or (decoded[D.block_frame(block)] == TRANSPARENT).all():
                assert micro is None
            else:
                np.testing.assert_array_equal(micro, decoded[D.block_frame(block)])


def test_piece_layout_matches_engine() -> None:
    # MIN stores the top row first. The engine reads
    # mt[block] = pieces[blocks - 2 + (block & 1) - (block & 0xE)],
    # so mt[0] / mt[1] (bottom-left / bottom-right) are the last two values in the file.
    blocks = 10
    micros = {i: np.full((32, 32), i, dtype=np.int16) for i in range(1, blocks + 1)}
    piece = [D.make_block(i, D.TileType.SQUARE) for i in range(1, blocks + 1)]
    image = D.render_piece(piece, micros)
    for block in range(blocks):
        file_index = blocks - 2 + (block & 1) - (block & 0xE)
        row_from_bottom, column = block // 2, block % 2
        y = image.shape[0] - 32 * (row_from_bottom + 1)
        assert image[y, 32 * column] == file_index + 1


def test_megatile_layout() -> None:
    piece_images = [np.full((64, 64), i, dtype=np.int16) for i in range(4)]
    for image in piece_images:
        image[:, :2] = TRANSPARENT
    image = D.render_megatile((0, 1, 2, 3), piece_images)
    assert image.shape == (96, 128)
    assert image[0, 40] == 0  # top piece: x 32..96, y 0..64
    assert image[20, 10] == 2  # left piece: x 0..64, y 16..80
    assert image[20, 120] == 1  # right piece: x 64..128, y 16..80
    assert image[90, 60] == 3  # bottom piece, drawn last: x 32..96, y 32..96
    assert image[0, 0] == TRANSPARENT
