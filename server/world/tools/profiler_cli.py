"""
world.tools.profiler_cli — benchmark symbol discovery and profiler CLI
=======================================================================

PRD #34: Add profiler CLI subcommand to enumerate and filter registered
benchmark symbol names in the current world session.

Problem: Operators must know exact benchmark names to profile. With 200+
registered benchmarks, running all wastes profiler budget (~12% overhead).
No runtime tool exists to discover which benchmarks are registered.

Solution: BenchmarkRegistry with query/filter API, and ProfilerCLI with
`list-benchmarks` subcommand supporting glob/regex filtering.

Sources:
  - CyberRT: tools/cyber_monitor, tools/cyber_recorder
  - Google Benchmark: --benchmark_list_tests
"""

from __future__ import annotations

import fnmatch
import re
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Pattern, Set, Tuple


class BenchmarkCategory(Enum):
    """Benchmark categories for filtering."""
    TRANSPORT = 'transport'
    SCHEDULER = 'scheduler'
    DATA = 'data'
    NODE = 'node'
    COMPONENT = 'component'
    FUSION = 'fusion'
    MESSAGE = 'message'
    IO = 'io'
    CUSTOM = 'custom'


@dataclass
class BenchmarkSymbol:
    """A registered benchmark symbol with metadata."""
    name: str                                    # mangled or demangled name
    demangled: str = ''                          # human-readable name
    module: str = ''                             # owning module
    category: BenchmarkCategory = BenchmarkCategory.CUSTOM
    description: str = ''
    registered_at_ns: int = 0                    # registration timestamp
    tags: Set[str] = field(default_factory=set)

    def matches_glob(self, pattern: str) -> bool:
        return (fnmatch.fnmatch(self.name, pattern) or
                fnmatch.fnmatch(self.demangled, pattern) or
                fnmatch.fnmatch(self.module, pattern))

    def matches_regex(self, compiled: Pattern) -> bool:
        return bool(compiled.search(self.name) or
                    compiled.search(self.demangled) or
                    compiled.search(self.module))


class BenchmarkRegistry:
    """Registry of benchmark symbols for runtime discovery.

    Equivalent to WORLD_REGISTER_BENCHMARK() macro collecting symbols.
    """

    _instance: Optional['BenchmarkRegistry'] = None

    @classmethod
    def instance(cls) -> 'BenchmarkRegistry':
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._benchmarks: Dict[str, BenchmarkSymbol] = {}

    def register(
        self,
        name: str,
        module: str = '',
        category: BenchmarkCategory = BenchmarkCategory.CUSTOM,
        description: str = '',
        tags: Optional[Set[str]] = None,
    ) -> BenchmarkSymbol:
        """Register a benchmark symbol."""
        sym = BenchmarkSymbol(
            name=name,
            demangled=_demangle(name),
            module=module,
            category=category,
            description=description,
            registered_at_ns=time.monotonic_ns(),
            tags=tags or set(),
        )
        self._benchmarks[name] = sym
        return sym

    def unregister(self, name: str) -> bool:
        return self._benchmarks.pop(name, None) is not None

    def list_all(self) -> List[BenchmarkSymbol]:
        return list(self._benchmarks.values())

    def filter_glob(self, pattern: str) -> List[BenchmarkSymbol]:
        """Filter benchmarks by glob pattern."""
        return [s for s in self._benchmarks.values() if s.matches_glob(pattern)]

    def filter_regex(self, pattern: str) -> List[BenchmarkSymbol]:
        """Filter benchmarks by regex pattern."""
        compiled = re.compile(pattern, re.IGNORECASE)
        return [s for s in self._benchmarks.values() if s.matches_regex(compiled)]

    def filter_category(self, category: BenchmarkCategory) -> List[BenchmarkSymbol]:
        return [s for s in self._benchmarks.values() if s.category == category]

    def filter_module(self, module: str) -> List[BenchmarkSymbol]:
        return [s for s in self._benchmarks.values() if s.module == module]

    def filter_tags(self, tags: Set[str]) -> List[BenchmarkSymbol]:
        return [s for s in self._benchmarks.values() if tags & s.tags]

    def count(self) -> int:
        return len(self._benchmarks)

    def get(self, name: str) -> Optional[BenchmarkSymbol]:
        return self._benchmarks.get(name)

    def clear(self):
        self._benchmarks.clear()


def _demangle(name: str) -> str:
    """Portable C++ symbol demangling (Python equivalent).

    In Python, names are already readable.  This handles
    mangled C++ names that come through FFI.
    """
    # Strip common C++ mangling prefixes
    if name.startswith('_Z'):
        # Basic demangling: remove _Z prefix and length-prefixed segments
        # Full demangling would use cxxfilt, but we do best-effort
        try:
            import subprocess
            result = subprocess.run(
                ['c++filt', name], capture_output=True, text=True, timeout=1
            )
            if result.returncode == 0:
                return result.stdout.strip()
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass
    return name


class ProfilerCLI:
    """Profiler CLI interface for benchmark discovery.

    Usage::

        cli = ProfilerCLI()
        cli.list_benchmarks()                      # all benchmarks
        cli.list_benchmarks(glob='transport*')     # glob filter
        cli.list_benchmarks(regex='.*fusion.*')    # regex filter
        cli.list_benchmarks(category='data')       # category filter
    """

    def __init__(self, registry: Optional[BenchmarkRegistry] = None):
        self._registry = registry or BenchmarkRegistry.instance()

    def list_benchmarks(
        self,
        glob: Optional[str] = None,
        regex: Optional[str] = None,
        category: Optional[str] = None,
        module: Optional[str] = None,
        tags: Optional[Set[str]] = None,
    ) -> List[BenchmarkSymbol]:
        """List and filter registered benchmarks."""
        if glob:
            results = self._registry.filter_glob(glob)
        elif regex:
            results = self._registry.filter_regex(regex)
        elif category:
            cat = BenchmarkCategory(category)
            results = self._registry.filter_category(cat)
        elif module:
            results = self._registry.filter_module(module)
        elif tags:
            results = self._registry.filter_tags(tags)
        else:
            results = self._registry.list_all()
        return results

    def format_table(self, benchmarks: List[BenchmarkSymbol]) -> str:
        """Format benchmarks as a human-readable table."""
        if not benchmarks:
            return "No benchmarks found."

        lines = []
        lines.append(f"{'Name':<40} {'Module':<15} {'Category':<12} {'Tags'}")
        lines.append('-' * 80)
        for b in benchmarks:
            tags_str = ', '.join(sorted(b.tags)) if b.tags else ''
            name = b.demangled or b.name
            lines.append(f"{name:<40} {b.module:<15} {b.category.value:<12} {tags_str}")
        lines.append(f"\nTotal: {len(benchmarks)} benchmarks")
        return '\n'.join(lines)

    def run_selected(
        self,
        names: List[str],
        callback: Optional[Callable[[BenchmarkSymbol], None]] = None,
    ) -> Dict[str, float]:
        """Run selected benchmarks (stub — actual execution in profiler runtime)."""
        results = {}
        for name in names:
            sym = self._registry.get(name)
            if sym and callback:
                callback(sym)
            results[name] = 0.0  # placeholder for actual timing
        return results
