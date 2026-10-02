from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from d1assets import mpq
from d1assets.hdpack import Entry, Manifest, normalize_asset_path, sha256_hex, texture_path, validate_pack
from d1assets.hdpack.cli import main as hdpack_main

SOURCE = b"synthetic asset bytes"
ASSET = "monsters/test/testw.cl2"


def make_pack(root: Path, scale: int = 2, size: tuple[int, int] = (3, 2)) -> Manifest:
    entries = []
    for frame in range(2):
        rel = texture_path(ASSET, 1, frame)
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGBA", (size[0] * scale, size[1] * scale), (frame, 0, 0, 255)).save(root / rel)
        entries.append(Entry(ASSET, 1, frame, scale, rel, sha256_hex(SOURCE), *size))
    manifest = Manifest(name="test-pack", entries=entries, description="synthetic")
    manifest.dump(root)
    return manifest


def edit_manifest(root: Path, change) -> None:  # type: ignore[no-untyped-def]
    path = root / "hdpack.json"
    data = json.loads(path.read_text())
    change(data)
    path.write_text(json.dumps(data))


def test_paths() -> None:
    assert normalize_asset_path("Monsters\\Zombie\\ZombieW.CL2") == "monsters/zombie/zombiew.cl2"
    assert (
        texture_path("Monsters\\Zombie\\ZombieW.CL2", 3, 12)
        == "textures/monsters/zombie/zombiew.cl2/d3_f0012.png"
    )


def test_valid_pack(tmp_path: Path) -> None:
    manifest = make_pack(tmp_path)
    report = validate_pack(tmp_path)
    assert report.ok, report.errors
    assert report.warnings == []
    loaded = Manifest.load(tmp_path)
    assert loaded.to_json() == manifest.to_json()
    assert hdpack_main(["validate", str(tmp_path)]) == 0


def test_source_hash_verification(tmp_path: Path) -> None:
    make_pack(tmp_path)
    assert validate_pack(tmp_path, {ASSET: SOURCE}).ok
    report = validate_pack(tmp_path, {ASSET: b"other version"})
    assert any("does not match" in e for e in report.errors)
    archive = tmp_path / "own.mpq"
    archive.write_bytes(mpq.write_mpq({"monsters\\test\\testw.cl2": SOURCE}))
    assert hdpack_main(["validate", str(tmp_path), "--mpq", str(archive)]) == 0
    archive.write_bytes(mpq.write_mpq({"monsters\\test\\testw.cl2": b"changed"}))
    assert hdpack_main(["validate", str(tmp_path), "--mpq", str(archive)]) == 1


@pytest.mark.parametrize(
    ("change", "message"),
    [
        (lambda d: d.update(format="other"), "'format'"),
        (lambda d: d.update(version=2), "'version'"),
        (lambda d: d.update(name=""), "'name'"),
        (lambda d: d["entries"][0].update(asset="Monsters\\X.cl2"), "'asset'"),
        (lambda d: d["entries"][0].update(asset="monsters/x.wav"), "'asset' must end"),
        (lambda d: d["entries"][0].update(frame=-1), "'frame'"),
        (lambda d: d["entries"][0].update(direction=True), "'direction'"),
        (lambda d: d["entries"][0].update(scale=0), "'scale'"),
        (lambda d: d["entries"][0].update(file="../escape.png"), "'file'"),
        (lambda d: d["entries"][0].update(file="/abs.png"), "'file'"),
        (lambda d: d["entries"][0].update(source_sha256="ABC"), "'source_sha256'"),
        (lambda d: d["entries"][0].update(source_width=0), "'source_width'"),
        (lambda d: d["entries"][0].pop("scale"), "missing keys scale"),
        (lambda d: d["entries"][1].update(frame=0), "duplicate"),
        (lambda d: d["entries"][1].update(source_sha256="0" * 64), "differs"),
        (lambda d: d["entries"][0].update(file="textures/missing.png"), "not found"),
        (lambda d: d["entries"][0].update(scale=3), "expected 9x6"),
    ],
)
def test_invalid_packs(tmp_path: Path, change, message: str) -> None:  # type: ignore[no-untyped-def]
    make_pack(tmp_path)
    edit_manifest(tmp_path, change)
    report = validate_pack(tmp_path)
    assert not report.ok
    assert any(message in e for e in report.errors), report.errors
    assert hdpack_main(["validate", "-q", str(tmp_path)]) == 1


def test_warnings(tmp_path: Path) -> None:
    make_pack(tmp_path)
    edit_manifest(tmp_path, lambda d: d.update(extra=1))
    Image.new("RGBA", (1, 1)).save(tmp_path / "textures" / "stray.png")
    report = validate_pack(tmp_path)
    assert report.ok
    assert any("unknown key 'extra'" in w for w in report.warnings)
    assert any("stray.png" in w for w in report.warnings)
    assert hdpack_main(["validate", str(tmp_path)]) == 0
    assert hdpack_main(["validate", "--strict", str(tmp_path)]) == 1


def test_not_a_png(tmp_path: Path) -> None:
    manifest = make_pack(tmp_path)
    (tmp_path / manifest.entries[0].file).write_bytes(b"not an image")
    assert not validate_pack(tmp_path).ok


def test_missing_or_broken_manifest(tmp_path: Path) -> None:
    assert not validate_pack(tmp_path).ok
    (tmp_path / "hdpack.json").write_text("{")
    assert not validate_pack(tmp_path).ok
    with pytest.raises(ValueError):
        Manifest.from_json({"format": "d1-hdpack"})


def test_hash_command(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    file = tmp_path / "asset.cl2"
    file.write_bytes(SOURCE)
    assert hdpack_main(["hash", str(file)]) == 0
    assert capsys.readouterr().out.startswith(sha256_hex(SOURCE))
    archive = tmp_path / "own.mpq"
    archive.write_bytes(mpq.write_mpq({"Monsters\\Test\\TestW.CL2": SOURCE}))
    assert hdpack_main(["hash", "--mpq", str(archive), "Monsters\\Test\\TestW.CL2"]) == 0
    assert capsys.readouterr().out.strip() == f"{sha256_hex(SOURCE)}  monsters/test/testw.cl2"


def test_png_modes_are_accepted(tmp_path: Path) -> None:
    manifest = make_pack(tmp_path)
    Image.fromarray(np.zeros((4, 6), np.uint8), "L").save(tmp_path / manifest.entries[0].file)
    assert validate_pack(tmp_path).ok
