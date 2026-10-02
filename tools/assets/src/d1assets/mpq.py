"""MPQ archives (format versions 0 and 1), read and write.

Supported: hash/block tables (encrypted), the v1 hi-block table, encrypted
files (incl. ``FIX_KEY``), single-unit and sectored files, and sector
compression with PKWARE DCL implode, zlib and bzip2. Diablo's own archives use
format 0 with imploded, often encrypted, files.

MPQs don't store file names, only hashes: to list an archive you need a list
of candidate names (``(listfile)`` inside the archive, or one you supply).
"""

from __future__ import annotations

import bz2
import os
import struct
import zlib
from collections.abc import Iterable, Iterator, Mapping
from dataclasses import dataclass

from . import pkware

MPQ_MAGIC = b"MPQ\x1a"
MPQ_USERDATA_MAGIC = b"MPQ\x1b"

FILE_IMPLODE = 0x00000100
FILE_COMPRESS = 0x00000200
FILE_ENCRYPTED = 0x00010000
FILE_FIX_KEY = 0x00020000
FILE_SINGLE_UNIT = 0x01000000
FILE_DELETE_MARKER = 0x02000000
FILE_SECTOR_CRC = 0x04000000
FILE_EXISTS = 0x80000000

COMPRESSION_HUFFMAN = 0x01
COMPRESSION_ZLIB = 0x02
COMPRESSION_PKWARE = 0x08
COMPRESSION_BZIP2 = 0x10

HASH_ENTRY_EMPTY = 0xFFFFFFFF
HASH_ENTRY_DELETED = 0xFFFFFFFE

_HASH_TABLE_OFFSET = 0
_HASH_NAME_A = 1
_HASH_NAME_B = 2
_HASH_FILE_KEY = 3

_MASK = 0xFFFFFFFF


class MpqError(ValueError):
    pass


def _make_crypt_table() -> list[int]:
    table = [0] * 0x500
    seed = 0x00100001
    for index1 in range(0x100):
        index2 = index1
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            temp1 = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            temp2 = seed & 0xFFFF
            table[index2] = temp1 | temp2
            index2 += 0x100
    return table


_CRYPT_TABLE = _make_crypt_table()


def hash_string(name: str, hash_type: int) -> int:
    """MPQ string hash. Names are case-insensitive and use ``\\`` separators."""
    seed1, seed2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in name.replace("/", "\\").upper().encode("latin-1"):
        seed1 = (_CRYPT_TABLE[(hash_type << 8) + ch] ^ (seed1 + seed2)) & _MASK
        seed2 = (ch + seed1 + seed2 + (seed2 << 5) + 3) & _MASK
    return seed1


def _crypt(data: bytes, key: int, encrypt: bool) -> bytes:
    n = len(data) // 4
    words = list(struct.unpack_from(f"<{n}I", data))
    seed = 0xEEEEEEEE
    for i, value in enumerate(words):
        seed = (seed + _CRYPT_TABLE[0x400 + (key & 0xFF)]) & _MASK
        out = value ^ ((key + seed) & _MASK)
        plain = value if encrypt else out
        key = ((((~key) << 0x15) + 0x11111111) & _MASK) | (key >> 0x0B)
        seed = (plain + seed + (seed << 5) + 3) & _MASK
        words[i] = out
    return struct.pack(f"<{n}I", *words) + data[4 * n :]


def decrypt(data: bytes, key: int) -> bytes:
    """Decrypts whole 32-bit words; trailing bytes are left as is (like Storm)."""
    return _crypt(data, key, encrypt=False)


def encrypt(data: bytes, key: int) -> bytes:
    return _crypt(data, key, encrypt=True)


def file_key(name: str, offset: int, size: int, flags: int) -> int:
    base = name.replace("/", "\\").rsplit("\\", 1)[-1]
    key = hash_string(base, _HASH_FILE_KEY)
    if flags & FILE_FIX_KEY:
        key = ((key + (offset & _MASK)) ^ size) & _MASK
    return key


@dataclass(frozen=True)
class HashEntry:
    name_a: int
    name_b: int
    locale: int
    platform: int
    block_index: int


@dataclass(frozen=True)
class BlockEntry:
    offset: int  # relative to the archive header, including hi-block bits
    compressed_size: int
    file_size: int
    flags: int


@dataclass(frozen=True)
class MpqHeader:
    archive_offset: int  # absolute position of the header in the file
    header_size: int
    archive_size: int
    format_version: int
    sector_size_shift: int
    hash_table_pos: int
    block_table_pos: int
    hash_table_size: int
    block_table_size: int
    hi_block_table_pos: int = 0

    @property
    def sector_size(self) -> int:
        return 512 << self.sector_size_shift


