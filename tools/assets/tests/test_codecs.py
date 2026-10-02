"""Round-trip and format tests for PAL, TRN, CEL, CL2, CLX, PCX and containers."""

from __future__ import annotations

import struct

import numpy as np
import pytest
from hypothesis import given
from hypothesis import strategies as st

from conftest import indexed_images, palettes
from d1assets import _runs, cel, cl2, clx, container, pal, pcx, trn
from d1assets.image import TRANSPARENT, from_rgba_exact, to_rgba


def assert_groups_equal(a: list[list[np.ndarray]], b: list[list[np.ndarray]]) -> None:
    assert len(a) == len(b)
    for ga, gb in zip(a, b):
        assert len(ga) == len(gb)
        for fa, fb in zip(ga, gb):
            np.testing.assert_array_equal(fa, fb)


# --- PAL / TRN -------------------------------------------------------------------------------------------


@given(palettes())
def test_pal_round_trip(palette: np.ndarray) -> None:
    data = pal.encode(palette)
    assert len(data) == 768
    np.testing.assert_array_equal(pal.decode(data), palette)


def test_pal_rejects_wrong_size() -> None:
    with pytest.raises(ValueError):
        pal.decode(bytes(767))


@given(st.binary(min_size=256, max_size=256), indexed_images(max_width=20, max_height=20))
def test_trn_round_trip_and_apply(data: bytes, image: np.ndarray) -> None:
    table = trn.decode(data)
    assert trn.encode(table) == data
    out = trn.apply(image, table)
    opaque = image != TRANSPARENT
    np.testing.assert_array_equal(out[~opaque], TRANSPARENT)
    np.testing.assert_array_equal(out[opaque], table[image[opaque]])


def test_trn_identity() -> None:
    image = np.array([[0, 255, TRANSPARENT]], dtype=np.int16)
    np.testing.assert_array_equal(trn.apply(image, trn.identity()), image)


# --- image helpers ---------------------------------------------------------------------------------------


@given(indexed_images(max_width=30, max_height=30), palettes(unique=True))
def test_rgba_exact_round_trip(image: np.ndarray, palette: np.ndarray) -> None:
    np.testing.assert_array_equal(from_rgba_exact(to_rgba(image, palette), palette), image)


# --- containers ------------------------------------------------------------------------------------------


@given(st.lists(st.lists(st.binary(max_size=40), max_size=6), min_size=1, max_size=5), st.booleans())
def test_container_round_trip(groups: list[list[bytes]], grouped: bool) -> None:
    if not grouped:
        groups = groups[:1]
    data = container.write_container(groups, grouped)
    assert container.read_container(data, grouped) == groups


def test_container_detection() -> None:
    single = container.write_container([[b"ab", b"cde"]])
    assert not container.is_grouped(single)
    grouped = container.write_container([[b"ab"], [b"cde", b"f"]])
    assert container.is_grouped(grouped)
    assert container.read_container(grouped) == [[b"ab"], [b"cde", b"f"]]


def test_container_with_shared_frame_data_area() -> None:
    # Diablo's grouped files put all list headers first and all frame data after them.
    data = (
        struct.pack("<2I", 8, 20)  # two groups: lists at 8 and 20
        + struct.pack("<3I", 1, 32 - 8, 35 - 8)  # list 0: one frame at absolute 32..35
        + struct.pack("<3I", 1, 35 - 20, 37 - 20)  # list 1: one frame at absolute 35..37
        + b"AAABB"
    )
    assert container.read_container(data, grouped=True) == [[b"AAA"], [b"BB"]]


def test_container_rejects_bad_offsets() -> None:
    with pytest.raises(ValueError):
        container.read_frame_list(struct.pack("<3I", 1, 12, 99))


# --- CEL -------------------------------------------------------------------------------------------------


@given(st.lists(indexed_images(), min_size=1, max_size=4), st.booleans())
def test_cel_round_trip(frames: list[np.ndarray], frame_header: bool) -> None:
    data = cel.encode([frames], frame_header=frame_header)
    widths = [f.shape[1] for f in frames]
    assert_groups_equal(cel.decode(data, widths, grouped=False, frame_header=frame_header), [frames])


@given(
    st.lists(
        st.lists(indexed_images(max_width=40, max_height=40), min_size=1, max_size=3), min_size=2, max_size=4
    )
)
def test_cel_grouped_round_trip(groups: list[list[np.ndarray]]) -> None:
    width = 17
    groups = [[f[:, :1].repeat(width, axis=1) for f in g] for g in groups]
    data = cel.encode(groups)
    assert_groups_equal(cel.decode(data, width, grouped=True, frame_header=False), groups)


def test_cel_encoding_details() -> None:
    image = np.array([[1, 2, TRANSPARENT], [TRANSPARENT] * 3], dtype=np.int16)
    frame = cel.encode_frame(image)
    # Bottom row first: 3 transparent; then 2 opaque pixels and 1 transparent.
    assert frame == bytes([0xFD, 0x02, 1, 2, 0xFF])
    np.testing.assert_array_equal(cel.decode_frame(frame, 3), image)


def test_cel_run_limits() -> None:
    image = np.concatenate([np.full(300, TRANSPARENT), np.full(300, 7)]).astype(np.int16)[None, :]
    frame = cel.encode_frame(image)
    assert frame[0] == 0x80  # longest transparent run: 128
    assert 0x7F in frame  # longest opaque run: 127
    np.testing.assert_array_equal(cel.decode_frame(frame, 600), image)


