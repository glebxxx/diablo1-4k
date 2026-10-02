import struct

import pytest
from hypothesis import given
from hypothesis import strategies as st

from d1assets import mpq

FILES = {
    "levels\\l1data\\test.cel": bytes(range(256)) * 40,
    "data\\tiny.bin": b"x",
    "monsters\\a\\b.cl2": b"hello mpq " * 700,
    "empty.txt": b"",
}


def test_known_hash_constants() -> None:
    # Well-known keys of the encrypted hash and block tables.
    assert mpq.hash_string("(hash table)", 3) == 0xC3AF3770
    assert mpq.hash_string("(block table)", 3) == 0xEC83B3A3


def test_hash_is_case_and_separator_insensitive() -> None:
    for kind in range(4):
        assert mpq.hash_string("Levels\\L1Data\\L1.CEL", kind) == mpq.hash_string(
            "levels/l1data/l1.cel", kind
        )


@given(st.binary(max_size=200), st.integers(0, 0xFFFFFFFF))
def test_encrypt_round_trip(data: bytes, key: int) -> None:
    encrypted = mpq.encrypt(data, key)
    assert len(encrypted) == len(data)
    assert mpq.decrypt(encrypted, key) == data


@pytest.mark.parametrize("version", [0, 1])
@pytest.mark.parametrize("compression", mpq.COMPRESSIONS)
@pytest.mark.parametrize(("encrypted", "fix_key"), [(False, False), (True, False), (True, True)])
@pytest.mark.parametrize("single_unit", [False, True])
def test_archive_round_trip(
    version: int, compression: str, encrypted: bool, fix_key: bool, single_unit: bool
) -> None:
    blob = mpq.write_mpq(
        FILES,
        format_version=version,
        compression=compression,
        encrypted=encrypted,
        fix_key=fix_key,
        single_unit=single_unit,
        hi_block_table=version == 1,
    )
    archive = mpq.MpqArchive(blob)
    assert archive.header.format_version == version
    assert archive.header.header_size == (32 if version == 0 else 44)
    for name, content in FILES.items():
        assert archive.read(name) == content
        assert archive.read(name.upper().replace("\\", "/")) == content
    assert sorted(archive.listfile()) == sorted(FILES)
    assert sorted(archive.known_files()) == sorted(FILES)


@given(
    st.dictionaries(
        st.from_regex(r"[a-z]{1,8}\\[a-z0-9_]{1,12}\.(cel|cl2|pal)", fullmatch=True),
        st.binary(max_size=9000),
        max_size=6,
    ),
    st.sampled_from(mpq.COMPRESSIONS),
    st.booleans(),
    st.integers(0, 3),
)
def test_archive_round_trip_property(
    files: dict[str, bytes], compression: str, encrypted: bool, shift: int
) -> None:
    # MPQ names are case-insensitive; the regex only produces lowercase names.
    archive = mpq.MpqArchive(
        mpq.write_mpq(files, compression=compression, encrypted=encrypted, sector_size_shift=shift)
    )
    for name, content in files.items():
        assert archive.read(name) == content


def test_missing_file() -> None:
    archive = mpq.MpqArchive(mpq.write_mpq(FILES))
    assert "nope.cel" not in archive
    with pytest.raises(KeyError):
        archive.read("nope.cel")


def test_without_listfile_names_come_from_candidates() -> None:
    archive = mpq.MpqArchive(mpq.write_mpq(FILES, add_listfile=False))
    assert archive.listfile() == []
    assert archive.known_files(["data\\tiny.bin", "other"]) == ["data\\tiny.bin"]


def test_header_after_user_data_block() -> None:
    inner = mpq.write_mpq(FILES)
    # An 'MPQ\x1b' user data header at 0 points at the archive at offset 512.
    user = b"MPQ\x1b" + struct.pack("<III", 512 - 16, 512, 16)
    archive = mpq.MpqArchive(user + bytes(512 - len(user)) + inner)
    assert archive.header.archive_offset == 512
    assert archive.read("data\\tiny.bin") == b"x"


def test_header_at_sector_offset() -> None:
    archive = mpq.MpqArchive(bytes(1024) + mpq.write_mpq(FILES))
    assert archive.read("monsters\\a\\b.cl2") == FILES["monsters\\a\\b.cl2"]


def test_not_an_archive() -> None:
    with pytest.raises(mpq.MpqError):
        mpq.MpqArchive(bytes(4096))


def test_unsupported_compression_mask() -> None:
    with pytest.raises(mpq.MpqError):
        mpq._decompress_sector(b"\x01abc", 10)
