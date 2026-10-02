"""HD texture pack manifest (``hdpack.json``): model, I/O and validation.

The layout is documented in ``tools/assets/HDPACK.md``.
"""

from __future__ import annotations

import hashlib
import json
import os
import posixpath
import re
from collections.abc import Iterable, Mapping
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from PIL import Image

FORMAT = "d1-hdpack"
VERSION = 1
MANIFEST_NAME = "hdpack.json"
TEXTURES_DIR = "textures"
ASSET_EXTENSIONS = (".cel", ".cl2", ".clx", ".pcx")
MAX_SCALE = 16

_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_ENTRY_KEYS = {
    "asset", "direction", "frame", "scale", "file", "source_sha256", "source_width", "source_height",
}  # fmt: skip
_TOP_KEYS = {"format", "version", "name", "description", "entries"}


def normalize_asset_path(path: str) -> str:
    """``Monsters\\Zombie\\ZombieW.CL2`` -> ``monsters/zombie/zombiew.cl2``."""
    return path.replace("\\", "/").strip("/").lower()


def to_mpq_path(asset: str) -> str:
    """``monsters/zombie/zombiew.cl2`` -> ``monsters\\zombie\\zombiew.cl2``."""
    return asset.replace("/", "\\")


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def texture_path(asset: str, direction: int, frame: int) -> str:
    """The conventional pack-relative PNG path for a frame."""
    return f"{TEXTURES_DIR}/{normalize_asset_path(asset)}/d{direction}_f{frame:04d}.png"


@dataclass
class Entry:
    """One HD replacement for one frame of one asset."""

    asset: str  # normalized MPQ path, e.g. "monsters/zombie/zombiew.cl2"
    direction: int  # group index (CL2/CLX direction); 0 for ungrouped files
    frame: int  # 0-based frame index within the group
    scale: int  # integer upscale factor of the PNG relative to the source frame
    file: str  # pack-relative POSIX path of the PNG
    source_sha256: str  # SHA-256 of the whole asset file as stored in the MPQ (decompressed)
    source_width: int  # size of the original frame in pixels
    source_height: int

    @property
    def key(self) -> tuple[str, int, int, int]:
        return (self.asset, self.direction, self.frame, self.scale)


@dataclass
class Manifest:
    name: str
    entries: list[Entry] = field(default_factory=list)
    description: str = ""
    format: str = FORMAT
    version: int = VERSION

    def to_json(self) -> dict[str, Any]:
        return {
            "format": self.format,
            "version": self.version,
            "name": self.name,
            "description": self.description,
            "entries": [asdict(e) for e in sorted(self.entries, key=lambda e: e.key)],
        }

    def dump(self, pack_dir: str | os.PathLike[str]) -> Path:
        path = Path(pack_dir) / MANIFEST_NAME
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_json(), indent=2) + "\n", encoding="utf-8")
        return path

    @classmethod
    def from_json(cls, data: Mapping[str, Any]) -> Manifest:
        """Builds a manifest from parsed JSON; call :func:`validate_pack` for full checks."""
        report = Report()
        _check_top(data, report)
        entries = [_parse_entry(e, i, report) for i, e in enumerate(data.get("entries") or [])]
        if report.errors:
            raise ValueError("invalid manifest:\n" + "\n".join(report.errors))
        return cls(
            name=data["name"],
            entries=[e for e in entries if e is not None],
            description=data.get("description", ""),
            format=data["format"],
            version=data["version"],
        )

    @classmethod
    def load(cls, pack_dir: str | os.PathLike[str]) -> Manifest:
        path = Path(pack_dir) / MANIFEST_NAME
        return cls.from_json(json.loads(path.read_text(encoding="utf-8")))


@dataclass
class Report:
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.errors


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _check_top(data: Any, report: Report) -> None:
    if not isinstance(data, Mapping):
        report.errors.append("manifest: top level must be a JSON object")
        return
    if data.get("format") != FORMAT:
        report.errors.append(f"manifest: 'format' must be {FORMAT!r}")
    if data.get("version") != VERSION:
        report.errors.append(f"manifest: unsupported 'version' {data.get('version')!r} (expected {VERSION})")
    if not isinstance(data.get("name"), str) or not data.get("name"):
        report.errors.append("manifest: 'name' must be a non-empty string")
    if "description" in data and not isinstance(data["description"], str):
        report.errors.append("manifest: 'description' must be a string")
    if not isinstance(data.get("entries"), list):
        report.errors.append("manifest: 'entries' must be a list")
    for key in sorted(set(data) - _TOP_KEYS):
        report.warnings.append(f"manifest: unknown key {key!r}")


def _check_relative_path(path: str) -> bool:
    if not path or path.startswith("/") or "\\" in path or re.match(r"^[A-Za-z]:", path):
        return False
    return ".." not in path.split("/") and posixpath.normpath(path) == path


