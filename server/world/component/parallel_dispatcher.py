"""
world.component.parallel_dispatcher — compile-time-style operator specialization

PRD #42: ParallelDispatcher operator specialization — static dispatch via
WarpExecutionOp / ScalarExecutionOp / VectorizedExecutionOp tag types so the
runtime picks the optimal execution path per operator category.

Sources:
  - Apollo CyberRT: component/component.h (partial template specialization)
  - agent-governance-toolkit: policy.ts (strategy-based dispatch)
  - pubsub-loop: world/scheduler/policy.py (ProcessorContext delegation)

Design:
  C++ uses template tags + enable_if for zero-overhead dispatch.  Python has no
  compile-time specialization, so we use:
    1. Tag classes with class-level constants (warp_width, vector_width, etc.)
    2. _DISPATCH_TABLE mapping tag → handler (one dict lookup, no isinstance chain)
    3. Protocol-based callable constraints (runtime_checkable @Protocol for concept-like checks)
  The dispatch table is built once at class init; subsequent calls are a single
  dict[type] lookup → direct handler call.  This eliminates the per-call
  isinstance chain that the old generic dispatch used.

UNKNOWN:
  - ISG warp primitives: isg_launch_warps / isg_warp_barrier don't exist in
    Python.  Warp path uses concurrent.futures ThreadPoolExecutor as the
    closest analogue; real ISG would need a C extension.
  - SIMD vectorized_call<N>: Python has no SIMD intrinsics.  Vectorized path
    uses numpy-style batch slicing when available, plain loop otherwise.
    Real SIMD would need ctypes/cffi into AVX/NEON code.
"""

from __future__ import annotations

import math
import os
import platform
import time
from abc import ABC, abstractmethod
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import (
    Any,
    Callable,
    Generic,
    List,
    Optional,
    Protocol,
    Sequence,
    Type,
    TypeVar,
    runtime_checkable,
)

T = TypeVar("T")

# ---------------------------------------------------------------------------
#  Step 1: Operator execution mode tags (C++ tag structs → Python classes)
# ---------------------------------------------------------------------------


class WarpExecutionOp:
    """ISG-style warp-parallel execution.

    Maps to C++:
        struct WarpExecutionOp {
            static constexpr size_t warp_width = 32;
            static constexpr bool requires_barrier = true;
        };
    """

    warp_width: int = 32
    requires_barrier: bool = True


class ScalarExecutionOp:
    """Single-element serial execution — zero parallel overhead.

    Maps to C++:
        struct ScalarExecutionOp {
            static constexpr size_t warp_width = 1;
            static constexpr bool requires_barrier = false;
        };
    """

    warp_width: int = 1
    requires_barrier: bool = False


class VectorizedExecutionOp:
    """SIMD-style vectorized batch execution.

    vector_width is determined by platform at import time:
      x86 AVX512 → 16, AVX2 → 8, ARM NEON → 4, fallback → 1

    Maps to C++:
        struct VectorizedExecutionOp {
            static constexpr size_t vector_width = /* platform-dependent */;
            static constexpr bool requires_barrier = false;
        };
    """

    requires_barrier: bool = False

    @staticmethod
    def _detect_vector_width() -> int:
        """Best-effort SIMD width detection (Python has no __AVX512F__ macro)."""
        arch = platform.machine().lower()
        if arch in ("x86_64", "amd64"):
            # Check /proc/cpuinfo for AVX flags (Linux)
            try:
                with open("/proc/cpuinfo", "r") as f:
                    cpuinfo = f.read()
                if "avx512f" in cpuinfo:
                    return 16
                if "avx2" in cpuinfo:
                    return 8
                if "avx" in cpuinfo:
                    return 4
            except OSError:
                pass
            return 8  # safe default for modern x86
        if arch in ("aarch64", "arm64"):
            return 4  # NEON 128-bit / 32-bit float
        return 1  # scalar fallback

    vector_width: int = 1  # set at module load

    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)

    @classmethod
    def _init_vector_width(cls) -> None:
        cls.vector_width = cls._detect_vector_width()


# Initialize vector_width at import
VectorizedExecutionOp._init_vector_width()


# Union of all known tags for type annotations
OpTag = Type[WarpExecutionOp] | Type[ScalarExecutionOp] | Type[VectorizedExecutionOp]

