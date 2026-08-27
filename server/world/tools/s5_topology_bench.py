"""
world.tools.s5_topology_bench — chained individual state scan benchmark

PRD #102: s5-topology benchmark for pub/sub pipeline latency and throughput.
5-hop chain: position_scan → velocity_scan → attribute_scan → aggregate → emit.
Each hop has an independent LatencyHistogram reporting p50/p95/p99.

Sources:
  - Apollo CyberRT: tools/cyber_monitor (performance monitoring)
  - pubsub-loop: world/tools/profiler_cli.py (BenchmarkRegistry)
  - Google Benchmark: --benchmark_filter API

UNKNOWN:
  - Actual ISG/GPU scan throughput requires native compilation.  Python
    benchmark measures interpreter overhead; real numbers would come from
    the C++ build targeting Ampere A100.
  - The 5M individuals/sec target and < 200ns/hop p99 are C++ targets.
    Python benchmark establishes the *relative* baseline for regression
    detection (±15% CI gate).
"""

from __future__ import annotations

import math
import random
import statistics
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple


# ---------------------------------------------------------------------------
#  LatencyHistogram — per-hop nanosecond latency accumulator
# ---------------------------------------------------------------------------


@dataclass
class LatencyStats:
    """Computed percentile statistics from a LatencyHistogram."""

    count: int = 0
    min_ns: int = 0
    max_ns: int = 0
    mean_ns: float = 0.0
    p50_ns: int = 0
    p95_ns: int = 0
    p99_ns: int = 0

    def summary(self) -> str:
        return (
            f"n={self.count} min={self.min_ns}ns "
            f"p50={self.p50_ns}ns p95={self.p95_ns}ns p99={self.p99_ns}ns "
            f"max={self.max_ns}ns mean={self.mean_ns:.0f}ns"
        )


class LatencyHistogram:
    """Nanosecond-resolution latency recorder with percentile computation.

    CyberRT equivalent:
        class LatencyHistogram {
            void record(Duration d);
            LatencyStats compute_percentiles() const;
        };
    """

    __slots__ = ("_samples", "_name")

    def __init__(self, name: str = ""):
        self._name = name
        self._samples: List[int] = []

    @property
    def name(self) -> str:
        return self._name

    def record(self, duration_ns: int) -> None:
        self._samples.append(duration_ns)

    @property
    def count(self) -> int:
        return len(self._samples)

    def compute_percentiles(self) -> LatencyStats:
        if not self._samples:
            return LatencyStats()
        sorted_s = sorted(self._samples)
        n = len(sorted_s)

        def percentile(pct: float) -> int:
            idx = int(math.ceil(pct / 100.0 * n)) - 1
            return sorted_s[max(idx, 0)]

        return LatencyStats(
            count=n,
            min_ns=sorted_s[0],
            max_ns=sorted_s[-1],
            mean_ns=statistics.mean(sorted_s),
            p50_ns=percentile(50),
            p95_ns=percentile(95),
            p99_ns=percentile(99),
        )

    def reset(self) -> None:
        self._samples.clear()


# ---------------------------------------------------------------------------
#  ScopedTimer — RAII-style timer (context manager in Python)
# ---------------------------------------------------------------------------


class ScopedTimer:
    """Context manager that records elapsed nanoseconds into a LatencyHistogram.

    CyberRT equivalent:
        class ScopedTimer {
            explicit ScopedTimer(LatencyHistogram& histogram);
            ~ScopedTimer();  // destructor records duration
        };

    Usage::

        hist = LatencyHistogram("hop1")
        with ScopedTimer(hist):
            scan_positions(states)
    """

    __slots__ = ("_histogram", "_start")

    def __init__(self, histogram: LatencyHistogram):
        self._histogram = histogram
        self._start: int = 0

    def __enter__(self) -> "ScopedTimer":
        self._start = time.monotonic_ns()
        return self

    def __exit__(self, *exc: Any) -> None:
        elapsed = time.monotonic_ns() - self._start
        self._histogram.record(elapsed)


