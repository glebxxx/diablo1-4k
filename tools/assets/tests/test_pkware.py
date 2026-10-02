import os

import pytest
from hypothesis import given
from hypothesis import strategies as st

from d1assets import pkware


def test_blast_reference_vector() -> None:
    # The example stream from zlib's contrib/blast/blast.c.
    assert pkware.explode(bytes.fromhex("00048224258f807f")) == b"AIAIAIAIAIAIA"


@pytest.mark.parametrize("huff", [pkware._LIT, pkware._LEN, pkware._DIST])
def test_code_tables_are_complete(huff: pkware._Huffman) -> None:
    assert sum(2.0**-n for n in huff.lengths) == 1.0


def test_table_sizes() -> None:
    assert len(pkware._LIT.lengths) == 256
    assert len(pkware._LEN.lengths) == 16
    assert len(pkware._DIST.lengths) == 64


@given(
    st.one_of(
        st.binary(max_size=3000),
        st.builds(lambda b, n: b * n, st.binary(min_size=1, max_size=20), st.integers(1, 400)),
    ),
    st.sampled_from([pkware.CMP_BINARY, pkware.CMP_ASCII]),
    st.sampled_from([4, 5, 6]),
)
def test_round_trip(data: bytes, mode: int, dict_bits: int) -> None:
    packed = pkware.implode(data, mode, dict_bits)
    assert packed[:2] == bytes((mode, dict_bits))
    assert pkware.explode(packed, len(data)) == data


def test_long_matches_and_far_distances() -> None:
    data = os.urandom(5000)
    data = data + data[:600] + bytes(2000) + data[1000:1003]
    for bits in (4, 5, 6):
        assert pkware.explode(pkware.implode(data, dict_bits=bits)) == data


def test_compresses_repetitive_data() -> None:
    data = b"Diablo " * 1000
    assert len(pkware.implode(data)) < len(data) // 20


@pytest.mark.parametrize("bad", [b"", b"\x02\x04", b"\x00\x07", b"\x00\x04"])
def test_rejects_invalid_streams(bad: bytes) -> None:
    with pytest.raises(pkware.PkwareError):
        pkware.explode(bad)


def test_rejects_truncated_stream() -> None:
    packed = pkware.implode(os.urandom(200))
    with pytest.raises(pkware.PkwareError):
        pkware.explode(packed[: len(packed) // 2])


def test_rejects_wrong_expected_size() -> None:
    with pytest.raises(pkware.PkwareError):
        pkware.explode(pkware.implode(b"abc"), expected_size=4)