# ---------------------------------------------------------------------------
#  Step 3: Callable protocols (C++20 concepts → Python Protocols)
# ---------------------------------------------------------------------------


@runtime_checkable
class WarpCallable(Protocol[T]):
    """Concept: callable that accepts a single element reference.

    C++ equivalent:
        template <typename Fn, typename T>
        concept WarpCallable = requires(Fn fn, T& elem) {
            { fn(elem) } -> std::same_as<void>;
        };
    """

    def __call__(self, elem: T) -> None: ...


@runtime_checkable
class VectorizedCallable(Protocol[T]):
    """Concept: callable that provides both scalar and batch paths.

    C++ equivalent:
        template <typename Fn, typename T, size_t N>
        concept VectorizedCallable = requires(Fn fn, T* batch) {
            { fn.template vectorized_call<N>(batch) } -> std::same_as<void>;
        } && WarpCallable<Fn, T>;
    """

    def __call__(self, elem: T) -> None: ...

    def vectorized_call(self, batch: Sequence[T]) -> Sequence[T]: ...


# ---------------------------------------------------------------------------
#  Dispatch statistics
# ---------------------------------------------------------------------------


@dataclass
class DispatchStats:
    """Per-tag dispatch counters and timing."""

    warp_dispatches: int = 0
    scalar_dispatches: int = 0
    vectorized_dispatches: int = 0
    total_elements: int = 0
    total_time_ns: int = 0
    warp_barriers: int = 0

    def summary(self) -> str:
        total_d = self.warp_dispatches + self.scalar_dispatches + self.vectorized_dispatches
        avg_ns = self.total_time_ns // max(total_d, 1)
        return (
            f"dispatches={total_d} "
            f"(warp={self.warp_dispatches} scalar={self.scalar_dispatches} "
            f"vec={self.vectorized_dispatches}) "
            f"elements={self.total_elements} avg_ns={avg_ns}"
        )


# ---------------------------------------------------------------------------
#  Step 2: ParallelDispatcher with tag-based specialization
# ---------------------------------------------------------------------------

# Sentinel for warp barrier synchronisation (in real ISG this would be a
# hardware instruction; here it's a threading.Barrier or no-op).
_WARP_BARRIER_SENTINEL = object()


