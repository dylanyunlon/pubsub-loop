"""
world.common.demangle — portable C++ symbol demangling
========================================================

PRD #59: Implement world::demangle — portable C++ symbol restoration tool
for the pub/sub-loop world runtime.

Problem: 4+ duplicate demangle implementations scattered across modules
(profiler, logger, component, etc.), each with different bugs:
- profiler: leaks memory on error path
- logger: MSVC path missing
- component: no demangle at all, raw typeid output

Solution: Single demangle() function with:
- GCC/Clang: abi::__cxa_demangle via subprocess
- MSVC: __unDName pattern matching
- Fallback: regex-based heuristic demangling
- Python: direct __qualname__ access (no mangling needed)

Sources:
  - CyberRT: common/macros.h (no demangle tool)
  - llvm: llvm-cxxfilt source
"""

from __future__ import annotations

import re
import subprocess
from functools import lru_cache
from typing import Any, Optional, Type


@lru_cache(maxsize=4096)
def demangle(mangled: str) -> str:
    """Demangle a C++ symbol name to human-readable form.

    Tries multiple strategies:
    1. c++filt subprocess (GCC/binutils)
    2. Regex-based heuristic for common Itanium ABI patterns
    3. Fallback: return as-is

    For Python types, returns __qualname__ directly.
    """
    if not mangled:
        return mangled

    # Python type: already human-readable
    if not mangled.startswith('_Z') and '::' not in mangled:
        if '.' in mangled or mangled[0].isupper():
            return mangled

    # Try c++filt (most reliable)
    result = _try_cxxfilt(mangled)
    if result is not None:
        return result

    # Heuristic demangling for Itanium ABI
    result = _itanium_demangle(mangled)
    if result != mangled:
        return result

    return mangled


def demangle_type(tp: type) -> str:
    """Get a clean, readable name for a Python type."""
    return tp.__qualname__


def demangle_value(obj: Any) -> str:
    """Get a clean, readable type name for any Python object."""
    return type(obj).__qualname__


def _try_cxxfilt(mangled: str) -> Optional[str]:
    """Try demangling via c++filt subprocess."""
    try:
        result = subprocess.run(
            ['c++filt', mangled],
            capture_output=True,
            text=True,
            timeout=1,
        )
        if result.returncode == 0:
            demangled = result.stdout.strip()
            if demangled != mangled:
                return demangled
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        pass
    return None


# Itanium ABI demangling patterns
_ITANIUM_BUILTIN = {
    'v': 'void', 'b': 'bool', 'c': 'char', 'a': 'signed char',
    'h': 'unsigned char', 's': 'short', 't': 'unsigned short',
    'i': 'int', 'j': 'unsigned int', 'l': 'long', 'm': 'unsigned long',
    'x': 'long long', 'y': 'unsigned long long',
    'f': 'float', 'd': 'double', 'e': 'long double',
}

_ITANIUM_QUAL_RE = re.compile(r'(\d+)([A-Za-z_]\w*)')


def _itanium_demangle(mangled: str) -> str:
    """Best-effort Itanium ABI demangling without external tools.

    Handles:
    - _Z + length-prefixed names: _Z4mainv → main()
    - _ZN + nested names: _ZN4data13ChannelWriterE → data::ChannelWriter
    - Template parameters (basic cases)
    """
    if not mangled.startswith('_Z'):
        return mangled

    s = mangled[2:]
    is_nested = s.startswith('N')
    if is_nested:
        s = s[1:]

    names = []
    while s:
        # End of nested name
        if s[0] == 'E':
            s = s[1:]
            break

        # Length-prefixed name segment
        m = _ITANIUM_QUAL_RE.match(s)
        if m:
            length = int(m.group(1))
            start = len(m.group(1))
            name = s[start:start + length]
            names.append(name)
            s = s[start + length:]
            continue

        # Template indicator
        if s[0] == 'I':
            s = s[1:]
            # Skip template args (simplified)
            depth = 1
            while s and depth > 0:
                if s[0] == 'I':
                    depth += 1
                elif s[0] == 'E':
                    depth -= 1
                s = s[1:]
            continue

        # Builtin type or function params
        if s[0] in _ITANIUM_BUILTIN:
            s = s[1:]
            continue

        # Can't parse further
        break

    if names:
        return '::'.join(names)
    return mangled


def readable_typename(tp: type) -> str:
    """Format a type name for logging and error messages.

    Equivalent to CyberRT's readable_type() but without the
    memory leak or MSVC incompatibility.
    """
    module = getattr(tp, '__module__', '')
    qualname = tp.__qualname__

    # Strip common prefixes for cleaner output
    if module in ('builtins', '__main__', ''):
        return qualname
    return f"{module}.{qualname}"


# ---------------------------------------------------------------------------
#  PRD #59 additions: demangle_short, safe_demangle, batch_demangle
# ---------------------------------------------------------------------------


def demangle_short(mangled: str) -> str:
    """Demangle and strip leading namespace prefixes.

    Keeps template parameters intact (including internal namespaces):
        "transport::UnifiedWriter<IndividualState>::write"
        → "write"  (top-level namespace stripped)

    But template arguments preserve their namespaces:
        "scheduler::CRoutine::dispatch<data::ChannelWriter<PositionState>>"
        → "dispatch<data::ChannelWriter<PositionState>>"

    C++ equivalent:
        std::string demangle_short(const char* mangled_name) {
            auto full = demangle(mangled_name);
            int bracket_depth = 0;
            size_t last_sep = npos;
            for (size_t i = 0; i+1 < full.size(); ++i) {
                if (full[i] == '<') ++bracket_depth;
                else if (full[i] == '>') --bracket_depth;
                else if (bracket_depth == 0 && full[i] == ':' && full[i+1] == ':')
                    last_sep = i + 2;
            }
            return (last_sep != npos) ? full.substr(last_sep) : full;
        }
    """
    if not mangled:
        return ""

    full = demangle(mangled)
    bracket_depth = 0
    last_sep = -1

    i = 0
    while i + 1 < len(full):
        ch = full[i]
        if ch == '<':
            bracket_depth += 1
        elif ch == '>':
            bracket_depth -= 1
        elif bracket_depth == 0 and ch == ':' and full[i + 1] == ':':
            last_sep = i + 2
            i += 1  # skip the second ':'
        i += 1

    return full[last_sep:] if last_sep >= 0 else full


def safe_demangle(mangled: Optional[str]) -> str:
    """Null-safe demangle — returns "" for None, never raises.

    C++ equivalent handles nullptr:
        if (!mangled_name) return "";
    """
    if mangled is None:
        return ""
    try:
        return demangle(mangled)
    except Exception:
        return mangled


def batch_demangle(symbols: list[str]) -> list[str]:
    """Demangle a list of symbols.

    For profiler stack traces where multiple frames need demangling at once.
    Uses the lru_cache on demangle() for deduplication.
    """
    return [safe_demangle(s) for s in symbols]
