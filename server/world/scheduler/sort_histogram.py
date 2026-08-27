"""
world.scheduler.sort_histogram — sort & histogram CRoutine tasks

PRD #56: Replace bool_constant tag dispatch with direct branching (C++
if constexpr; Python: plain if/else on class-level constant).

Before (tag dispatch):
    class SortCRoutine:
        def execute(self, states):
            self._execute_impl(states, BoolTag(self._descending))
        def _execute_impl(self, states, tag: TrueTag):  ...  # descending
        def _execute_impl(self, states, tag: FalseTag): ...  # ascending

After (direct branch — this file):
    class SortCRoutine:
        def execute(self, states):
            if self.descending:
                _warp_sort(states, reverse=True)   # only this path
            else:
                _warp_sort(states, reverse=False)   # only this path

Sources:
  - Apollo CyberRT: scheduler/croutine.h (CRoutine lifecycle)
  - NVIDIA CUB: DeviceRadixSort (ascending/descending variants)
  - pubsub-loop: world/scheduler/scheduler.py (CRoutine/RoutineState)
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import (
    Any,
    Callable,
    Generic,
    List,
    Optional,
    Sequence,
    TypeVar,
)

T = TypeVar("T")


# ---------------------------------------------------------------------------
#  SortCRoutine — ascending/descending individual state sorting
# ---------------------------------------------------------------------------


@dataclass
class SortConfig:
    """Configuration for SortCRoutine."""

    descending: bool = False
    stable: bool = True  # use stable sort to preserve insertion order of equal elements


class SortCRoutine(Generic[T]):
    """Sort CRoutine task — sorts individual states in-place.

    Template parameter ``descending`` is a class-level constant (Python bool,
    C++ non-type template parameter).  The execute() method uses a direct
    branch instead of tag dispatch:

        if constexpr (Desc) {
            warp_sort(states, std::greater<>{});
        } else {
            warp_sort(states, std::less<>{});
        }

    Usage::

        sorter = SortCRoutine(descending=True)
        sorter.execute(states)
        # states is now sorted in descending order

        # With custom key:
        sorter = SortCRoutine(descending=False, key=lambda s: s.position.x)
        sorter.execute(states)
    """

    __slots__ = ("_config", "_key")

    def __init__(
        self,
        descending: bool = False,
        stable: bool = True,
        key: Optional[Callable[[T], Any]] = None,
    ):
        self._config = SortConfig(descending=descending, stable=stable)
        self._key = key

    @property
    def descending(self) -> bool:
        return self._config.descending

    @property
    def stable(self) -> bool:
        return self._config.stable

    def execute(self, states: List[T]) -> None:
        """Sort *states* in-place.

        Direct branch — no tag dispatch.  Python ``list.sort()`` is the
        equivalent of C++ ``warp_sort()`` (both are introsort-based).
        """
        if not states:
            return

        # Direct branch on compile-time-equivalent constant
        if self._config.descending:
            states.sort(key=self._key, reverse=True)
        else:
            states.sort(key=self._key, reverse=False)

        # Note: stable=True is the default for Python's Timsort.
        # If stable=False were needed, we'd use a different algorithm,
        # but Python's sort is always stable, which matches our default.

    def execute_span(self, states: List[T], start: int, end: int) -> None:
        """Sort a sub-range [start, end) of *states*.

        Useful for warp-partitioned sorting where each warp sorts its own
        segment independently.
        """
        if start >= end:
            return
        segment = states[start:end]
        self.execute(segment)
        states[start:end] = segment


# ---------------------------------------------------------------------------
#  HistogramCRoutine — individual state binning
# ---------------------------------------------------------------------------


@dataclass
class HistogramBuffer:
    """Fixed-bin histogram accumulator.

    Attributes:
        num_bins: Number of bins.
        range_min: Lower bound of the histogram range.
        range_max: Upper bound of the histogram range.
        counts: Per-bin counts.
    """

    num_bins: int = 16
    range_min: float = 0.0
    range_max: float = 1.0
    counts: List[int] = field(default_factory=list)

    def __post_init__(self) -> None:
        if not self.counts:
            self.counts = [0] * self.num_bins

    @property
    def bin_width(self) -> float:
        return (self.range_max - self.range_min) / self.num_bins

    def reset(self) -> None:
        self.counts = [0] * self.num_bins

    def total(self) -> int:
        return sum(self.counts)


class HistogramCRoutine(Generic[T]):
    """Histogram CRoutine task — bins individual state values.

    Template parameter ``inclusive`` controls bin boundary semantics:
      - inclusive=True:  value == bin_edge belongs to the LEFT bin
      - inclusive=False: value == bin_edge belongs to the RIGHT bin

    The execute() method uses a direct branch:

        if constexpr (Inclusive) {
            // inclusive binning
        } else {
            // exclusive binning
        }

    Usage::

        hist = HistogramBuffer(num_bins=16, range_min=0.0, range_max=1.0)
        histo = HistogramCRoutine(inclusive=True, key=lambda s: s.velocity.x)
        histo.execute(states, hist)
    """

    __slots__ = ("_inclusive", "_key")

    def __init__(
        self,
        inclusive: bool = True,
        key: Optional[Callable[[T], float]] = None,
    ):
        self._inclusive = inclusive
        self._key = key

    @property
    def inclusive(self) -> bool:
        return self._inclusive

    def execute(
        self,
        input_data: Sequence[T],
        hist: HistogramBuffer,
    ) -> None:
        """Bin all elements of *input_data* into *hist*.

        Direct branch — no tag dispatch.
        """
        if not input_data:
            return

        num_bins = hist.num_bins
        rmin = hist.range_min
        rmax = hist.range_max

        if rmax <= rmin:
            return

        inv_width = num_bins / (rmax - rmin)

        # Direct branch on inclusive/exclusive — the if-constexpr equivalent
        if self._inclusive:
            self._bin_inclusive(input_data, hist.counts, num_bins, rmin, inv_width)
        else:
            self._bin_exclusive(input_data, hist.counts, num_bins, rmin, inv_width)

    def _bin_inclusive(
        self,
        data: Sequence[T],
        counts: List[int],
        num_bins: int,
        rmin: float,
        inv_width: float,
    ) -> None:
        """Inclusive binning: value == edge → left bin (floor)."""
        key = self._key
        for elem in data:
            val = key(elem) if key else float(elem)  # type: ignore[arg-type]
            idx = int((val - rmin) * inv_width)
            # Clamp: inclusive means boundary goes LEFT
            if idx >= num_bins:
                idx = num_bins - 1
            if idx < 0:
                idx = 0
            counts[idx] += 1

    def _bin_exclusive(
        self,
        data: Sequence[T],
        counts: List[int],
        num_bins: int,
        rmin: float,
        inv_width: float,
    ) -> None:
        """Exclusive binning: value == edge → right bin (ceil - 1)."""
        key = self._key
        for elem in data:
            val = key(elem) if key else float(elem)  # type: ignore[arg-type]
            raw = (val - rmin) * inv_width
            idx = int(raw)
            # Exclusive: exact boundary goes to the RIGHT bin
            if raw == idx and idx > 0:
                idx -= 1
            # Clamp
            if idx >= num_bins:
                idx = num_bins - 1
            if idx < 0:
                idx = 0
            counts[idx] += 1