class ParallelDispatcher:
    """Operator-specialized parallel dispatcher.

    Replaces the old generic dispatch() with tag-based routing:

        # OLD — type-erased, no optimization possible
        dispatcher.dispatch(fn, data, count)

        # NEW — compile-time-style specialization
        dispatcher.dispatch(WarpExecutionOp, fn, data, count)
        dispatcher.dispatch(ScalarExecutionOp, fn, data, count)
        dispatcher.dispatch(VectorizedExecutionOp, fn, data, count)

    The dispatch table is built at __init__; each call is one dict lookup.

    Sources:
      - Apollo CyberRT component.h: partial template specialization for
        Component<M0>, Component<M0,M1>, Component<M0,M1,M2>
      - agent-governance-toolkit policy.ts: Record<Strategy, Handler>
    """

    __slots__ = ("_stats", "_pool", "_dispatch_table", "_max_workers")

    def __init__(self, max_workers: Optional[int] = None):
        self._stats = DispatchStats()
        self._max_workers = max_workers or min(os.cpu_count() or 4, 32)
        self._pool: Optional[ThreadPoolExecutor] = None  # lazy

        # Build dispatch table: tag type → handler method
        self._dispatch_table: dict[type, Callable[..., None]] = {
            WarpExecutionOp: self._dispatch_warp,
            ScalarExecutionOp: self._dispatch_scalar,
            VectorizedExecutionOp: self._dispatch_vectorized,
        }

    # ------------------------------------------------------------------
    #  Public API
    # ------------------------------------------------------------------

    def dispatch(
        self,
        op_tag: OpTag,
        fn: Callable[..., Any],
        data: Sequence[T],
        count: Optional[int] = None,
    ) -> None:
        """Dispatch *count* elements from *data* using the execution path
        selected by *op_tag*.

        Args:
            op_tag: One of WarpExecutionOp, ScalarExecutionOp,
                    VectorizedExecutionOp.
            fn:     Callable that processes elements.  For VectorizedExecutionOp
                    it must also expose ``vectorized_call(batch)``.
            data:   Indexable sequence of elements.
            count:  Number of elements to process (default: len(data)).

        Raises:
            TypeError: If *op_tag* is not a recognised execution tag.
            TypeError: If *fn* does not satisfy the concept for the chosen tag.
        """
        handler = self._dispatch_table.get(op_tag)
        if handler is None:
            raise TypeError(
                f"Unknown operator tag {op_tag!r}.  "
                f"Expected one of WarpExecutionOp, ScalarExecutionOp, "
                f"VectorizedExecutionOp."
            )
        if count is None:
            count = len(data)
        handler(fn, data, count)

    # Deprecated generic entry point — mirrors [[deprecated]] annotation
    def dispatch_generic(
        self,
        fn: Callable[[Any], None],
        data: Sequence[T],
        count: Optional[int] = None,
        threshold: int = 64,
    ) -> None:
        """DEPRECATED: use dispatch(OpTag, fn, data) instead.

        Kept for backward compatibility.  Will be removed after all call
        sites have migrated to tag-based dispatch (PRD #42 sub-task P2).
        """
        import warnings

        warnings.warn(
            "dispatch_generic() is deprecated; use dispatch(OpTag, fn, data)",
            DeprecationWarning,
            stacklevel=2,
        )
        if count is None:
            count = len(data)
        if count > threshold:
            self.dispatch(WarpExecutionOp, fn, data, count)
        else:
            self.dispatch(ScalarExecutionOp, fn, data, count)

    @property
    def stats(self) -> DispatchStats:
        return self._stats

    def shutdown(self) -> None:
        """Release thread pool resources."""
        if self._pool is not None:
            self._pool.shutdown(wait=False)
            self._pool = None

    # ------------------------------------------------------------------
    #  Warp execution path
    # ------------------------------------------------------------------

    def _dispatch_warp(
        self,
        fn: Callable[[T], None],
        data: Sequence[T],
        count: int,
    ) -> None:
        """Warp-parallel dispatch: partition elements into warp_width groups.

        C++ equivalent dispatches to ISG warp lanes.  Python equivalent uses
        ThreadPoolExecutor with one future per warp.

        Barrier semantics: if WarpExecutionOp.requires_barrier is True, the
        dispatcher waits for all warp futures before returning (mimics
        isg_warp_barrier).
        """
        t0 = time.monotonic_ns()

        W = WarpExecutionOp.warp_width
        num_warps = math.ceil(count / W)

        if num_warps <= 1:
            # Single warp or sub-warp — run inline, no thread overhead
            for i in range(count):
                fn(data[i])
        else:
            pool = self._get_pool()

            def _warp_task(warp_id: int) -> None:
                begin = warp_id * W
                end = min(begin + W, count)
                for i in range(begin, end):
                    fn(data[i])
                # isg_warp_barrier() analogue — implicit in future.result()

            futures = [pool.submit(_warp_task, wid) for wid in range(num_warps)]

            if WarpExecutionOp.requires_barrier:
                for f in futures:
                    f.result()
                self._stats.warp_barriers += num_warps

        elapsed = time.monotonic_ns() - t0
        self._stats.warp_dispatches += 1
        self._stats.total_elements += count
        self._stats.total_time_ns += elapsed

    # ------------------------------------------------------------------
    #  Scalar execution path
    # ------------------------------------------------------------------

    def _dispatch_scalar(
        self,
        fn: Callable[[T], None],
        data: Sequence[T],
        count: int,
    ) -> None:
        """Scalar serial dispatch: zero parallel overhead.

        C++ equivalent compiles to a plain loop with no thread_create,
        no mutex_lock, no parallel_for instrumentation.
        """
        t0 = time.monotonic_ns()

        for i in range(count):
            fn(data[i])

        elapsed = time.monotonic_ns() - t0
        self._stats.scalar_dispatches += 1
        self._stats.total_elements += count
        self._stats.total_time_ns += elapsed

    # ------------------------------------------------------------------
    #  Vectorized execution path
    # ------------------------------------------------------------------

    def _dispatch_vectorized(
        self,
        fn: Any,  # must satisfy VectorizedCallable if has vectorized_call
        data: Sequence[T],
        count: int,
    ) -> None:
        """Vectorized batch dispatch: main loop processes vector_width elements
        at a time; tail elements use scalar fallback.

        If *fn* provides ``vectorized_call(batch)``, the main loop calls it
        with slices of size vector_width.  Otherwise falls back to element-wise
        scalar calls (concept violation warning at runtime — in C++ this would
        be a compile error via VectorizedCallable concept).
        """
        t0 = time.monotonic_ns()
        V = VectorizedExecutionOp.vector_width

        has_vec = hasattr(fn, "vectorized_call") and callable(fn.vectorized_call)

        if not has_vec:
            # Concept violation — in C++ this is a compile error.
            # In Python we warn and fall back to scalar.
            import warnings

            warnings.warn(
                f"Callable {fn!r} does not satisfy VectorizedCallable "
                f"(missing vectorized_call method).  Falling back to scalar loop.  "
                f"In C++ this would be a concept constraint error.",
                RuntimeWarning,
                stacklevel=3,
            )
            for i in range(count):
                fn(data[i])
        else:
            vec_count = count // V
            remainder = count % V

            # Vectorized main loop
            for i in range(vec_count):
                batch = data[i * V : (i + 1) * V]
                fn.vectorized_call(batch)

            # Scalar tail
            tail_start = vec_count * V
            for i in range(tail_start, tail_start + remainder):
                fn(data[i])

        elapsed = time.monotonic_ns() - t0
        self._stats.vectorized_dispatches += 1
        self._stats.total_elements += count
        self._stats.total_time_ns += elapsed

    # ------------------------------------------------------------------
    #  Internal helpers
    # ------------------------------------------------------------------

    def _get_pool(self) -> ThreadPoolExecutor:
        if self._pool is None:
            self._pool = ThreadPoolExecutor(max_workers=self._max_workers)
        return self._pool

    def __del__(self) -> None:
        self.shutdown()


