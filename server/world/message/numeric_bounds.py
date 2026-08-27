"""
world.message.numeric_bounds — numeric boundary constraints for state properties
==================================================================================

PRD #101: Generate compile-time numeric boundary constraints for floating-point
individual state properties from message type-traits.

Problem: Out-of-range state values (position, velocity, health) propagate
silently through the pub/sub pipeline, corrupt GLB scene attributes, and are
only discovered during visual inspection.

Solution: Declarative boundary constraints on state struct fields, checked at
definition time (decorator) and at message-ingestion time (validate_bounds).

Sources:
  - CyberRT: message/message_traits numeric limits
  - Protocol Buffers: field options / validators
"""

from __future__ import annotations

import math
from dataclasses import dataclass, fields as dc_fields
from typing import Any, Dict, List, Optional, Set, Tuple, Type


@dataclass(frozen=True)
class Bound:
    """Numeric boundary constraint for a field."""
    min_val: float = -math.inf
    max_val: float = math.inf
    name: str = ''

    def __post_init__(self):
        if self.min_val >= self.max_val:
            raise ValueError(
                f"Bound '{self.name}': min ({self.min_val}) must be < max ({self.max_val})"
            )

    def check(self, value: float) -> bool:
        """Return True if value is within bounds."""
        return self.min_val <= value <= self.max_val

    def clamp(self, value: float) -> float:
        """Clamp value to bounds."""
        return max(self.min_val, min(value, self.max_val))


# ---------------------------------------------------------------------------
#  Standard bounds for common state properties
# ---------------------------------------------------------------------------

# World coordinate bounds (meters) — prevents GLB corruption
POSITION_BOUND = Bound(min_val=-1e6, max_val=1e6, name='position')

# Velocity bounds (m/s) — prevents physics explosion
VELOCITY_BOUND = Bound(min_val=-1e4, max_val=1e4, name='velocity')

# Angular velocity (rad/s)
ANGULAR_VELOCITY_BOUND = Bound(min_val=-1e3, max_val=1e3, name='angular_velocity')

# Quaternion component bounds (should be [-1, 1] after normalization)
QUATERNION_BOUND = Bound(min_val=-1.001, max_val=1.001, name='quaternion')

# Health / normalized attributes [0, 1]
UNIT_BOUND = Bound(min_val=0.0, max_val=1.0, name='unit')

# Temperature (Kelvin) — physical minimum
TEMPERATURE_BOUND = Bound(min_val=0.0, max_val=1e6, name='temperature')

# Time-related bounds
TICK_RATE_BOUND = Bound(min_val=0.001, max_val=10000.0, name='tick_rate_hz')
DURATION_BOUND = Bound(min_val=0.0, max_val=86400.0, name='duration_seconds')


# ---------------------------------------------------------------------------
#  Bounds registry and validation
# ---------------------------------------------------------------------------

# Maps (type_name, field_name) → Bound
_BOUNDS_REGISTRY: Dict[Tuple[str, str], Bound] = {}


def register_bounds(cls: type, field_name: str, bound: Bound):
    """Register a boundary constraint for a specific field of a state type."""
    key = (cls.__qualname__, field_name)
    _BOUNDS_REGISTRY[key] = bound


def get_bounds(cls: type, field_name: str) -> Optional[Bound]:
    """Get registered bounds for a field."""
    return _BOUNDS_REGISTRY.get((cls.__qualname__, field_name))


def bounded_field(bound: Bound):
    """Decorator-style metadata marker for dataclass fields.

    Usage::

        @dataclass
        class IndividualPos:
            x: float = field(metadata={'bound': POSITION_BOUND})
            y: float = field(metadata={'bound': POSITION_BOUND})
            z: float = field(metadata={'bound': POSITION_BOUND})
    """
    return {'bound': bound}


def validate_bounds(state: Any) -> List[str]:
    """Validate all bounded fields of a dataclass state instance.

    Returns a list of violation messages (empty = all valid).
    """
    if not hasattr(state, '__dataclass_fields__'):
        return []

    violations = []
    cls_name = type(state).__qualname__

    for f in dc_fields(state):
        value = getattr(state, f.name)
        if not isinstance(value, (int, float)):
            continue

        # Check metadata-based bounds
        bound = f.metadata.get('bound') if f.metadata else None
        if bound is None:
            bound = _BOUNDS_REGISTRY.get((cls_name, f.name))
        if bound is None:
            continue

        if not bound.check(float(value)):
            violations.append(
                f"{cls_name}.{f.name} = {value} out of bounds "
                f"[{bound.min_val}, {bound.max_val}]"
            )

    return violations


def clamp_state(state: Any) -> Any:
    """Clamp all bounded fields to their valid ranges.

    Modifies the state in-place and returns it.
    """
    if not hasattr(state, '__dataclass_fields__'):
        return state

    cls_name = type(state).__qualname__

    for f in dc_fields(state):
        value = getattr(state, f.name)
        if not isinstance(value, (int, float)):
            continue

        bound = f.metadata.get('bound') if f.metadata else None
        if bound is None:
            bound = _BOUNDS_REGISTRY.get((cls_name, f.name))
        if bound is None:
            continue

        clamped = bound.clamp(float(value))
        if clamped != value:
            object.__setattr__(state, f.name, clamped)

    return state


def bounded_state(cls):
    """Class decorator: auto-register bounds from field metadata.

    Usage::

        @bounded_state
        @dataclass
        class IndividualPos:
            x: float = field(metadata={'bound': POSITION_BOUND})
            ...
    """
    if hasattr(cls, '__dataclass_fields__'):
        for f in dc_fields(cls):
            bound = f.metadata.get('bound') if f.metadata else None
            if bound is not None:
                register_bounds(cls, f.name, bound)
    return cls
