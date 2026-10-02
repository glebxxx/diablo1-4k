from __future__ import annotations

import numpy as np
import pytest
from hypothesis import given
from hypothesis import strategies as st

from conftest import indexed_images, palettes
from d1assets import quantize as Q
from d1assets.image import TRANSPARENT, to_rgba


def test_light_ramps_match_make_light_table() -> None:
    ramps = Q.light_ramps()
    assert [len(r) for r in ramps] == [16] * 8 + [8] * 4 + [16] * 5 + [15, 1]
    assert ramps[0] == range(0, 16)
    assert ramps[8] == range(128, 136)
    assert ramps[12] == range(160, 176)
    assert ramps[-1] == range(255, 256)
    covered = [i for r in ramps for i in r]
    assert covered == list(range(256))
    ids = Q.ramp_ids()
    assert ids[15] == 0 and ids[16] == 1 and ids[159] == 11 and ids[254] == 17 and ids[255] == 18


def test_reserved_indices() -> None:
    assert Q.reserved_indices("caves") == frozenset(range(1, 32))
    assert Q.reserved_indices("nest") == frozenset(range(1, 16))
    assert Q.reserved_indices("town") == frozenset()
    assert Q.reserved_indices([(3, 4)]) == frozenset({3, 4})


@given(indexed_images(max_width=24, max_height=24), palettes(unique=True), st.sampled_from(Q.METRICS))
def test_nearest_is_exact_for_palette_colours(image: np.ndarray, palette: np.ndarray, metric: str) -> None:
    out = Q.quantize_nearest(to_rgba(image, palette), palette, metric=metric)
    np.testing.assert_array_equal(out, image)


@given(indexed_images(max_width=24, max_height=24), palettes(), st.sampled_from(sorted(Q.CYCLING_RANGES)))
def test_nearest_respects_exclusions(image: np.ndarray, palette: np.ndarray, cycling: str) -> None:
    reserved = Q.reserved_indices(cycling)
    out = Q.quantize_nearest(to_rgba(image, palette), palette, exclude=reserved)
    assert not set(np.unique(out).tolist()) & reserved
    np.testing.assert_array_equal(out == TRANSPARENT, image == TRANSPARENT)


def test_nearest_picks_closest_and_lowest_on_ties() -> None:
    palette = np.zeros((256, 3), dtype=np.uint8)
    palette[10] = (100, 100, 100)
    palette[11] = (100, 100, 100)
    palette[12] = (200, 0, 0)
    rgba = np.array([[[101, 99, 100, 255], [190, 10, 0, 255], [0, 0, 0, 0]]], dtype=np.uint8)
    out = Q.quantize_nearest(rgba, palette, exclude=[0])
    assert out.tolist() == [[10, 12, TRANSPARENT]]


@given(
    indexed_images(max_width=12, max_height=12),
    palettes(),
    st.integers(1, 4),
    st.sampled_from(sorted(Q.CYCLING_RANGES)),
    st.sampled_from(["alpha", "source"]),
    st.integers(0, 2**32 - 1),
)
def test_ramp_quantization_invariants(
    source: np.ndarray, palette: np.ndarray, scale: int, cycling: str, mask: str, seed: int
) -> None:
    # A noisy "upscaled" image: the source scaled up, with colour noise and some alpha changes.
    rng = np.random.default_rng(seed)
    up = Q.upscale_nearest(source, scale)
    rgba = to_rgba(up, palette).astype(np.int16)
    rgba[:, :, :3] += rng.integers(-40, 41, rgba[:, :, :3].shape)
    flip = rng.random(up.shape) < 0.1
    rgba[:, :, 3] = np.where(flip, 255 - rgba[:, :, 3], rgba[:, :, 3])
    rgba = np.clip(rgba, 0, 255).astype(np.uint8)

    out = Q.quantize_ramp(rgba, palette, source, cycling=cycling, mask=mask)
    reserved = Q.reserved_indices(cycling)
    ids = Q.ramp_ids()
    expected_opaque = (up != TRANSPARENT) if mask == "source" else (rgba[:, :, 3] >= 128)
    np.testing.assert_array_equal(out != TRANSPARENT, expected_opaque)
    both = expected_opaque & (up != TRANSPARENT)
    # Every pixel stays in the light ramp of its source pixel...
    np.testing.assert_array_equal(ids[out[both]], ids[up[both]])
    # ...cycling pixels keep their index, and nothing else uses a cycling index
    # (unless its whole ramp is reserved, in which case it keeps its source index).
    is_reserved = np.isin(up, list(reserved)) & both
    np.testing.assert_array_equal(out[is_reserved], up[is_reserved])
    others = out[both & ~is_reserved]
    leaked = np.isin(others, list(reserved))
    assert np.array_equal(others[leaked], up[both & ~is_reserved][leaked])
    # Opaque pixels without a source fall back to any non-reserved colour.
    orphan = expected_opaque & (up == TRANSPARENT)
    assert not np.isin(out[orphan], list(reserved)).any()


@given(indexed_images(max_width=10, max_height=10), palettes(unique=True), st.integers(1, 3))
def test_ramp_is_exact_on_unmodified_upscale(source: np.ndarray, palette: np.ndarray, scale: int) -> None:
    up = Q.upscale_nearest(source, scale)
    out = Q.quantize_ramp(to_rgba(up, palette), palette, source)
    np.testing.assert_array_equal(out, up)


def test_ramp_restricts_hue() -> None:
    palette = np.zeros((256, 3), dtype=np.uint8)
    palette[32:48] = [(i * 16, 0, 0) for i in range(16)]  # red ramp
    palette[48:64] = [(0, 0, i * 16) for i in range(16)]  # blue ramp
    source = np.array([[40]], dtype=np.int16)  # red
    rgba = np.array([[[0, 0, 250, 255]]], dtype=np.uint8)  # pure blue
    assert Q.quantize_nearest(rgba, palette)[0, 0] in range(48, 64)
    assert Q.quantize_ramp(rgba, palette, source)[0, 0] in range(32, 48)


def test_ramp_size_mismatch() -> None:
    with pytest.raises(ValueError):
        Q.quantize_ramp(
            np.zeros((5, 4, 4), np.uint8), np.zeros((256, 3), np.uint8), np.zeros((2, 2), np.int16)
        )