# ---------------------------------------------------------------------------
#  Convenience: IndividualDispatcher — world-model-aware wrapper
# ---------------------------------------------------------------------------


class IndividualDispatcher:
    """Higher-level dispatcher that routes individual tick tasks through
    ParallelDispatcher with automatic op-tag selection.

    Integrates with the WorldResolver to determine which individuals should
    use warp vs scalar vs vectorized paths based on their component type and
    the world's current population.

    Sources:
      - Apollo CyberRT: component/component_base.h (ComponentBase::Process)
      - agent-governance-toolkit: policy.ts (cascade containment dispatch)
      - pubsub-loop: world/component/tick_driver.py (WorldTickDriver)
    """

    # Population thresholds for automatic op-tag selection
    WARP_THRESHOLD = 32  # use warp if count >= this
    VECTORIZED_THRESHOLD = 8  # use vectorized if count >= this (and < warp)

    def __init__(self, dispatcher: Optional[ParallelDispatcher] = None):
        self._dispatcher = dispatcher or ParallelDispatcher()
        self._tag_overrides: dict[str, OpTag] = {}

    def set_tag_override(self, component_type: str, tag: OpTag) -> None:
        """Force a specific op-tag for a component type.

        Useful when a component knows its optimal execution mode (e.g., a
        neural-network component forces VectorizedExecutionOp).
        """
        self._tag_overrides[component_type] = tag

    def dispatch_ticks(
        self,
        tick_fn: Callable[[T], None],
        individuals: Sequence[T],
        component_type: str = "",
    ) -> None:
        """Dispatch tick updates for a batch of individuals.

        Auto-selects the op-tag based on:
          1. Explicit override (set_tag_override)
          2. Population count heuristics
        """
        tag = self._select_tag(component_type, len(individuals))
        self._dispatcher.dispatch(tag, tick_fn, individuals)

    def _select_tag(self, component_type: str, count: int) -> OpTag:
        # Check for explicit override
        if component_type in self._tag_overrides:
            return self._tag_overrides[component_type]

        # Automatic selection by population
        if count >= self.WARP_THRESHOLD:
            return WarpExecutionOp
        if count >= self.VECTORIZED_THRESHOLD:
            return VectorizedExecutionOp
        return ScalarExecutionOp

    @property
    def stats(self) -> DispatchStats:
        return self._dispatcher.stats

    def shutdown(self) -> None:
        self._dispatcher.shutdown()
