"""End-to-end pipeline on a synthetic MPQ: export -> "upscale" -> pack -> validate."""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from d1assets import cel, cl2, clx, dungeon, mpq, pal, pcx, quantize, trn
from d1assets.cli import main
from d1assets.hdpack import Manifest, sha256_hex, validate_pack
from d1assets.image import TRANSPARENT, load_png_indexed, to_pil

ASSET = "monsters\\synth\\synthw.cl2"


def synthetic_palette() -> np.ndarray:
    rng = np.random.default_rng(1)
    colors = rng.choice(1 << 24, size=256, replace=False)
    return np.stack([(colors >> 16) & 255, (colors >> 8) & 255, colors & 255], axis=1).astype(np.uint8)


def synthetic_frames(width: int = 24, height: int = 20, count: int = 2) -> list[np.ndarray]:
    frames = []
    for i in range(count):
        image = np.full((height, width), TRANSPARENT, dtype=np.int16)
        image[4:16, 6 + i : 18 + i] = 40 + (np.arange(12) % 8)[None, :]
        image[10, 8] = 5  # a colour-cycling index in caves
        frames.append(image)
    return frames


@pytest.fixture
def archive(tmp_path: Path) -> tuple[Path, list[list[np.ndarray]]]:
    groups = [synthetic_frames(), synthetic_frames()]
    level = [(np.full((32, 32), 7, np.int16), dungeon.TileType.SQUARE)]
    files = {
        ASSET: cl2.encode(groups),
        "levels\\synth\\synth.pal": pal.encode(synthetic_palette()),
        "monsters\\synth\\red.trn": trn.encode(trn.identity()),
        "levels\\synth\\synth.cel": dungeon.encode_level_cel(level),
        "levels\\synth\\synth.min": dungeon.encode_min([[0] * 8 + [dungeon.make_block(1, 0), 0]]),
        "levels\\synth\\synth.til": dungeon.encode_til([(0, 0, 0, 0)]),
        "ui_art\\synth.pcx": pcx.encode(np.arange(64, dtype=np.uint8).reshape(8, 8), synthetic_palette()),
        "data\\synth.clx": clx.encode([synthetic_frames(count=1)]),
        "data\\synth.cel": cel.encode([synthetic_frames(count=1)]),
    }
    path = tmp_path / "own.mpq"
    path.write_bytes(mpq.write_mpq(files, encrypted=True, fix_key=True))
    return path, groups