def _decompress_sector(data: bytes, expected: int) -> bytes:
    mask = data[0]
    payload = data[1:]
    unsupported = mask & ~(COMPRESSION_ZLIB | COMPRESSION_PKWARE | COMPRESSION_BZIP2)
    if unsupported:
        raise MpqError(f"unsupported compression mask 0x{mask:02x}")
    # Decompression order is the reverse of Storm's compression order.
    if mask & COMPRESSION_BZIP2:
        payload = bz2.decompress(payload)
    if mask & COMPRESSION_PKWARE:
        payload = pkware.explode(payload)
    if mask & COMPRESSION_ZLIB:
        payload = zlib.decompress(payload)
    if len(payload) != expected:
        raise MpqError(f"sector decompressed to {len(payload)} bytes, expected {expected}")
    return payload


class MpqArchive:
    """Read-only access to an MPQ archive held in memory."""

    def __init__(self, source: str | os.PathLike[str] | bytes) -> None:
        if isinstance(source, (bytes, bytearray, memoryview)):
            self.data = bytes(source)
        else:
            with open(source, "rb") as f:
                self.data = f.read()
        self.header = self._find_header()
        self.hash_table = self._read_hash_table()
        self.block_table = self._read_block_table()

    def _find_header(self) -> MpqHeader:
        data = self.data
        for pos in range(0, len(data) - 32 + 1, 512):
            magic = data[pos : pos + 4]
            if magic == MPQ_USERDATA_MAGIC:
                header_offset = struct.unpack_from("<I", data, pos + 8)[0]
                if data[pos + header_offset : pos + header_offset + 4] == MPQ_MAGIC:
                    return self._parse_header(pos + header_offset)
            elif magic == MPQ_MAGIC:
                return self._parse_header(pos)
        raise MpqError("no MPQ header found")

    def _parse_header(self, pos: int) -> MpqHeader:
        (header_size, archive_size, version, shift, hash_pos, block_pos, hash_size, block_size) = (
            struct.unpack_from("<IIHHIIII", self.data, pos + 4)
        )
        hi_block_pos = 0
        if version >= 1 and header_size >= 44 and pos + 44 <= len(self.data):
            hi_block_pos, hash_hi, block_hi = struct.unpack_from("<QHH", self.data, pos + 32)
            hash_pos |= hash_hi << 32
            block_pos |= block_hi << 32
        elif version > 1:
            raise MpqError(f"unsupported MPQ format version {version}")
        return MpqHeader(
            pos, header_size, archive_size, version, shift, hash_pos, block_pos, hash_size, block_size,
            hi_block_pos,
        )  # fmt: skip

    def _read_table(self, pos: int, entries: int, key_name: str) -> bytes:
        start = self.header.archive_offset + pos
        raw = self.data[start : start + entries * 16]
        if len(raw) != entries * 16:
            raise MpqError(f"{key_name} is truncated")
        return decrypt(raw, hash_string(key_name, _HASH_FILE_KEY))

    def _read_hash_table(self) -> list[HashEntry]:
        h = self.header
        raw = self._read_table(h.hash_table_pos, h.hash_table_size, "(hash table)")
        return [HashEntry(*struct.unpack_from("<IIHHI", raw, 16 * i)) for i in range(h.hash_table_size)]

    def _read_block_table(self) -> list[BlockEntry]:
        h = self.header
        raw = self._read_table(h.block_table_pos, h.block_table_size, "(block table)")
        hi = [0] * h.block_table_size
        if h.hi_block_table_pos:
            start = h.archive_offset + h.hi_block_table_pos
            hi = list(struct.unpack_from(f"<{h.block_table_size}H", self.data, start))
        entries = []
        for i in range(h.block_table_size):
            offset, csize, fsize, flags = struct.unpack_from("<IIII", raw, 16 * i)
            entries.append(BlockEntry(offset | (hi[i] << 32), csize, fsize, flags))
        return entries

    def find(self, name: str) -> BlockEntry | None:
        """Returns the block entry for ``name`` (any locale), or None."""
        size = len(self.hash_table)
        if size == 0:
            return None
        index = hash_string(name, _HASH_TABLE_OFFSET) & (size - 1)
        name_a = hash_string(name, _HASH_NAME_A)
        name_b = hash_string(name, _HASH_NAME_B)
        for i in range(size):
            entry = self.hash_table[(index + i) % size]
            if entry.block_index == HASH_ENTRY_EMPTY:
                return None
            matches = entry.name_a == name_a and entry.name_b == name_b
            if matches and entry.block_index < len(self.block_table):
                block = self.block_table[entry.block_index]
                if block.flags & FILE_EXISTS and not block.flags & FILE_DELETE_MARKER:
                    return block
        return None

    def __contains__(self, name: object) -> bool:
        return isinstance(name, str) and self.find(name) is not None

    def read(self, name: str) -> bytes:
        """Reads and decompresses a file. Raises ``KeyError`` if it is missing."""
        block = self.find(name)
        if block is None:
            raise KeyError(name)
        return self._read_block(name, block)

    def _read_block(self, name: str, block: BlockEntry) -> bytes:
        start = self.header.archive_offset + block.offset
        raw = self.data[start : start + block.compressed_size]
        if len(raw) != block.compressed_size:
            raise MpqError(f"{name}: file data is truncated")
        if block.file_size == 0:
            return b""
        key = 0
        if block.flags & FILE_ENCRYPTED:
            key = file_key(name, block.offset, block.file_size, block.flags)
        compressed = bool(block.flags & (FILE_IMPLODE | FILE_COMPRESS))

        if block.flags & FILE_SINGLE_UNIT:
            if block.flags & FILE_ENCRYPTED:
                raw = decrypt(raw, key)
            if compressed and block.compressed_size < block.file_size:
                return self._decompress(raw, block.file_size, block.flags)
            return raw[: block.file_size]

        sector_size = self.header.sector_size
        num_sectors = (block.file_size + sector_size - 1) // sector_size
        if not compressed:
            out = bytearray()
            for i in range(num_sectors):
                chunk = raw[i * sector_size : (i + 1) * sector_size]
                out += decrypt(chunk, (key + i) & _MASK) if block.flags & FILE_ENCRYPTED else chunk
            return bytes(out[: block.file_size])

        table_entries = num_sectors + 1 + (1 if block.flags & FILE_SECTOR_CRC else 0)
        table = raw[: 4 * table_entries]
        if block.flags & FILE_ENCRYPTED:
            table = decrypt(table, (key - 1) & _MASK)
        offsets = struct.unpack_from(f"<{num_sectors + 1}I", table)
        out = bytearray()
        for i in range(num_sectors):
            expected = min(sector_size, block.file_size - i * sector_size)
            sector = raw[offsets[i] : offsets[i + 1]]
            if block.flags & FILE_ENCRYPTED:
                sector = decrypt(sector, (key + i) & _MASK)
            if len(sector) < expected:
                out += self._decompress(sector, expected, block.flags)
            else:
                out += sector[:expected]
        return bytes(out)

    @staticmethod
    def _decompress(data: bytes, expected: int, flags: int) -> bytes:
        if flags & FILE_IMPLODE:
            return pkware.explode(data, expected)
        return _decompress_sector(data, expected)

    def listfile(self) -> list[str]:
        """Names from the archive's ``(listfile)``, if it has one."""
        if "(listfile)" not in self:
            return []
        text = self.read("(listfile)").decode("latin-1")
        return [line.strip() for line in text.replace(";", "\n").splitlines() if line.strip()]

    def known_files(self, candidates: Iterable[str] = ()) -> list[str]:
        """Returns the names from the listfile and ``candidates`` that exist in the archive."""
        seen: dict[str, None] = {}
        for name in [*self.listfile(), *candidates]:
            if name and name.upper() not in {n.upper() for n in seen} and name in self:
                seen[name] = None
        return list(seen)

    def iter_files(self, candidates: Iterable[str] = ()) -> Iterator[tuple[str, bytes]]:
        for name in self.known_files(candidates):
            yield name, self.read(name)


