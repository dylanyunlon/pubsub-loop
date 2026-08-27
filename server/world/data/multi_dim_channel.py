"""
world.data.multi_dim_channel — multi-dimensional strided channel for pub/sub
=============================================================================

PRD #37: MultiDimChannel<T, Rank> with strided layout, zero-copy inter-node
pub/sub sharing, and node-scope typed views.

Solves three problems:
1. Spatial grid individuals (2D/3D) no longer compute manual strides
2. Multi-attribute state tensors get composable slice/subview API
3. Time-windowed history gets structured representation

Sources:
  - CyberRT: data/channel_buffer
  - NumPy: ndarray stride model
  - C++23: std::mdspan
"""

from __future__ import annotations

import array
import math
import struct
import threading
from dataclasses import dataclass, field
from enum import Enum
from typing import (
    Any, Callable, Generic, Iterator, List, Optional, Sequence,
    Tuple, TypeVar, Union,
)

T = TypeVar('T')


class Layout(Enum):
    ROW_MAJOR = 'row_major'      # C-order: last dim contiguous
    COLUMN_MAJOR = 'column_major'  # Fortran-order: first dim contiguous
    STRIDED = 'strided'           # custom strides


# ---------------------------------------------------------------------------
#  ChannelHeader — metadata for shared-memory layout
# ---------------------------------------------------------------------------

HEADER_MAGIC = 0x4D444348  # 'MDCH'
HEADER_SIZE = 64           # cache-line aligned

@dataclass
class ChannelHeader:
    """Metadata stored at the start of a shared-memory segment."""
    magic: int = HEADER_MAGIC
    rank: int = 0
    extents: Tuple[int, ...] = ()
    strides: Tuple[int, ...] = ()
    element_size: int = 0
    layout: Layout = Layout.ROW_MAJOR
    total_elements: int = 0
    seq_num: int = 0            # monotonic write counter
    committed: bool = False     # release fence flag

    def pack(self) -> bytes:
        """Serialize header to bytes for shared-memory."""
        # Fixed format: magic(4) + rank(4) + 8 extents(8*8) + 8 strides(8*8)
        #               + element_size(4) + layout(4) + total(8) + seq(8) + committed(4)
        ext = list(self.extents) + [0] * (8 - len(self.extents))
        strd = list(self.strides) + [0] * (8 - len(self.strides))
        return struct.pack(
            '<II8Q8QIIQQ?3x',
            self.magic, self.rank,
            *ext[:8], *strd[:8],
            self.element_size,
            self.layout.value.encode()[0] if isinstance(self.layout.value, str) else 0,
            self.total_elements,
            self.seq_num,
            self.committed,
        )

    @classmethod
    def unpack(cls, data: bytes) -> 'ChannelHeader':
        vals = struct.unpack('<II8Q8QIIQQ?3x', data[:HEADER_SIZE + 84])
        magic, rank = vals[0], vals[1]
        extents = tuple(vals[2:2 + rank])
        strides = tuple(vals[10:10 + rank])
        return cls(
            magic=magic, rank=rank,
            extents=extents, strides=strides,
            element_size=vals[18],
            total_elements=vals[20], seq_num=vals[21],
            committed=vals[22],
        )


# ---------------------------------------------------------------------------
#  Stride computation
# ---------------------------------------------------------------------------

def compute_strides(extents: Tuple[int, ...], layout: Layout) -> Tuple[int, ...]:
    """Compute byte strides for a given shape and layout order."""
    rank = len(extents)
    if rank == 0:
        return ()
    strides = [0] * rank
    if layout == Layout.ROW_MAJOR:
        strides[-1] = 1
        for i in range(rank - 2, -1, -1):
            strides[i] = strides[i + 1] * extents[i + 1]
    elif layout == Layout.COLUMN_MAJOR:
        strides[0] = 1
        for i in range(1, rank):
            strides[i] = strides[i - 1] * extents[i - 1]
    else:
        raise ValueError("Use custom strides for Layout.STRIDED")
    return tuple(strides)


def _flat_index(indices: Tuple[int, ...], strides: Tuple[int, ...]) -> int:
    """Convert multi-dim indices to flat offset."""
    return sum(i * s for i, s in zip(indices, strides))


