"""PKWARE Data Compression Library "implode" (compression type 0x08 in MPQs).

The decoder follows the format as documented in Mark Adler's ``blast.c``
(zlib ``contrib/blast``). The encoder is a small greedy LZ77 matcher that
produces streams any DCL "explode" implementation accepts.

Stream layout: one byte literal mode (0 = raw 8-bit literals, 1 = Huffman
coded literals), one byte dictionary size bits (4, 5 or 6 for 1, 2 or 4 KiB),
then an LSB-first bit stream of tokens:

* ``0`` + literal;
* ``1`` + length code (+ extra bits) + distance code + low distance bits.

Length 519 marks the end of the stream. Huffman codes are sent bit-inverted.
"""

from __future__ import annotations

from collections.abc import Sequence

# Compact code-length tables from blast.c: low nibble = length, high nibble + 1 = repeat count.
_LITLEN = bytes(
    [
        11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8,
        9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5,
        7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
        8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27,
        44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45,
        44, 173,
    ]
)  # fmt: skip
_LENLEN = bytes([2, 35, 36, 53, 38, 23])
_DISTLEN = bytes([2, 20, 53, 230, 247, 151, 248])

_LEN_BASE = (3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264)
_LEN_EXTRA = (0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8)

MIN_MATCH = 2
MAX_MATCH = 518
_END_LENGTH = 519

CMP_BINARY = 0
CMP_ASCII = 1


class PkwareError(ValueError):
    pass


def _expand_lengths(rep: bytes) -> list[int]:
    lengths: list[int] = []
    for byte in rep:
        lengths.extend([byte & 15] * ((byte >> 4) + 1))
    return lengths


def _reverse(value: int, bits: int) -> int:
    result = 0
    for _ in range(bits):
        result = (result << 1) | (value & 1)
        value >>= 1
    return result


class _Huffman:
    """Canonical code (blast.c ``construct``) with an LSB-first lookup table."""

    def __init__(self, rep: bytes) -> None:
        self.lengths = _expand_lengths(rep)
        self.max_bits = max(self.lengths)
        # Canonical codes: by length, then by symbol.
        self.codes = [0] * len(self.lengths)
        code = 0
        for length in range(1, self.max_bits + 1):
            for symbol, sym_len in enumerate(self.lengths):
                if sym_len == length:
                    self.codes[symbol] = code
                    code += 1
            code <<= 1
        # Stream bits: inverted code, MSB first, packed LSB-first.
        self.stream_bits = [_reverse(c ^ ((1 << n) - 1), n) for c, n in zip(self.codes, self.lengths)]
        size = 1 << self.max_bits
        self.table: list[tuple[int, int]] = [(-1, 0)] * size
        for symbol, (bits, n) in enumerate(zip(self.stream_bits, self.lengths)):
            for high in range(1 << (self.max_bits - n)):
                self.table[bits | (high << n)] = (symbol, n)


_LIT = _Huffman(_LITLEN)
_LEN = _Huffman(_LENLEN)
_DIST = _Huffman(_DISTLEN)


class _BitReader:
    def __init__(self, data: bytes, pos: int) -> None:
        self.data = data
        self.pos = pos
        self.buf = 0
        self.cnt = 0

    def _fill(self, need: int) -> None:
        while self.cnt < need:
            if self.pos < len(self.data):
                self.buf |= self.data[self.pos] << self.cnt
            self.pos += 1  # Past the end, pad with zero bits; `_check` catches their use.
            self.cnt += 8

    def bits(self, n: int) -> int:
        if n == 0:
            return 0
        self._fill(n)
        value = self.buf & ((1 << n) - 1)
        self.buf >>= n
        self.cnt -= n
        self._check()
        return value

    def decode(self, huff: _Huffman) -> int:
        self._fill(huff.max_bits)
        symbol, n = huff.table[self.buf & ((1 << huff.max_bits) - 1)]
        if symbol < 0:
            raise PkwareError("invalid Huffman code")
        self.buf >>= n
        self.cnt -= n
        self._check()
        return symbol

    def _check(self) -> None:
        # Bits consumed beyond the real input mean the stream is truncated.
        if self.pos * 8 - self.cnt > len(self.data) * 8:
            raise PkwareError("unexpected end of implode stream")


