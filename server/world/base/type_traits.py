"""
world.base.type_traits — compile-time-like type introspection for pub/sub-loop
================================================================================

PRD #51: is_virtual_base_of — Python port of C++ SFINAE virtual-base detection.

In Python, there is no syntactic 'virtual inheritance', but the MRO (Method
Resolution Order) exhibits analogous diamond-inheritance patterns.  This module
provides `is_virtual_base_of(base, derived)` which returns True when `base`
appears in `derived.__mro__` via **multiple inheritance paths** (the Python
equivalent of C++ virtual-base sharing), and False for single-path (non-virtual)
or non-base relationships.

Use cases in pubsub-loop:
  - IndividualDispatcher must choose dynamic_cast-like safe downcasting for
    diamond-inherited capability bases vs direct attribute access for linear
    inheritance.
  - CapabilityRegistry must deduplicate registrations when a concrete Individual
    inherits the same capability base through multiple mixins.

Sources:
  - CyberRT: <type_traits>, is_base_of usage in component/
  - agent-governance-toolkit: AgentMesh capability registration

Implementation note:
  Python's C3 linearization (MRO) already resolves the diamond, so the "virtual"
  distinction is: does Base appear in more than one *direct inheritance branch*
  of Derived's class tree?  If yes → virtual-like (shared instance).
  If no → single-path (non-virtual).
"""

from __future__ import annotations

import functools
from typing import Any, Dict, FrozenSet, List, Optional, Set, Tuple, Type


# ---------------------------------------------------------------------------
#  is_virtual_base_of  (PRD #51)
# ---------------------------------------------------------------------------

@functools.lru_cache(maxsize=4096)
def is_virtual_base_of(base: type, derived: type) -> bool:
    """Return True iff *base* is a 'virtual base' (diamond-shared) of *derived*.

    A base class is considered 'virtual' when it is reachable through **more
    than one direct parent** of *derived*.  This mirrors the C++ virtual-base
    semantics where a single shared sub-object replaces per-path copies.

    Returns False for non-class types, same-type, and single-path inheritance.
    """
    if not (isinstance(base, type) and isinstance(derived, type)):
        return False
    if base is derived:
        return False
    if not issubclass(derived, base):
        return False

    # Count how many *direct parents* of `derived` have `base` in their MRO.
    direct_parents = derived.__bases__
    paths = sum(1 for parent in direct_parents if issubclass(parent, base))
    return paths >= 2


@functools.lru_cache(maxsize=4096)
def is_base_of(base: type, derived: type) -> bool:
    """Safe issubclass wrapper (never raises on non-class args)."""
    if not (isinstance(base, type) and isinstance(derived, type)):
        return False
    if base is derived:
        return False
    return issubclass(derived, base)


# ---------------------------------------------------------------------------
#  safe_downcast — dynamic_cast equivalent
# ---------------------------------------------------------------------------

def safe_downcast(base_instance: Any, target_type: type) -> Optional[Any]:
    """Safely downcast *base_instance* to *target_type*.

    In C++ terms:
    - For non-virtual bases, equivalent to static_cast (fast, unchecked).
    - For virtual bases, equivalent to dynamic_cast (runtime check).

    Returns None if the cast is invalid.
    """
    if isinstance(base_instance, target_type):
        return base_instance
    return None


def safe_upcast(derived_instance: Any, base_type: type) -> Optional[Any]:
    """Safely upcast, returning the same object if isinstance holds.

    For virtual bases in C++, even upcast needs dynamic_cast; in Python
    this is always safe, but we surface the API for parity.
    """
    if isinstance(derived_instance, base_type):
        return derived_instance
    return None


# ---------------------------------------------------------------------------
#  Diamond detection utilities
# ---------------------------------------------------------------------------

@functools.lru_cache(maxsize=1024)
def shared_virtual_bases(cls: type) -> FrozenSet[type]:
    """Return the set of classes that appear as virtual (diamond) bases of *cls*."""
    result: Set[type] = set()
    for ancestor in cls.__mro__[1:]:  # skip cls itself
        if ancestor is object:
            continue
        if is_virtual_base_of(ancestor, cls):
            result.add(ancestor)
    return frozenset(result)


def inheritance_depth(base: type, derived: type) -> int:
    """MRO distance from *derived* to *base*.  -1 if not a base."""
    if not (isinstance(base, type) and isinstance(derived, type)):
        return -1
    try:
        return derived.__mro__.index(base)
    except ValueError:
        return -1


def deduplicated_capabilities(cls: type, capability_base: type) -> List[type]:
    """Return capability classes reachable from *cls*, deduplicating virtual bases.

    Solves the CapabilityRegistry double-registration bug:
    if CapabilityBase is a virtual base, it appears once, not per-path.
    """
    seen: Set[type] = set()
    result: List[type] = []
    virtual_bases = shared_virtual_bases(cls)

    for ancestor in cls.__mro__:
        if ancestor is object or ancestor is cls:
            continue
        if not issubclass(ancestor, capability_base):
            continue
        if ancestor in virtual_bases:
            # Virtual base: only register once
            if ancestor not in seen:
                seen.add(ancestor)
                result.append(ancestor)
        else:
            # Non-virtual: always register
            result.append(ancestor)
            seen.add(ancestor)
    return result


# ---------------------------------------------------------------------------
#  IndividualDispatcher integration helper
# ---------------------------------------------------------------------------

class DispatchPolicy:
    """Per-type dispatch metadata used by IndividualDispatcher.

    Precomputes whether each base in the hierarchy is virtual,
    so dispatch() can choose safe_downcast vs direct attribute access.
    """

    __slots__ = ('_type', '_virtual_bases', '_dispatch_map')

    def __init__(self, individual_type: type):
        self._type = individual_type
        self._virtual_bases = shared_virtual_bases(individual_type)
        self._dispatch_map: Dict[type, bool] = {}
        for ancestor in individual_type.__mro__:
            if ancestor is object:
                continue
            self._dispatch_map[ancestor] = ancestor in self._virtual_bases

    def needs_dynamic_cast(self, base_type: type) -> bool:
        """Return True if accessing *base_type* on this individual requires
        dynamic_cast (i.e. it's a virtual/diamond base)."""
        return self._dispatch_map.get(base_type, False)

    def dispatch_to(self, individual: Any, base_type: type) -> Optional[Any]:
        """Cast *individual* to *base_type* using the correct cast strategy."""
        if self.needs_dynamic_cast(base_type):
            return safe_downcast(individual, base_type)
        else:
            # Non-virtual: direct attribute access is safe
            if isinstance(individual, base_type):
                return individual
            return None

    @property
    def virtual_bases(self) -> FrozenSet[type]:
        return self._virtual_bases


# ---------------------------------------------------------------------------
#  string_view compat (PRD #53)
# ---------------------------------------------------------------------------

# Python equivalent: str is already the universal string type.
# This alias exists only for API parity with the C++ world::base namespace.
string_view = str  # replaces deprecated __string_view alias