# ---------------------------------------------------------------------------
#  MultiDimView — zero-copy view with stride-based indexing
# ---------------------------------------------------------------------------

class MultiDimView(Generic[T]):
    """A strided view over a flat buffer, supporting multi-dimensional access.

    This is the Python equivalent of C++ mdspan.  Supports:
    - operator() via __getitem__ / __setitem__ with tuple indices
    - slice<Dim>(index) → lower-rank view
    - subview<Dim>(start, count) → same-rank view over a subset
    """

    __slots__ = ('_buffer', '_extents', '_strides', '_offset', '_rank', '_writable')

    def __init__(
        self,
        buffer: list,
        extents: Tuple[int, ...],
        strides: Tuple[int, ...],
        offset: int = 0,
        writable: bool = False,
    ):
        self._buffer = buffer
        self._extents = extents
        self._strides = strides
        self._offset = offset
        self._rank = len(extents)
        self._writable = writable

    @property
    def rank(self) -> int:
        return self._rank

    @property
    def extents(self) -> Tuple[int, ...]:
        return self._extents

    @property
    def strides(self) -> Tuple[int, ...]:
        return self._strides

    @property
    def total_elements(self) -> int:
        result = 1
        for e in self._extents:
            result *= e
        return result

    def _check_indices(self, indices: Tuple[int, ...]):
        if len(indices) != self._rank:
            raise IndexError(
                f"Expected {self._rank} indices, got {len(indices)}"
            )
        for i, (idx, ext) in enumerate(zip(indices, self._extents)):
            if not (0 <= idx < ext):
                raise IndexError(
                    f"Index {idx} out of range [0, {ext}) for dimension {i}"
                )

    def __getitem__(self, indices) -> T:
        if not isinstance(indices, tuple):
            indices = (indices,)
        self._check_indices(indices)
        flat = self._offset + _flat_index(indices, self._strides)
        return self._buffer[flat]

    def __setitem__(self, indices, value: T):
        if not self._writable:
            raise TypeError("View is read-only")
        if not isinstance(indices, tuple):
            indices = (indices,)
        self._check_indices(indices)
        flat = self._offset + _flat_index(indices, self._strides)
        self._buffer[flat] = value

    def slice(self, dim: int, index: int) -> 'MultiDimView[T]':
        """Fix dimension *dim* at *index*, returning a view with rank-1.

        Example: 2D view.slice(1, k) → 1D view of column k.
        """
        if dim < 0 or dim >= self._rank:
            raise IndexError(f"Dimension {dim} out of range [0, {self._rank})")
        if not (0 <= index < self._extents[dim]):
            raise IndexError(f"Index {index} out of range for dim {dim}")

        new_offset = self._offset + index * self._strides[dim]
        new_extents = self._extents[:dim] + self._extents[dim + 1:]
        new_strides = self._strides[:dim] + self._strides[dim + 1:]
        return MultiDimView(
            self._buffer, new_extents, new_strides,
            new_offset, self._writable,
        )

    def subview(self, dim: int, start: int, count: int) -> 'MultiDimView[T]':
        """Return a view restricted to [start, start+count) along *dim*.

        Same rank, narrower extent.
        """
        if dim < 0 or dim >= self._rank:
            raise IndexError(f"Dimension {dim} out of range")
        if start < 0 or start + count > self._extents[dim]:
            raise IndexError(f"Subview [{start}:{start + count}) out of range")

        new_offset = self._offset + start * self._strides[dim]
        new_extents = (
            self._extents[:dim] + (count,) + self._extents[dim + 1:]
        )
        return MultiDimView(
            self._buffer, new_extents, self._strides,
            new_offset, self._writable,
        )

    def rows(self) -> Iterator['MultiDimView[T]']:
        """Iterate the first dimension, yielding (rank-1) sub-views."""
        if self._rank < 1:
            return
        for i in range(self._extents[0]):
            yield self.slice(0, i)

    def to_list(self) -> list:
        """Flatten to a list (copies data)."""
        if self._rank == 0:
            return []
        if self._rank == 1:
            return [
                self._buffer[self._offset + i * self._strides[0]]
                for i in range(self._extents[0])
            ]
        result = []
        for row_view in self.rows():
            result.append(row_view.to_list())
        return result

    @property
    def data(self) -> list:
        """Direct access to underlying buffer."""
        return self._buffer


