"""Shared Hypothesis strategies. Every fixture here is synthetic: no game data."""

from __future__ import annotations

import os

import numpy as np
from hypothesis import HealthCheck, settings
from hypothesis import strategies as st

settings.register_profile("ci", max_examples=200, deadline=None, suppress_health_check=[HealthCheck.too_slow])
settings.register_profile("dev", max_examples=60, deadline=None, suppress_health_check=[HealthCheck.too_slow])
settings.load_profile(os.environ.get("HYPOTHESIS_PROFILE", "dev"))


@st.composite
def indexed_images(
    draw: st.DrawFn,
    max_width: int = 140,
    max_height: int = 80,
    min_width: int = 1,
    min_height: int = 1,
    transparent: bool = True,
) -> np.ndarray:
    """Sprite-like indexed images: long runs (to hit run-length limits) plus noise."""
    width = draw(st.integers(min_width, max_width))
    height = draw(st.integers(min_height, max_height))
    low = -1 if transparent else 0
    runs = draw(st.lists(st.tuples(st.integers(low, 255), st.integers(1, 300)), min_size=1, max_size=30))
    flat = np.concatenate([np.full(length, value, np.int16) for value, length in runs])
    flat = np.resize(flat, width * height).astype(np.int16)
    rng = np.random.default_rng(draw(st.integers(0, 2**32 - 1)))
    noise = rng.random(flat.size) < draw(st.sampled_from([0.0, 0.05, 0.5]))
    flat[noise] = rng.integers(low, 256, int(noise.sum()))
    return flat.reshape(height, width)


@st.composite
def palettes(draw: st.DrawFn, unique: bool = False) -> np.ndarray:
    seed = draw(st.integers(0, 2**32 - 1))
    rng = np.random.default_rng(seed)
    if unique:
        colors = rng.choice(1 << 24, size=256, replace=False)
        return np.stack([(colors >> 16) & 255, (colors >> 8) & 255, colors & 255], axis=1).astype(np.uint8)
    return rng.integers(0, 256, (256, 3), dtype=np.uint8)