# ---------------------------------------------------------------------------
#  Synthetic IndividualScanState (benchmark data)
# ---------------------------------------------------------------------------


@dataclass
class Vec3d:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0


@dataclass
class IndividualScanState:
    """Synthetic individual state for benchmark scans."""

    id: int = 0
    position: Vec3d = field(default_factory=Vec3d)
    velocity: Vec3d = field(default_factory=Vec3d)
    attributes: Dict[str, float] = field(default_factory=dict)


def _generate_states(n: int) -> List[IndividualScanState]:
    """Generate n synthetic individual states for benchmarking."""
    states: List[IndividualScanState] = []
    for i in range(n):
        states.append(
            IndividualScanState(
                id=i,
                position=Vec3d(
                    random.uniform(-1000, 1000),
                    random.uniform(-1000, 1000),
                    random.uniform(-100, 100),
                ),
                velocity=Vec3d(
                    random.uniform(-10, 10),
                    random.uniform(-10, 10),
                    random.uniform(-1, 1),
                ),
                attributes={"health": random.uniform(0, 100), "energy": random.uniform(0, 100)},
            )
        )
    return states


# ---------------------------------------------------------------------------
#  s5-topology scan hops
# ---------------------------------------------------------------------------


def _scan_positions(states: Sequence[IndividualScanState]) -> float:
    """Hop 1: scan all position vectors, return sum of x for checksum."""
    acc = 0.0
    for s in states:
        acc += s.position.x + s.position.y + s.position.z
    return acc


def _scan_velocities(states: Sequence[IndividualScanState]) -> float:
    """Hop 2: scan all velocity vectors."""
    acc = 0.0
    for s in states:
        acc += s.velocity.x + s.velocity.y + s.velocity.z
    return acc


def _scan_attributes(states: Sequence[IndividualScanState]) -> float:
    """Hop 3: scan attribute maps."""
    acc = 0.0
    for s in states:
        for v in s.attributes.values():
            acc += v
    return acc


def _aggregate(
    pos_sum: float, vel_sum: float, attr_sum: float, n: int
) -> Dict[str, float]:
    """Hop 4: aggregate statistics."""
    return {
        "avg_pos": pos_sum / max(n, 1),
        "avg_vel": vel_sum / max(n, 1),
        "avg_attr": attr_sum / max(n, 1),
        "count": float(n),
    }


def _emit(agg: Dict[str, float], histogram: LatencyHistogram) -> None:
    """Hop 5: emit results (simulates channel publish)."""
    # In production this would publish to /world/profiler/bench/s5_output
    _ = agg  # consumed


# ---------------------------------------------------------------------------
#  S5TopologyConfig + BenchmarkResult
# ---------------------------------------------------------------------------


@dataclass
class S5TopologyConfig:
    """Configuration for s5-topology benchmark."""

    num_individuals: int = 100_000
    num_scan_iterations: int = 500
    record_per_hop_latency: bool = True