# --- Writing -------------------------------------------------------------------------------------------

COMPRESSIONS = ("none", "implode", "zlib", "bzip2")


def _compress(data: bytes, compression: str) -> bytes:
    """Compresses one sector; returns the raw data when compression doesn't help."""
    if compression == "implode":
        packed = pkware.implode(data)
    elif compression == "zlib":
        packed = bytes((COMPRESSION_ZLIB,)) + zlib.compress(data, 9)
    elif compression == "bzip2":
        packed = bytes((COMPRESSION_BZIP2,)) + bz2.compress(data, 9)
    else:
        return data
    return packed if len(packed) < len(data) else data


def write_mpq(
    files: Mapping[str, bytes],
    *,
    format_version: int = 0,
    compression: str = "implode",
    encrypted: bool = False,
    fix_key: bool = False,
    single_unit: bool = False,
    sector_size_shift: int = 3,
    add_listfile: bool = True,
    hi_block_table: bool = False,
) -> bytes:
    """Builds an MPQ archive in memory (used by the tests and for packing small mods).

    ``compression`` is one of ``"none"``, ``"implode"`` (Diablo's choice; sets
    ``FILE_IMPLODE``), ``"zlib"`` or ``"bzip2"`` (``FILE_COMPRESS``).
    """
    if compression not in COMPRESSIONS:
        raise ValueError(f"compression must be one of {COMPRESSIONS}")
    if format_version not in (0, 1):
        raise ValueError("format_version must be 0 or 1")
    entries = dict(files)
    if add_listfile:
        entries["(listfile)"] = "\r\n".join(n for n in files).encode("latin-1") + b"\r\n"
    header_size = 32 if format_version == 0 else 44
    sector_size = 512 << sector_size_shift

    flags = FILE_EXISTS
    if compression == "implode":
        flags |= FILE_IMPLODE
    elif compression != "none":
        flags |= FILE_COMPRESS
    if encrypted:
        flags |= FILE_ENCRYPTED
        if fix_key:
            flags |= FILE_FIX_KEY
    if single_unit:
        flags |= FILE_SINGLE_UNIT

    body = bytearray()
    blocks: list[BlockEntry] = []
    for name, content in entries.items():
        content = bytes(content)
        offset = header_size + len(body)
        file_flags = flags if content else FILE_EXISTS
        key = file_key(name, offset, len(content), file_flags)
        if not content:
            blob = b""
        elif single_unit:
            blob = _compress(content, compression)
            if encrypted:
                blob = encrypt(blob, key)
        else:
            sectors = [content[i : i + sector_size] for i in range(0, len(content), sector_size)]
            packed = [_compress(s, compression) for s in sectors]
            if encrypted:
                packed = [encrypt(s, (key + i) & _MASK) for i, s in enumerate(packed)]
            if compression == "none":
                blob = b"".join(packed)
            else:
                offsets = [4 * (len(packed) + 1)]
                for s in packed:
                    offsets.append(offsets[-1] + len(s))
                table = struct.pack(f"<{len(offsets)}I", *offsets)
                if encrypted:
                    table = encrypt(table, (key - 1) & _MASK)
                blob = table + b"".join(packed)
        blocks.append(BlockEntry(offset, len(blob), len(content), file_flags))
        body += blob

    hash_size = 16
    while hash_size < 2 * len(entries):
        hash_size *= 2
    slots: list[HashEntry | None] = [None] * hash_size
    for block_index, name in enumerate(entries):
        index = hash_string(name, _HASH_TABLE_OFFSET) & (hash_size - 1)
        while slots[index] is not None:
            index = (index + 1) % hash_size
        slots[index] = HashEntry(
            hash_string(name, _HASH_NAME_A), hash_string(name, _HASH_NAME_B), 0, 0, block_index
        )
    empty = HashEntry(_MASK, _MASK, 0xFFFF, 0xFFFF, HASH_ENTRY_EMPTY)
    hash_raw = b"".join(struct.pack("<IIHHI", *_astuple(e or empty)) for e in slots)
    block_raw = b"".join(
        struct.pack("<IIII", b.offset & _MASK, b.compressed_size, b.file_size, b.flags) for b in blocks
    )
    hash_pos = header_size + len(body)
    block_pos = hash_pos + len(hash_raw)
    tail = encrypt(hash_raw, hash_string("(hash table)", _HASH_FILE_KEY)) + encrypt(
        block_raw, hash_string("(block table)", _HASH_FILE_KEY)
    )
    hi_pos = 0
    if format_version == 1 and hi_block_table:
        hi_pos = block_pos + len(block_raw)
        tail += struct.pack(f"<{len(blocks)}H", *(b.offset >> 32 for b in blocks))
    archive_size = header_size + len(body) + len(tail)
    header = MPQ_MAGIC + struct.pack(
        "<IIHHIIII",
        header_size,
        archive_size,
        format_version,
        sector_size_shift,
        hash_pos,
        block_pos,
        hash_size,
        len(blocks),
    )
    if format_version == 1:
        header += struct.pack("<QHH", hi_pos, 0, 0)
    return header + bytes(body) + tail


def _astuple(entry: HashEntry) -> tuple[int, int, int, int, int]:
    return (entry.name_a, entry.name_b, entry.locale, entry.platform, entry.block_index)