def test_mpq_list_and_extract(
    archive: tuple[Path, list], tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    path, _ = archive
    assert main(["mpq", "list", str(path)]) == 0
    assert ASSET in capsys.readouterr().out
    out = tmp_path / "x"
    assert main(["mpq", "extract", str(path), ASSET, "-o", str(out)]) == 0
    assert (out / "monsters/synth/synthw.cl2").is_file()
    assert main(["mpq", "extract", str(path), "--all", "-o", str(out)]) == 0
    assert (out / "ui_art/synth.pcx").is_file()


def test_full_pipeline(archive: tuple[Path, list[list[np.ndarray]]], tmp_path: Path) -> None:
    path, groups = archive
    work, upscaled, pack = tmp_path / "work", tmp_path / "upscaled", tmp_path / "pack"
    palette_arg = "levels\\synth\\synth.pal"
    assert (
        main(
            ["export", ASSET, "--mpq", str(path), "--palette", palette_arg, "--width", "24", "-o", str(work)]
        )
        == 0
    )
    exported = Manifest.load(work)
    assert len(exported.entries) == 4
    assert validate_pack(work).ok

    # Stand-in for Topaz / Real-ESRGAN: a smooth 3x resize, which invents off-palette colours.
    for entry in exported.entries:
        target = upscaled / entry.file
        target.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(work / entry.file) as im:
            im.resize((im.width * 3, im.height * 3), Image.Resampling.BICUBIC).save(target)

    args = ["pack", str(work), str(upscaled), "-o", str(pack), "--scale", "3", "--name", "synth-hd"]
    args += ["--quantize", "ramp", "--cycling", "caves"]  # palette: the one `export` used
    assert main(args) == 0
    manifest = Manifest.load(pack)
    assert manifest.name == "synth-hd"
    assert {e.scale for e in manifest.entries} == {3}
    source = mpq.MpqArchive(path).read(ASSET)
    assert {e.source_sha256 for e in manifest.entries} == {sha256_hex(source)}
    assert validate_pack(pack, {"monsters/synth/synthw.cl2": source}).ok

    # Every packed pixel uses a palette colour from its source pixel's light ramp.
    palette = synthetic_palette()
    ids = quantize.ramp_ids()
    entry = manifest.entries[0]
    hd = load_png_indexed(str(pack / entry.file), palette)
    up = quantize.upscale_nearest(groups[entry.direction][entry.frame], 3)
    both = (hd != TRANSPARENT) & (up != TRANSPARENT)
    np.testing.assert_array_equal(ids[hd[both]], ids[up[both]])
    cycling = (up == 5) & both
    assert (hd[cycling] == 5).all()

    from d1assets.hdpack.cli import main as hdpack_main

    assert hdpack_main(["validate", str(pack), "--mpq", str(path)]) == 0


def test_pack_rejects_wrong_scale(archive: tuple[Path, list], tmp_path: Path) -> None:
    path, _ = archive
    work, upscaled = tmp_path / "work", tmp_path / "up"
    main(
        [
            "export",
            ASSET,
            "--mpq",
            str(path),
            "--palette",
            "levels\\synth\\synth.pal",
            "--width",
            "24",
            "-o",
            str(work),
        ]
    )
    for entry in Manifest.load(work).entries:
        target = upscaled / entry.file.removeprefix("textures/")
        target.parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGBA", (10, 10)).save(target)
    assert main(["pack", str(work), str(upscaled), "-o", str(tmp_path / "p"), "--scale", "2"]) == 1
    assert (
        main(["pack", str(work), str(upscaled), "-o", str(tmp_path / "p"), "--scale", "2", "--resize"]) == 0
    )
    assert validate_pack(tmp_path / "p").ok


@pytest.mark.parametrize(
    "args",
    [
        ["data\\synth.clx", "--palette", "levels\\synth\\synth.pal"],
        ["data\\synth.cel", "--palette", "levels\\synth\\synth.pal", "--width", "24"],
        ["ui_art\\synth.pcx"],
        [
            "levels\\synth\\synth.cel",
            "--palette",
            "levels\\synth\\synth.pal",
            "--min",
            "levels\\synth\\synth.min",
        ],
        [
            ASSET,
            "--palette",
            "levels\\synth\\synth.pal",
            "--widths",
            "24,24",
            "--trn",
            "monsters\\synth\\red.trn",
        ],
    ],
)
def test_export_formats(archive: tuple[Path, list], tmp_path: Path, args: list[str]) -> None:
    path, _ = archive
    out = tmp_path / "work"
    assert main(["export", *args, "--mpq", str(path), "-o", str(out)]) == 0
    assert validate_pack(out).ok


def test_export_needs_width(archive: tuple[Path, list], tmp_path: Path) -> None:
    path, _ = archive
    assert (
        main(
            [
                "export",
                ASSET,
                "--mpq",
                str(path),
                "--palette",
                "levels\\synth\\synth.pal",
                "-o",
                str(tmp_path),
            ]
        )
        == 1
    )


def test_encode_and_quantize_commands(tmp_path: Path) -> None:
    palette = synthetic_palette()
    (tmp_path / "p.pal").write_bytes(pal.encode(palette))
    frames = synthetic_frames(count=4)
    names = []
    for i, frame in enumerate(frames):
        names.append(str(tmp_path / f"f{i}.png"))
        to_pil(frame, palette).save(names[-1])
    for fmt in ("cel", "cl2", "clx"):
        out = tmp_path / f"out.{fmt}"
        assert (
            main(
                [
                    "encode",
                    *names,
                    "--format",
                    fmt,
                    "--palette",
                    str(tmp_path / "p.pal"),
                    "--groups",
                    "2",
                    "-o",
                    str(out),
                ]
            )
            == 0
        )
        data = out.read_bytes()
        decoded = clx.decode(data) if fmt == "clx" else {"cel": cel, "cl2": cl2}[fmt].decode(data, 24)
        assert len(decoded) == 2
        np.testing.assert_array_equal(decoded[1][1], frames[3])

    big = tmp_path / "big.png"
    with Image.open(names[0]) as im:
        im.resize((48, 40), Image.Resampling.BILINEAR).save(big)
    for mode in (["--mode", "nearest"], ["--mode", "ramp", "--source", names[0]]):
        out = tmp_path / "q.png"
        assert main(["quantize", str(big), "--palette", str(tmp_path / "p.pal"), *mode, "-o", str(out)]) == 0
        assert load_png_indexed(str(out), palette).shape == (40, 48)
    assert (
        main(
            [
                "quantize",
                str(big),
                "--palette",
                str(tmp_path / "p.pal"),
                "--mode",
                "ramp",
                "-o",
                str(tmp_path / "q.png"),
            ]
        )
        == 1
    )


def test_tiles_command(archive: tuple[Path, list], tmp_path: Path) -> None:
    path, _ = archive
    common = ["--mpq", str(path), "--cel", "levels\\synth\\synth.cel", "--min", "levels\\synth\\synth.min"]
    common += ["--palette", "levels\\synth\\synth.pal"]
    assert main(["tiles", *common, "-o", str(tmp_path / "pieces")]) == 0
    with Image.open(tmp_path / "pieces" / "piece_0000.png") as im:
        assert im.size == (64, 160)
    assert main(["tiles", *common, "--til", "levels\\synth\\synth.til", "-o", str(tmp_path / "tiles")]) == 0
    with Image.open(tmp_path / "tiles" / "tile_0001.png") as im:
        assert im.size == (128, 192)
