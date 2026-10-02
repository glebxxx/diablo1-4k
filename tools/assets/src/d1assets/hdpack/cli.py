"""``d1-hdpack``: validate HD texture packs and compute source hashes."""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Sequence
from pathlib import Path

from ..mpq import MpqArchive
from .manifest import (
    MANIFEST_NAME,
    normalize_asset_path,
    sha256_hex,
    sources_from_archives,
    validate_pack,
)


def _cmd_validate(args: argparse.Namespace) -> int:
    sources = None
    if args.mpq:
        try:
            data = json.loads((Path(args.pack) / MANIFEST_NAME).read_text(encoding="utf-8"))
            assets = {e["asset"] for e in data.get("entries", []) if isinstance(e, dict) and "asset" in e}
        except (OSError, ValueError):
            assets = set()
        sources = sources_from_archives([MpqArchive(p) for p in args.mpq], assets)
    report = validate_pack(args.pack, sources, check_images=not args.no_images)
    for message in report.errors:
        print(f"error: {message}")
    for message in report.warnings:
        if not args.quiet:
            print(f"warning: {message}")
    status = "OK" if report.ok else "FAILED"
    print(f"{status}: {len(report.errors)} error(s), {len(report.warnings)} warning(s)")
    if args.strict and report.warnings:
        return 1
    return 0 if report.ok else 1


def _cmd_hash(args: argparse.Namespace) -> int:
    archive = MpqArchive(args.mpq) if args.mpq else None
    for name in args.files:
        data = archive.read(name) if archive else Path(name).read_bytes()
        label = normalize_asset_path(name) if archive else name
        print(f"{sha256_hex(data)}  {label}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="d1-hdpack", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    validate = sub.add_parser("validate", help="check a pack directory against the hdpack format")
    validate.add_argument("pack", help="pack directory (contains hdpack.json)")
    validate.add_argument("--mpq", action="append", help="your own MPQ, to verify source hashes (repeatable)")
    validate.add_argument("--no-images", action="store_true", help="skip opening PNG files")
    validate.add_argument("--strict", action="store_true", help="treat warnings as errors")
    validate.add_argument("-q", "--quiet", action="store_true", help="do not print warnings")
    validate.set_defaults(func=_cmd_validate)

    hash_cmd = sub.add_parser("hash", help="print the source_sha256 of asset files")
    hash_cmd.add_argument("files", nargs="+", help="files on disk, or MPQ paths with --mpq")
    hash_cmd.add_argument("--mpq", help="read the files from this MPQ")
    hash_cmd.set_defaults(func=_cmd_hash)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    sys.exit(main())