# ---------------------------------------------------------------------------
#  MultiDimChannel
# ---------------------------------------------------------------------------

class MultiDimChannel(Generic[T]):
    """Multi-dimensional pub/sub channel with strided layout.

    Usage::

        # Publisher creates a 128x128 grid channel
        ch = MultiDimChannel.create((128, 128), layout=Layout.ROW_MAJOR)
        view = ch.writable_view()
        view[64, 32] = 1.0
        view.commit()

        # Subscriber reads
        rv = ch.read_view()
        val = rv[64, 32]  # 1.0
        col_32 = rv.slice(1, 32)  # 1D view of column 32
    """

    __slots__ = (
        '_buffer', '_header', '_lock', '_channel_name',
        '_committed_seq', '_write_seq',
    )

    def __init__(
        self,
        buffer: list,
        header: ChannelHeader,
        channel_name: str = '',
    ):
        self._buffer = buffer
        self._header = header
        self._lock = threading.Lock()
        self._channel_name = channel_name
        self._committed_seq = 0
        self._write_seq = 0

    @classmethod
    def create(
        cls,
        extents: Tuple[int, ...],
        layout: Layout = Layout.ROW_MAJOR,
        channel_name: str = '',
        default_value: Any = 0.0,
        strides: Optional[Tuple[int, ...]] = None,
    ) -> 'MultiDimChannel':
        """Create a new channel with given shape."""
        rank = len(extents)
        if strides is None:
            computed_strides = compute_strides(extents, layout)
        else:
            computed_strides = strides
            layout = Layout.STRIDED

        total = 1
        for e in extents:
            total *= e

        buffer = [default_value] * total
        header = ChannelHeader(
            rank=rank,
            extents=extents,
            strides=computed_strides,
            element_size=8,  # Python float
            layout=layout,
            total_elements=total,
        )
        return cls(buffer, header, channel_name)

    @property
    def extents(self) -> Tuple[int, ...]:
        return self._header.extents

    @property
    def strides(self) -> Tuple[int, ...]:
        return self._header.strides

    @property
    def layout(self) -> Layout:
        return self._header.layout

    @property
    def rank(self) -> int:
        return self._header.rank

    @property
    def total_elements(self) -> int:
        return self._header.total_elements

    @property
    def channel_name(self) -> str:
        return self._channel_name

    @property
    def seq_num(self) -> int:
        return self._committed_seq

    def writable_view(self) -> 'WritableMultiDimView[T]':
        """Get a writable view for publishing."""
        return WritableMultiDimView(self)

    def read_view(self) -> MultiDimView[T]:
        """Get a read-only view of committed data."""
        return MultiDimView(
            self._buffer, self._header.extents, self._header.strides,
            offset=0, writable=False,
        )

    def commit(self):
        """Release fence — makes writes visible to readers."""
        with self._lock:
            self._write_seq += 1
            self._committed_seq = self._write_seq
            self._header.seq_num = self._committed_seq
            self._header.committed = True

    def is_valid(self) -> bool:
        return self._header.magic == HEADER_MAGIC and self._header.rank > 0

    def release(self):
        """Release the channel resources."""
        self._buffer.clear()
        self._header = ChannelHeader()


class WritableMultiDimView(Generic[T]):
    """Writable view with commit semantics."""

    __slots__ = ('_channel', '_view')

    def __init__(self, channel: MultiDimChannel[T]):
        self._channel = channel
        self._view = MultiDimView(
            channel._buffer, channel._header.extents,
            channel._header.strides, offset=0, writable=True,
        )

    def __getitem__(self, indices) -> T:
        return self._view[indices]

    def __setitem__(self, indices, value: T):
        self._view[indices] = value

    def slice(self, dim: int, index: int) -> MultiDimView[T]:
        return self._view.slice(dim, index)

    def subview(self, dim: int, start: int, count: int) -> MultiDimView[T]:
        return self._view.subview(dim, start, count)

    @property
    def total_elements(self) -> int:
        return self._view.total_elements

    @property
    def data(self) -> list:
        return self._view.data

    def commit(self):
        """Release fence — makes writes visible to readers."""
        self._channel.commit()