@dataclass
class BenchmarkResult:
    """Result of an s5-topology benchmark run."""

    name: str = ""
    config: S5TopologyConfig = field(default_factory=S5TopologyConfig)
    hop_stats: List[LatencyStats] = field(default_factory=list)
    total_time_ns: int = 0
    total_individuals_scanned: int = 0

    @property
    def throughput_per_sec(self) -> float:
        """Individuals processed per second (across all hops)."""
        if self.total_time_ns == 0:
            return 0.0
        return self.total_individuals_scanned / (self.total_time_ns / 1e9)

    @property
    def total_p99_ns(self) -> int:
        """Sum of per-hop p99 latencies."""
        return sum(h.p99_ns for h in self.hop_stats)

    def summary(self) -> str:
        lines = [f"=== {self.name} ==="]
        lines.append(
            f"  individuals={self.config.num_individuals} "
            f"iterations={self.config.num_scan_iterations}"
        )
        lines.append(
            f"  throughput={self.throughput_per_sec:,.0f} individuals/sec"
        )
        lines.append(f"  total_p99={self.total_p99_ns}ns")
        for i, hs in enumerate(self.hop_stats):
            hop_names = ["position_scan", "velocity_scan", "attribute_scan",
                         "aggregate", "emit"]
            name = hop_names[i] if i < len(hop_names) else f"hop{i}"
            lines.append(f"  {name}: {hs.summary()}")
        return "\n".join(lines)

    @classmethod
    def from_histograms(
        cls,
        name: str,
        config: S5TopologyConfig,
        histograms: List[LatencyHistogram],
        total_time_ns: int,
    ) -> "BenchmarkResult":
        return cls(
            name=name,
            config=config,
            hop_stats=[h.compute_percentiles() for h in histograms],
            total_time_ns=total_time_ns,
            total_individuals_scanned=config.num_individuals * config.num_scan_iterations,
        )


# ---------------------------------------------------------------------------
#  Benchmark runner
# ---------------------------------------------------------------------------


def run_s5_topology_benchmark(
    config: Optional[S5TopologyConfig] = None,
) -> BenchmarkResult:
    """Execute the s5-topology chained scan benchmark.

    Generates synthetic individual states, then runs *num_scan_iterations*
    passes of the 5-hop chain, recording per-hop latency in independent
    LatencyHistograms.
    """
    cfg = config or S5TopologyConfig()

    # Generate synthetic data once (not counted in benchmark time)
    states = _generate_states(cfg.num_individuals)

    # 5 independent histograms
    histograms = [LatencyHistogram(f"hop{i}") for i in range(5)]

    total_t0 = time.monotonic_ns()

    for _ in range(cfg.num_scan_iterations):
        with ScopedTimer(histograms[0]):
            pos_sum = _scan_positions(states)

        with ScopedTimer(histograms[1]):
            vel_sum = _scan_velocities(states)

        with ScopedTimer(histograms[2]):
            attr_sum = _scan_attributes(states)

        with ScopedTimer(histograms[3]):
            agg = _aggregate(pos_sum, vel_sum, attr_sum, len(states))

        with ScopedTimer(histograms[4]):
            _emit(agg, histograms[4])

    total_elapsed = time.monotonic_ns() - total_t0

    return BenchmarkResult.from_histograms(
        name=f"s5_topology/scan/{cfg.num_individuals // 1000}k",
        config=cfg,
        histograms=histograms,
        total_time_ns=total_elapsed,
    )


def register_s5_topology_benchmarks(registry: Any) -> None:
    """Register s5-topology benchmarks with a BenchmarkRegistry.

    Integrates with world.tools.profiler_cli.BenchmarkRegistry:

        from world.tools.profiler_cli import BenchmarkRegistry
        from world.tools.s5_topology_bench import register_s5_topology_benchmarks

        reg = BenchmarkRegistry.instance()
        register_s5_topology_benchmarks(reg)
    """
    # Import here to avoid circular dependency
    from world.tools.profiler_cli import BenchmarkCategory

    configs = [
        S5TopologyConfig(num_individuals=1_000, num_scan_iterations=1000),
        S5TopologyConfig(num_individuals=10_000, num_scan_iterations=500),
        S5TopologyConfig(num_individuals=100_000, num_scan_iterations=100),
    ]

    for cfg in configs:
        label = f"{cfg.num_individuals // 1000}k"
        name = f"s5_topology/scan/{label}"

        def _make_runner(c: S5TopologyConfig = cfg) -> Callable[[], BenchmarkResult]:
            def _run() -> BenchmarkResult:
                return run_s5_topology_benchmark(c)
            return _run

        registry.register(
            name=name,
            fn=_make_runner(),
            category=BenchmarkCategory.LATENCY,
            description=f"s5-topology chained scan: {label} individuals",
        )