def test_cel_frame_header_offsets() -> None:
    image = np.zeros((70, 4), dtype=np.int16)
    frame = cel.encode_frame(image, frame_header=True)
    header = struct.unpack_from("<5H", frame)
    assert header == (10, 10 + 32 * 5, 10 + 64 * 5, 0, 0)
    assert cel.has_frame_header(frame)


def test_cel_wrong_width_is_an_error() -> None:
    frame = cel.encode_frame(np.zeros((2, 5), dtype=np.int16))
    with pytest.raises(ValueError):
        cel.decode_frame(frame, 3)


# --- CL2 -------------------------------------------------------------------------------------------------


@given(st.lists(st.lists(indexed_images(max_height=140), min_size=1, max_size=3), min_size=1, max_size=4))
def test_cl2_round_trip(groups: list[list[np.ndarray]]) -> None:
    width = groups[0][0].shape[1]
    groups = [[np.resize(f, (f.shape[0], width)) for f in g] for g in groups]
    grouped = len(groups) > 1
    data = cl2.encode(groups, grouped)
    assert_groups_equal(cl2.decode(data, width, grouped), groups)


@given(indexed_images(max_height=140))
def test_cl2_block_offsets_point_at_row_starts(image: np.ndarray) -> None:
    frame = cl2.encode_frame(image)
    header = struct.unpack_from("<5H", frame)
    assert header[0] == 10
    width = image.shape[1]
    for block, offset in enumerate(header[1:], start=1):
        if block * 32 >= image.shape[0]:
            assert offset == 0
            continue
        # Decoding from the offset yields exactly the rows above this block.
        rest = _runs.decode_stream(frame, offset)
        np.testing.assert_array_equal(rest, image[::-1][block * 32 :].reshape(-1))
        assert len(rest) % width == 0


# --- CLX -------------------------------------------------------------------------------------------------


def test_clx_matches_engine_encoder() -> None:
    image = np.array([[5, 5, 5, TRANSPARENT, TRANSPARENT, 7]], dtype=np.int16)
    assert clx.encode_frame(image) == bytes.fromhex("060006000100bc0502ff07")


def test_clx_long_runs() -> None:
    row = np.concatenate([np.full(200, TRANSPARENT), np.full(100, 9), np.arange(100) % 256]).astype(np.int16)
    frame = clx.encode_frame(row[None, :])
    np.testing.assert_array_equal(clx.decode_frame(frame), row[None, :])
    assert frame[6] == 0x7F  # transparent runs are split at 127
    assert 0x80 in frame  # fill runs are split at 63


@given(st.lists(st.lists(indexed_images(), min_size=1, max_size=3), min_size=1, max_size=4))
def test_clx_round_trip(groups: list[list[np.ndarray]]) -> None:
    grouped = len(groups) > 1
    data = clx.encode(groups, grouped)
    assert_groups_equal(clx.decode(data, grouped), groups)
    for frames, raw in zip(groups, container.read_container(data, grouped)):
        for frame, blob in zip(frames, raw):
            assert clx.frame_size(blob) == (frame.shape[1], frame.shape[0])


@given(st.lists(indexed_images(max_width=60, max_height=40), min_size=1, max_size=3))
def test_clx_from_cel_and_cl2(frames: list[np.ndarray]) -> None:
    widths = [f.shape[1] for f in frames]
    from_cel = clx.from_cel(cel.encode([frames]), widths, grouped=False)
    assert_groups_equal(clx.decode(from_cel, grouped=False), [frames])
    from_cl2 = clx.from_cl2(cl2.encode([frames]), widths, grouped=False)
    assert_groups_equal(clx.decode(from_cl2, grouped=False), [frames])


def test_clx_rejects_size_mismatch() -> None:
    frame = bytearray(clx.encode_frame(np.zeros((2, 2), dtype=np.int16)))
    struct.pack_into("<H", frame, 4, 3)
    with pytest.raises(ValueError):
        clx.decode_frame(bytes(frame))


# --- PCX -------------------------------------------------------------------------------------------------


@given(indexed_images(max_width=90, max_height=40, transparent=False), st.booleans(), palettes())
def test_pcx_round_trip(image: np.ndarray, with_palette: bool, palette: np.ndarray) -> None:
    pixels = image.astype(np.uint8)
    data = pcx.encode(pixels, palette if with_palette else None)
    decoded = pcx.decode(data)
    np.testing.assert_array_equal(decoded.pixels, pixels)
    if with_palette:
        np.testing.assert_array_equal(decoded.palette, palette)
    else:
        assert decoded.palette is None


def test_pcx_escapes_high_literals() -> None:
    pixels = np.array([[0xC5, 1, 0xC0]], dtype=np.uint8)
    data = pcx.encode(pixels, None)
    assert data[128:] == bytes([0xC1, 0xC5, 1, 0xC1, 0xC0, 0])  # odd width: one pad byte
    np.testing.assert_array_equal(pcx.decode(data).pixels, pixels)


def test_pcx_rejects_other_formats() -> None:
    data = bytearray(pcx.encode(np.zeros((2, 2), dtype=np.uint8), None))
    data[3] = 4  # 4 bits per pixel
    with pytest.raises(ValueError):
        pcx.decode(bytes(data))