def explode(data: bytes, expected_size: int | None = None) -> bytes:
    """Decompresses a PKWARE DCL implode stream."""
    if len(data) < 2:
        raise PkwareError("implode stream too short")
    lit, dict_bits = data[0], data[1]
    if lit > 1:
        raise PkwareError(f"invalid literal mode {lit}")
    if not 4 <= dict_bits <= 6:
        raise PkwareError(f"invalid dictionary size {dict_bits}")
    reader = _BitReader(data, 2)
    out = bytearray()
    while True:
        if reader.bits(1):
            symbol = reader.decode(_LEN)
            length = _LEN_BASE[symbol] + reader.bits(_LEN_EXTRA[symbol])
            if length == _END_LENGTH:
                break
            low_bits = 2 if length == 2 else dict_bits
            dist = (reader.decode(_DIST) << low_bits) + reader.bits(low_bits) + 1
            if dist > len(out):
                raise PkwareError("distance too far back")
            while length:
                n = min(length, dist)
                start = len(out) - dist
                out += out[start : start + n]
                length -= n
        else:
            out.append(reader.decode(_LIT) if lit else reader.bits(8))
        if expected_size is not None and len(out) > expected_size:
            raise PkwareError("implode stream longer than expected")
    if expected_size is not None and len(out) != expected_size:
        raise PkwareError(f"implode stream produced {len(out)} bytes, expected {expected_size}")
    return bytes(out)


class _BitWriter:
    def __init__(self) -> None:
        self.out = bytearray()
        self.buf = 0
        self.cnt = 0

    def put(self, value: int, n: int) -> None:
        self.buf |= value << self.cnt
        self.cnt += n
        while self.cnt >= 8:
            self.out.append(self.buf & 0xFF)
            self.buf >>= 8
            self.cnt -= 8

    def code(self, huff: _Huffman, symbol: int) -> None:
        self.put(huff.stream_bits[symbol], huff.lengths[symbol])

    def finish(self) -> bytes:
        if self.cnt:
            self.out.append(self.buf & 0xFF)
        return bytes(self.out)


def _put_length(writer: _BitWriter, length: int) -> None:
    for symbol in range(len(_LEN_BASE)):
        base, extra = _LEN_BASE[symbol], _LEN_EXTRA[symbol]
        if base <= length < base + (1 << extra):
            writer.code(_LEN, symbol)
            writer.put(length - base, extra)
            return
    raise AssertionError(f"unencodable length {length}")


def _longest_match(
    data: bytes, pos: int, candidates: Sequence[int], max_dist_long: int, max_chain: int
) -> tuple[int, int]:
    best_len, best_dist = 0, 0
    limit = min(MAX_MATCH, len(data) - pos)
    checked = 0
    for cand in reversed(candidates):
        dist = pos - cand
        if dist > max_dist_long:
            break
        checked += 1
        if checked > max_chain:
            break
        length = 0
        while length < limit and data[cand + length] == data[pos + length]:
            length += 1
        if length == 2 and dist > 256:
            continue
        if length > best_len:
            best_len, best_dist = length, dist
            if length == limit:
                break
    return best_len, best_dist


def implode(data: bytes, mode: int = CMP_BINARY, dict_bits: int = 6, max_chain: int = 64) -> bytes:
    """Compresses ``data`` into a PKWARE DCL implode stream."""
    if mode not in (CMP_BINARY, CMP_ASCII):
        raise ValueError("mode must be CMP_BINARY or CMP_ASCII")
    if not 4 <= dict_bits <= 6:
        raise ValueError("dict_bits must be 4, 5 or 6")
    data = bytes(data)
    max_dist = 64 << dict_bits
    writer = _BitWriter()
    writer.out += bytes((mode, dict_bits))
    chains: dict[bytes, list[int]] = {}

    def remember(p: int) -> None:
        if p + 2 <= len(data):
            chain = chains.setdefault(data[p : p + 2], [])
            chain.append(p)
            if len(chain) > 4 * max_chain:
                del chain[: 2 * max_chain]

    pos = 0
    while pos < len(data):
        length, dist = 0, 0
        if pos + MIN_MATCH <= len(data):
            candidates = chains.get(data[pos : pos + 2])
            if candidates:
                length, dist = _longest_match(data, pos, candidates, max_dist, max_chain)
        if length >= MIN_MATCH:
            writer.put(1, 1)
            _put_length(writer, length)
            low_bits = 2 if length == 2 else dict_bits
            d = dist - 1
            writer.code(_DIST, d >> low_bits)
            writer.put(d & ((1 << low_bits) - 1), low_bits)
            for p in range(pos, pos + length):
                remember(p)
            pos += length
        else:
            writer.put(0, 1)
            if mode == CMP_ASCII:
                writer.code(_LIT, data[pos])
            else:
                writer.put(data[pos], 8)
            remember(pos)
            pos += 1
    writer.put(1, 1)
    _put_length(writer, _END_LENGTH)
    return writer.finish()
