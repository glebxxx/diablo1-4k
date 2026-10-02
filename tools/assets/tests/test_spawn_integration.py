"""Optional checks against the shareware ``spawn.mpq``; skipped unless it is provided.

No game data is stored in this repository. CI downloads ``spawn.mpq`` the same
way upstream DevilutionX CI does, and points ``D1ASSETS_SPAWN_MPQ`` at it::

    wget -qnc https://github.com/diasurgical/devilutionx-assets/releases/download/v2/spawn.mpq -P build
    D1ASSETS_SPAWN_MPQ=build/spawn.mpq pytest

These tests only compare our codecs with the real files; they write nothing.
"""

from __future__ import annotations

import os

import numpy as np
import pytest

from d1assets import cel, cl2, clx, dungeon, pal, pcx
from d1assets.container import read_frame_list
from d1assets.mpq import MpqArchive

SPAWN = os.environ.get("D1ASSETS_SPAWN_MPQ")
pytestmark = pytest.mark.skipif(not SPAWN or not os.path.isfile(SPAWN), reason="D1ASSETS_SPAWN_MPQ not set")


@pytest.fixture(scope="module")
def spawn() -> MpqArchive:
    assert SPAWN
    return MpqArchive(SPAWN)


@pytest.mark.parametrize(("base", "blocks"), [("levels\\l1data\\l1", 10), ("levels\\towndata\\town", 16)])
def test_level_micro_tiles_reencode_byte_exact(spawn: MpqArchive, base: str, blocks: int) -> None:
    frames = read_frame_list(spawn.read(base + ".cel"))
    pieces = dungeon.decode_min(spawn.read(base + ".min"), blocks)
    types = dungeon.frame_types(pieces)
    assert set(types.values()) == set(dungeon.TileType)  # all 6 encodings are exercised
    for frame, tile_type in types.items():
        original = frames[frame - 1]
        assert dungeon.encode_micro(dungeon.decode_micro(original, tile_type), tile_type) == original
    tiles = dungeon.decode_til(spawn.read(base + ".til"))
    assert all(max(t) < len(pieces) for t in tiles)
    pal.decode(spawn.read(base + ".pal"))


def test_cl2_monster(spawn: MpqArchive) -> None:
    data = spawn.read("monsters\\zombie\\zombiew.cl2")
    groups = cl2.decode(data, 128)
    assert len(groups) == 8  # directions
    assert all(f.shape == (96, 128) for g in groups for f in g)
    again = cl2.decode(cl2.encode(groups), 128)
    for a, b in zip(groups, again):
        for fa, fb in zip(a, b):
            np.testing.assert_array_equal(fa, fb)
    sheet = clx.decode(clx.from_cl2(data, 128))
    np.testing.assert_array_equal(sheet[3][5], groups[3][5])


def test_cel_byte_exact(spawn: MpqArchive) -> None:
    data = spawn.read("data\\char.cel")
    groups = cel.decode(data, 320)
    assert groups[0][0].shape == (352, 320)
    assert cel.encode(groups) == data


def test_pcx(spawn: MpqArchive) -> None:
    image = pcx.decode(spawn.read("ui_art\\title.pcx"))
    assert image.pixels.shape == (480, 640)
    assert image.palette is not None
    again = pcx.decode(pcx.encode(image.pixels, image.palette))
    np.testing.assert_array_equal(again.pixels, image.pixels)