def _parse_entry(raw: Any, index: int, report: Report) -> Entry | None:
    where = f"entries[{index}]"
    if not isinstance(raw, Mapping):
        report.errors.append(f"{where}: must be an object")
        return None
    missing = sorted(_ENTRY_KEYS - set(raw))
    if missing:
        report.errors.append(f"{where}: missing keys {', '.join(missing)}")
        return None
    for key in sorted(set(raw) - _ENTRY_KEYS):
        report.warnings.append(f"{where}: unknown key {key!r}")
    errors_before = len(report.errors)
    asset = raw["asset"]
    if not isinstance(asset, str) or asset != normalize_asset_path(asset) or not _check_relative_path(asset):
        report.errors.append(f"{where}: 'asset' must be a lowercase relative path with '/' separators")
    elif not asset.endswith(ASSET_EXTENSIONS):
        report.errors.append(f"{where}: 'asset' must end with one of {', '.join(ASSET_EXTENSIONS)}")
    for key in ("direction", "frame"):
        if not _is_int(raw[key]) or raw[key] < 0:
            report.errors.append(f"{where}: '{key}' must be a non-negative integer")
    if not _is_int(raw["scale"]) or not 1 <= raw["scale"] <= MAX_SCALE:
        report.errors.append(f"{where}: 'scale' must be an integer in 1..{MAX_SCALE}")
    for key in ("source_width", "source_height"):
        if not _is_int(raw[key]) or raw[key] <= 0:
            report.errors.append(f"{where}: '{key}' must be a positive integer")
    file = raw["file"]
    if not isinstance(file, str) or not _check_relative_path(file) or not file.endswith(".png"):
        report.errors.append(f"{where}: 'file' must be a relative POSIX path to a .png inside the pack")
    sha = raw["source_sha256"]
    if not isinstance(sha, str) or not _SHA256_RE.match(sha):
        report.errors.append(f"{where}: 'source_sha256' must be 64 lowercase hex digits")
    if len(report.errors) != errors_before:
        return None
    return Entry(**{k: raw[k] for k in _ENTRY_KEYS})


SourceLookup = Mapping[str, bytes]


def validate_pack(
    pack_dir: str | os.PathLike[str],
    sources: SourceLookup | None = None,
    check_images: bool = True,
) -> Report:
    """Validates a pack directory.

    ``sources`` optionally maps normalized asset paths to the original file
    bytes (e.g. read from the user's own MPQ) to verify ``source_sha256``.
    """
    report = Report()
    root = Path(pack_dir)
    manifest_path = root / MANIFEST_NAME
    if not manifest_path.is_file():
        report.errors.append(f"{manifest_path}: not found")
        return report
    try:
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        report.errors.append(f"{MANIFEST_NAME}: invalid JSON: {exc}")
        return report
    _check_top(data, report)
    raw_entries = data.get("entries") if isinstance(data, Mapping) else None
    if not isinstance(raw_entries, list):
        return report
    if not raw_entries:
        report.warnings.append("manifest: no entries")

    seen: dict[tuple[str, int, int, int], int] = {}
    hashes: dict[str, str] = {}
    referenced: set[str] = set()
    for index, raw in enumerate(raw_entries):
        entry = _parse_entry(raw, index, report)
        if entry is None:
            continue
        where = f"entries[{index}] ({entry.asset} d{entry.direction} f{entry.frame})"
        if entry.key in seen:
            report.errors.append(f"{where}: duplicate of entries[{seen[entry.key]}]")
        seen.setdefault(entry.key, index)
        if hashes.setdefault(entry.asset, entry.source_sha256) != entry.source_sha256:
            report.errors.append(f"{where}: 'source_sha256' differs from other entries of the same asset")
        if sources is not None:
            source = sources.get(entry.asset)
            if source is None:
                report.warnings.append(f"{where}: asset not found in the given archives")
            elif sha256_hex(source) != entry.source_sha256:
                report.errors.append(f"{where}: 'source_sha256' does not match the asset in the archives")
        referenced.add(entry.file)
        png = root / entry.file
        if not png.is_file():
            report.errors.append(f"{where}: file {entry.file} not found")
            continue
        if check_images:
            _check_png(png, entry, where, report)

    textures = root / TEXTURES_DIR
    if textures.is_dir():
        for path in sorted(textures.rglob("*")):
            if path.is_file() and path.relative_to(root).as_posix() not in referenced:
                report.warnings.append(f"{path.relative_to(root).as_posix()}: not referenced by the manifest")
    return report


def _check_png(path: Path, entry: Entry, where: str, report: Report) -> None:
    try:
        with Image.open(path) as im:
            if im.format != "PNG":
                report.errors.append(f"{where}: {entry.file} is not a PNG ({im.format})")
                return
            if im.mode not in ("RGBA", "RGB", "P", "LA", "L"):
                report.errors.append(f"{where}: {entry.file} has unsupported mode {im.mode}")
            expected = (entry.source_width * entry.scale, entry.source_height * entry.scale)
            if im.size != expected:
                got = f"{im.size[0]}x{im.size[1]}"
                want = (
                    f"{expected[0]}x{expected[1]} ({entry.source_width}x{entry.source_height} x{entry.scale})"
                )
                report.errors.append(f"{where}: {entry.file} is {got}, expected {want}")
    except OSError as exc:
        report.errors.append(f"{where}: cannot read {entry.file}: {exc}")


def sources_from_archives(archives: Iterable[Any], assets: Iterable[str]) -> dict[str, bytes]:
    """Reads ``assets`` (normalized paths) from MPQ archives; later archives override earlier ones."""
    found: dict[str, bytes] = {}
    for archive in archives:
        for asset in assets:
            name = to_mpq_path(asset)
            if name in archive:
                found[asset] = archive.read(name)
    return found
