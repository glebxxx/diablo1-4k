"""``d1assets``: command-line tools for Diablo graphics and HD texture packs.

Typical pipeline (see the README):

1. ``d1assets export`` frames from YOUR OWN MPQ into a work folder (PNG + a scale-1 manifest);
2. upscale the PNGs locally (Topaz, Real-ESRGAN, ...), keeping file names;
3. ``d1assets pack`` the upscaled PNGs into an HD pack (optionally quantized to the palette);
4. ``d1-hdpack validate`` it and copy it to the mods folder.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image

from . import cel, cl2, clx, dungeon, pal, pcx, quantize, trn
from .hdpack.manifest import (
    MANIFEST_NAME,
    Entry,
    Manifest,
    normalize_asset_path,
    sha256_hex,
    texture_path,
)
from .image import TRANSPARENT, IndexedImage, Palette, load_png_indexed, to_pil, to_rgba
from .mpq import MpqArchive

SOURCE_INDEX_NAME = "source.npz"
PALETTE_KEY_PREFIX = "palette:"  # source.npz also stores the palette each asset was exported with
FORMATS = ("cel", "cl2", "clx", "pcx", "levelcel")


class CliError(Exception):
    pass


def _read(path: str, archive: MpqArchive | None) -> bytes:
    """Reads ``path`` from the archive if it is there, else from disk."""
    if archive is not None and path in archive:
        return archive.read(path)
    file = Path(path)
    if not file.is_file():
        where = " (not in the MPQ either)" if archive is not None else ""
        raise CliError(f"{path}: file not found{where}")
    return file.read_bytes()


def _palette(path: str | None, archive: MpqArchive | None) -> Palette | None:
    return pal.decode(_read(path, archive)) if path else None


def _require_palette(path: str | None, archive: MpqArchive | None) -> Palette:
    palette = _palette(path, archive)
    if palette is None:
        raise CliError("--palette is required")
    return palette


def _widths(args: argparse.Namespace) -> int | list[int]:
    if args.widths:
        return [int(w) for w in args.widths.split(",")]
    if args.width:
        return int(args.width)
    raise CliError("CEL and CL2 frames do not store their width: pass --width or --widths")


def _grouped(value: str) -> bool | None:
    return {"auto": None, "yes": True, "no": False}[value]


def _detect_format(name: str, args: argparse.Namespace) -> str:
    if args.format != "auto":
        return str(args.format)
    lower = name.lower()
    if lower.endswith(".cel") and args.min:
        return "levelcel"
    for fmt in ("cel", "cl2", "clx", "pcx"):
        if lower.endswith("." + fmt):
            return fmt
    raise CliError(f"{name}: cannot guess the format, pass --format")


def decode_asset(
    data: bytes, fmt: str, args: argparse.Namespace, archive: MpqArchive | None
) -> tuple[list[list[IndexedImage]], Palette | None]:
    """Decodes an asset into groups of frames; returns an embedded palette for PCX."""
    grouped = _grouped(args.grouped)
    if fmt == "cel":
        return cel.decode(data, _widths(args), grouped), None
    if fmt == "cl2":
        return cl2.decode(data, _widths(args), grouped), None
    if fmt == "clx":
        return clx.decode(data, grouped), None
    if fmt == "pcx":
        image = pcx.decode(data)
        return [[image.pixels.astype(np.int16)]], image.palette
    if fmt == "levelcel":
        if not args.min:
            raise CliError("level CELs need --min (and --blocks)")
        pieces = dungeon.decode_min(_read(args.min, archive), args.blocks)
        micros = dungeon.decode_level_cel(data, pieces)
        count = max(micros, default=0)
        frames = [micros.get(i + 1, np.full((32, 32), TRANSPARENT, np.int16)) for i in range(count)]
        return [frames], None
    raise CliError(f"unknown format {fmt}")


# --- mpq ------------------------------------------------------------------------------------------------


def _candidates(args: argparse.Namespace) -> list[str]:
    names: list[str] = list(getattr(args, "names", None) or [])
    if args.listfile:
        text = Path(args.listfile).read_text(encoding="latin-1")
        names += [n.strip() for n in text.replace(";", "\n").splitlines() if n.strip()]
    return names


def cmd_mpq_list(args: argparse.Namespace) -> int:
    archive = MpqArchive(args.archive)
    names = archive.known_files(_candidates(args))
    for name in names:
        block = archive.find(name)
        assert block is not None
        print(f"{block.file_size:>10}  {name}")
    h = archive.header
    print(
        f"# {len(names)} named of {sum(1 for b in archive.block_table if b.file_size)} files;"
        f" format v{h.format_version}, sector size {h.sector_size}",
        file=sys.stderr,
    )
    return 0


def cmd_mpq_extract(args: argparse.Namespace) -> int:
    archive = MpqArchive(args.archive)
    names = archive.known_files(_candidates(args)) if args.all else list(args.names)
    if not names:
        raise CliError("nothing to extract: give file names, or --all with a listfile")
    out = Path(args.output)
    for name in names:
        target = out / normalize_asset_path(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(archive.read(name))
        print(target)
    return 0


# --- export / pack --------------------------------------------------------------------------------------


def _load_or_new_manifest(path: Path, name: str) -> Manifest:
    return Manifest.load(path) if (path / MANIFEST_NAME).is_file() else Manifest(name=name)


def _load_source_index(path: Path) -> dict[str, np.ndarray]:
    """Frames (palette indices) keyed by PNG path, and palettes keyed by ``palette:<asset>``."""
    file = path / SOURCE_INDEX_NAME
    if not file.is_file():
        return {}
    with np.load(file) as npz:
        return {k: npz[k] for k in npz.files}


def cmd_export(args: argparse.Namespace) -> int:
    archive = MpqArchive(args.mpq) if args.mpq else None
    data = _read(args.input, archive)
    fmt = _detect_format(args.input, args)
    groups, embedded = decode_asset(data, fmt, args, archive)
    palette = _palette(args.palette, archive) if args.palette else embedded
    if palette is None:
        raise CliError("--palette is required")
    if args.trn:
        table = trn.decode(_read(args.trn, archive))
        groups = [[trn.apply(f, table) for f in frames] for frames in groups]

    asset = normalize_asset_path(args.asset or args.input)
    out = Path(args.output)
    manifest = _load_or_new_manifest(out, args.name or out.name)
    manifest.entries = [e for e in manifest.entries if e.asset != asset]
    index = _load_source_index(out)
    digest = sha256_hex(data)
    count = 0
    for direction, frames in enumerate(groups):
        for number, frame in enumerate(frames):
            if frame.size == 0 or (frame == TRANSPARENT).all():
                continue  # nothing to upscale; the original frame stays in use
            rel = texture_path(asset, direction, number)
            target = out / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            to_pil(frame, palette).save(target)
            index[rel] = frame
            index[PALETTE_KEY_PREFIX + asset] = palette
            height, width = frame.shape
            manifest.entries.append(Entry(asset, direction, number, 1, rel, digest, width, height))
            count += 1
    manifest.dump(out)
    # Typed as Any: some numpy stubs confuse ``**kwds`` with the ``allow_pickle`` keyword.
    arrays: dict[str, Any] = dict(index)
    np.savez_compressed(out / SOURCE_INDEX_NAME, **arrays)
    print(f"exported {count} frame(s) of {asset} to {out}")
    return 0


def _quantize_rgba(
    rgba: np.ndarray, args: argparse.Namespace, palette: Palette | None, source: IndexedImage | None
) -> np.ndarray:
    if args.quantize == "none":
        return rgba
    if palette is None:
        raise CliError("--palette is required to quantize")
    if args.quantize == "nearest":
        indexed = quantize.quantize_nearest(
            rgba, palette, exclude=quantize.reserved_indices(args.cycling), metric=args.metric
        )
    else:
        if source is None:
            raise CliError("ramp quantization needs the source frames (source.npz from `d1assets export`)")
        indexed = quantize.quantize_ramp(
            rgba, palette, source, cycling=args.cycling, mask=args.mask, metric=args.metric
        )
    return to_rgba(indexed, palette)


def cmd_pack(args: argparse.Namespace) -> int:
    archive = MpqArchive(args.mpq) if args.mpq else None
    export_dir, upscaled_dir, out = Path(args.export), Path(args.upscaled), Path(args.output)
    source_manifest = Manifest.load(export_dir)
    source_index = _load_source_index(export_dir)
    forced_palette = _palette(args.palette, archive) if args.palette else None
    manifest = _load_or_new_manifest(out, args.name or out.name)
    if args.description:
        manifest.description = args.description
    done: dict[tuple[str, int, int, int], Entry] = {e.key: e for e in manifest.entries}
    missing = 0
    for entry in source_manifest.entries:
        if entry.scale != 1:
            continue
        # Accept the work folder layout, or the same layout without the leading "textures/".
        upscaled = upscaled_dir / entry.file
        if not upscaled.is_file() and entry.file.startswith("textures/"):
            upscaled = upscaled_dir / entry.file.removeprefix("textures/")
        if not upscaled.is_file():
            missing += 1
            continue
        expected = (entry.source_width * args.scale, entry.source_height * args.scale)
        with Image.open(upscaled) as im:
            image = im.convert("RGBA")
        if image.size != expected:
            if not args.resize:
                raise CliError(f"{upscaled}: size {image.size}, expected {expected}; pass --resize to fit")
            image = image.resize(expected, Image.Resampling.LANCZOS)
        palette = forced_palette
        stored = source_index.get(PALETTE_KEY_PREFIX + entry.asset)
        if palette is None and stored is not None:
            palette = stored.astype(np.uint8)
        rgba = _quantize_rgba(np.asarray(image), args, palette, source_index.get(entry.file))
        rel = entry.file
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(rgba, "RGBA").save(target)
        new = Entry(
            entry.asset, entry.direction, entry.frame, args.scale, rel, entry.source_sha256,
            entry.source_width, entry.source_height,
        )  # fmt: skip
        done[new.key] = new
    manifest.entries = list(done.values())
    manifest.dump(out)
    note = f"; {missing} frame(s) not upscaled yet" if missing else ""
    print(f"packed {len(manifest.entries)} frame(s) into {out}{note}")
    return 0


# --- single-file tools ----------------------------------------------------------------------------------


def cmd_quantize(args: argparse.Namespace) -> int:
    archive = MpqArchive(args.mpq) if args.mpq else None
    palette = _require_palette(args.palette, archive)
    with Image.open(args.image) as im:
        rgba = np.asarray(im.convert("RGBA"))
    if args.mode == "ramp":
        if not args.source:
            raise CliError("--mode ramp needs --source (the original low-resolution frame)")
        source = load_png_indexed(args.source, palette)
        indexed = quantize.quantize_ramp(
            rgba, palette, source, cycling=args.cycling, mask=args.mask, metric=args.metric
        )
    else:
        indexed = quantize.quantize_nearest(
            rgba, palette, exclude=quantize.reserved_indices(args.cycling), metric=args.metric
        )
    to_pil(indexed, palette).save(args.output)
    return 0


def cmd_encode(args: argparse.Namespace) -> int:
    archive = MpqArchive(args.mpq) if args.mpq else None
    palette = _require_palette(args.palette, archive)
    frames = [load_png_indexed(p, palette) for p in args.images]
    if len(frames) % args.groups:
        raise CliError(f"{len(frames)} frames cannot be split into {args.groups} equal groups")
    per = len(frames) // args.groups
    groups = [frames[i * per : (i + 1) * per] for i in range(args.groups)]
    if args.format == "cel":
        data = cel.encode(groups, frame_header=args.cel_frame_header)
    elif args.format == "cl2":
        data = cl2.encode(groups)
    else:
        data = clx.encode(groups)
    Path(args.output).write_bytes(data)
    widths = sorted({f.shape[1] for f in frames})
    print(f"wrote {args.output}: {len(frames)} frame(s), width(s) {widths}")
    return 0


def cmd_tiles(args: argparse.Namespace) -> int:
    archive = MpqArchive(args.mpq) if args.mpq else None
    palette = _require_palette(args.palette, archive)
    pieces = dungeon.decode_min(_read(args.min, archive), args.blocks)
    micros = dungeon.decode_level_cel(_read(args.cel, archive), pieces)
    piece_images = [dungeon.render_piece(p, micros) for p in pieces]
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    if args.til:
        tiles = dungeon.decode_til(_read(args.til, archive))
        for i, tile in enumerate(tiles):
            to_pil(dungeon.render_megatile(tile, piece_images), palette).save(out / f"tile_{i + 1:04d}.png")
        print(f"rendered {len(tiles)} mega-tile(s) to {out}")
    else:
        for i, image in enumerate(piece_images):
            to_pil(image, palette).save(out / f"piece_{i:04d}.png")
        print(f"rendered {len(piece_images)} piece(s) to {out}")
    return 0


# --- parser ---------------------------------------------------------------------------------------------


def _add_decode_options(p: argparse.ArgumentParser) -> None:
    p.add_argument("--format", choices=("auto", *FORMATS), default="auto")
    p.add_argument("--width", type=int, help="frame width (CEL/CL2)")
    p.add_argument("--widths", help="comma-separated per-frame widths (CEL/CL2)")
    p.add_argument("--grouped", choices=("auto", "yes", "no"), default="auto", help="frame groups/directions")
    p.add_argument("--min", help="MIN file, for level CELs")
    p.add_argument("--blocks", type=int, default=10, help="MIN blocks per piece (10; 16 for town and hell)")


def _add_quantize_options(p: argparse.ArgumentParser) -> None:
    p.add_argument(
        "--cycling",
        choices=tuple(quantize.CYCLING_RANGES),
        default="none",
        help="reserve colour-cycling indices of this dungeon type",
    )
    p.add_argument("--mask", choices=("alpha", "source"), default="alpha", help="ramp mode transparency")
    p.add_argument("--metric", choices=quantize.METRICS, default="rgb")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="d1assets", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="command", required=True)

    mpq_cmd = sub.add_parser("mpq", help="list or extract MPQ archives")
    mpq_sub = mpq_cmd.add_subparsers(dest="mpq_command", required=True)
    p = mpq_sub.add_parser("list", help="list files whose names are known")
    p.add_argument("archive")
    p.add_argument("--listfile", help="extra candidate names, one per line")
    p.set_defaults(func=cmd_mpq_list)
    p = mpq_sub.add_parser("extract", help="extract files")
    p.add_argument("archive")
    p.add_argument("names", nargs="*", help="MPQ paths, e.g. 'monsters\\zombie\\zombiew.cl2'")
    p.add_argument("--all", action="store_true", help="extract every named file")
    p.add_argument("--listfile", help="extra candidate names, one per line")
    p.add_argument("-o", "--output", required=True)
    p.set_defaults(func=cmd_mpq_extract)

    p = sub.add_parser("export", help="decode an asset into PNG frames + a scale-1 manifest")
    p.add_argument("input", help="asset path (in --mpq, or on disk)")
    p.add_argument("-o", "--output", required=True, help="work folder (created / merged)")
    p.add_argument("--mpq", help="read the asset, palette, TRN and MIN from this MPQ")
    p.add_argument("--palette", help="PAL file (optional for PCX)")
    p.add_argument("--trn", help="apply a TRN colour translation")
    p.add_argument("--asset", help="asset path to record in the manifest (default: input)")
    p.add_argument("--name", help="manifest name")
    _add_decode_options(p)
    p.set_defaults(func=cmd_export)

    p = sub.add_parser("pack", help="build an HD pack from upscaled PNGs")
    p.add_argument("export", help="work folder written by `export`")
    p.add_argument("upscaled", help="folder with upscaled PNGs (same relative paths)")
    p.add_argument("-o", "--output", required=True, help="pack folder (created / merged)")
    p.add_argument("--scale", type=int, required=True)
    p.add_argument("--name", help="pack name")
    p.add_argument("--description")
    p.add_argument("--resize", action="store_true", help="resize images that are not exactly --scale times")
    p.add_argument("--quantize", choices=("none", "nearest", "ramp"), default="none")
    p.add_argument("--palette", help="PAL file for --quantize (default: the palette used by `export`)")
    p.add_argument("--mpq", help="read the palette from this MPQ")
    _add_quantize_options(p)
    p.set_defaults(func=cmd_pack)

    p = sub.add_parser("quantize", help="map one RGBA image to the palette")
    p.add_argument("image")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--palette", required=True)
    p.add_argument("--mpq", help="read the palette from this MPQ")
    p.add_argument("--mode", choices=("nearest", "ramp"), default="nearest")
    p.add_argument("--source", help="original low-resolution frame (PNG), for --mode ramp")
    _add_quantize_options(p)
    p.set_defaults(func=cmd_quantize)

    p = sub.add_parser("encode", help="encode palette PNG frames as CEL, CL2 or CLX")
    p.add_argument("images", nargs="+", help="frames in order (direction-major)")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--format", choices=("cel", "cl2", "clx"), required=True)
    p.add_argument("--palette", required=True)
    p.add_argument("--mpq", help="read the palette from this MPQ")
    p.add_argument("--groups", type=int, default=1, help="number of groups/directions")
    p.add_argument("--cel-frame-header", action="store_true", help="write 10-byte CEL frame headers")
    p.set_defaults(func=cmd_encode)

    p = sub.add_parser("tiles", help="render dungeon pieces or mega-tiles to PNG")
    p.add_argument("--cel", required=True, help="level CEL, e.g. levels\\l1data\\l1.cel")
    p.add_argument("--min", required=True)
    p.add_argument("--til", help="render mega-tiles instead of pieces")
    p.add_argument("--blocks", type=int, default=10, help="MIN blocks per piece (10; 16 for town and hell)")
    p.add_argument("--palette", required=True)
    p.add_argument("--mpq", help="read the inputs from this MPQ")
    p.add_argument("-o", "--output", required=True)
    p.set_defaults(func=cmd_tiles)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.func(args))
    except (CliError, ValueError, KeyError, OSError) as exc:
        print(f"d1assets: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
