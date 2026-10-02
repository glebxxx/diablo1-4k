"""Frame containers shared by CEL, CL2, CLX and level CEL files.

A *frame list* is::

    uint32 frame_count
    uint32 frame_offset[frame_count + 1]   # relative to the list start; the
                                           # last one is the list size
    frame data...

A *grouped* file (e.g. one group per direction) starts with ``group_count``
uint32 offsets to frame lists, followed by the lists. It has no explicit group
count: the first offset is ``4 * group_count``.

Detection follows the engine (``Source/utils/cel_to_clx.cpp``): if the first
word, read as a frame count, gives a last offset equal to the file size, the
file is a single frame list; otherwise it is grouped.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence

FrameList = list[bytes]


def _u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"truncated container: need 4 bytes at offset {offset}, size {len(data)}")
    return int(struct.unpack_from("<I", data, offset)[0])


def is_grouped(data: bytes) -> bool:
    """Returns True if ``data`` looks like a grouped container (engine heuristic)."""
    count = _u32(data, 0)
    end_pos = 4 * count + 4
    return not (end_pos + 4 <= len(data) and _u32(data, end_pos) == len(data))


def read_frame_list(data: bytes, start: int = 0) -> FrameList:
    """Reads a single frame list beginning at ``start``."""
    count = _u32(data, start)
    end = len(data)
    if 4 * (count + 2) > end - start:
        raise ValueError(f"frame count {count} does not fit in {end - start} bytes")
    offsets = [_u32(data, start + 4 + 4 * i) for i in range(count + 1)]
    frames: FrameList = []
    for i in range(count):
        begin, finish = start + offsets[i], start + offsets[i + 1]
        if not (start <= begin <= finish <= end):
            raise ValueError(f"frame {i} has invalid offsets {offsets[i]}..{offsets[i + 1]}")
        frames.append(bytes(data[begin:finish]))
    return frames


def read_container(data: bytes, grouped: bool | None = None) -> list[FrameList]:
    """Reads a container into a list of groups.

    A non-grouped file yields exactly one group. Pass ``grouped`` to override
    the engine's detection heuristic.
    """
    data = bytes(data)
    if grouped is None:
        grouped = is_grouped(data)
    if not grouped:
        return [read_frame_list(data)]
    first = _u32(data, 0)
    if first % 4 != 0 or first == 0:
        raise ValueError(f"invalid group header size {first}")
    num_groups = first // 4
    starts = [_u32(data, 4 * g) for g in range(num_groups)]
    groups: list[FrameList] = []
    # Frame offsets are relative to their list header, but frame data need not follow it:
    # Diablo's files put all list headers first, then all frame data.
    for begin in starts:
        groups.append(read_frame_list(data, begin))
    return groups


def write_frame_list(frames: Sequence[bytes]) -> bytes:
    header_size = 4 * (len(frames) + 2)
    offsets = [header_size]
    for frame in frames:
        offsets.append(offsets[-1] + len(frame))
    return struct.pack(f"<{len(offsets) + 1}I", len(frames), *offsets) + b"".join(frames)


def write_container(groups: Sequence[Sequence[bytes]], grouped: bool | None = None) -> bytes:
    """Writes groups of frames. ``grouped`` defaults to ``len(groups) != 1``."""
    if grouped is None:
        grouped = len(groups) != 1
    if not grouped:
        if len(groups) != 1:
            raise ValueError("a non-grouped container holds exactly one group")
        return write_frame_list(groups[0])
    if not groups:
        raise ValueError("a grouped container needs at least one group")
    lists = [write_frame_list(g) for g in groups]
    offsets = [4 * len(lists)]
    for blob in lists[:-1]:
        offsets.append(offsets[-1] + len(blob))
    return struct.pack(f"<{len(offsets)}I", *offsets) + b"".join(lists)
