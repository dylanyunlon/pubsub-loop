"""
world.common.constraints — capability-constrained member functions
===================================================================

PRD #55: Extend CYBER_TRAILING_REQUIRES to constrain non-template individual
component member functions by capability predicates.

In C++ this is a SFINAE/requires macro.  In Python, we implement this as
decorators that check capability predicates at call time and raise clear
errors when a method is called on a type that doesn't have the required
capability.

Usage::

    class IndividualState:
        has_position = True
        has_velocity = False

        @requires_capability('has_position')
        def update_position(self, x, y, z):
            ...

        @requires_capability('has_velocity')
        def update_velocity(self, vx, vy, vz):
            ...  # raises CapabilityError if has_velocity is False

Sources:
  - CyberRT: common/macros.h CYBER_TRAILING_REQUIRES
  - C++20: requires clauses on non-template members
"""

from __future__ import annotations

import functools
from typing import Any, Callable, Optional, Set, Type, TypeVar

F = TypeVar('F', bound=Callable)


class CapabilityError(TypeError):
    """Raised when a method is called on a type lacking the required capability."""
    def __init__(self, method_name: str, capability: str, cls_name: str):
        super().__init__(
            f"{cls_name}.{method_name}() requires capability '{capability}', "
            f"but {cls_name} does not provide it"
        )
        self.method_name = method_name
        self.capability = capability
        self.cls_name = cls_name


def requires_capability(capability: str) -> Callable:
    """Decorator: constrain a method to types with a given capability flag.

    The capability is checked by looking for a class attribute with the given
    name that evaluates to True.  If the attribute is missing or False, calling
    the method raises CapabilityError.

    This is the Python equivalent of:
        CYBER_MEMBER_REQUIRES(has_position_v)
        void update_position(float x, float y, float z);
    """
    def decorator(func: F) -> F:
        @functools.wraps(func)
        def wrapper(self, *args, **kwargs):
            cls = type(self)
            if not getattr(cls, capability, False):
                raise CapabilityError(func.__name__, capability, cls.__name__)
            return func(self, *args, **kwargs)
        return wrapper
    return decorator


def requires_any(*capabilities: str) -> Callable:
    """Decorator: method requires at least one of the listed capabilities."""
    def decorator(func: F) -> F:
        @functools.wraps(func)
        def wrapper(self, *args, **kwargs):
            cls = type(self)
            if not any(getattr(cls, cap, False) for cap in capabilities):
                raise CapabilityError(
                    func.__name__,
                    ' | '.join(capabilities),
                    cls.__name__,
                )
            return func(self, *args, **kwargs)
        return wrapper
    return decorator


def requires_all(*capabilities: str) -> Callable:
    """Decorator: method requires ALL of the listed capabilities."""
    def decorator(func: F) -> F:
        @functools.wraps(func)
        def wrapper(self, *args, **kwargs):
            cls = type(self)
            missing = [cap for cap in capabilities if not getattr(cls, cap, False)]
            if missing:
                raise CapabilityError(
                    func.__name__,
                    ' & '.join(missing),
                    cls.__name__,
                )
            return func(self, *args, **kwargs)
        return wrapper
    return decorator


# ---------------------------------------------------------------------------
#  Static capability validation (compile-time equivalent)
# ---------------------------------------------------------------------------

def validate_capabilities(cls: type, required: Set[str]) -> list:
    """Validate that a class provides all required capabilities.

    Returns a list of error messages for missing capabilities.
    Equivalent to static_assert(has_capability_v<T>) at class definition time.
    """
    errors = []
    for cap in required:
        if not getattr(cls, cap, False):
            errors.append(f"{cls.__name__} is missing capability '{cap}'")
    return errors


def capability_check(cls: type = None, *, capabilities: Set[str] = None):
    """Class decorator: validate capabilities at class definition time.

    Usage::

        @capability_check(capabilities={'has_position', 'has_velocity'})
        class MovingIndividual:
            has_position = True
            has_velocity = True
            ...
    """
    def wrapper(cls):
        if capabilities:
            errors = validate_capabilities(cls, capabilities)
            if errors:
                raise TypeError(
                    f"Capability check failed for {cls.__name__}: "
                    + '; '.join(errors)
                )
        return cls
    if cls is not None:
        return wrapper(cls)
    return wrapper


# ---------------------------------------------------------------------------
#  Capability descriptor (introspection)
# ---------------------------------------------------------------------------

def list_capabilities(cls: type) -> Set[str]:
    """List all capability flags (bool class attributes starting with 'has_')."""
    result = set()
    for name in dir(cls):
        if name.startswith('has_') and isinstance(getattr(cls, name, None), bool):
            if getattr(cls, name):
                result.add(name)
    return result
