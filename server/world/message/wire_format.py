"""
world.message.wire_format — safe cross-ABI composite state transport
======================================================================

PRD #6: Fix composite individual state value lowering through message pointer
wrapper when crossing C-ABI transport boundaries.

Problem: CompositeState with virtual methods or non-standard-layout members
(e.g. lists, dicts) cannot be safely reinterpret_cast across process boundaries.
The vtable pointer and heap-allocated members have process-local addresses.

Solution: Define CompositeStateWire (POD transport carrier) with to_wire() /
from_wire() serialization.  MessagePtrWrapper routes standard-layout types
through zero-copy and non-standard types through the wire serialization path.

Sources:
  - CyberRT: message/message_traits, transport/message_info
  - Protocol Buffers: wire format encoding
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, fields, asdict
from typing import Any, Callable, Dict, List, Optional, Tuple, Type, TypeVar

T = TypeVar('T')


# ---------------------------------------------------------------------------
#  Standard layout detection (Python equivalent of is_standard_layout_v)
# ---------------------------------------------------------------------------

def is_standard_layout(cls: type) -> bool:
    """Check if a type is 'standard layout' for cross-ABI transport.

    In Python terms, a type is standard-layout if:
    1. It is a dataclass (structured, known fields)
    2. All fields are primitive types (int, float, bool, str, bytes)
       or tuples/frozen sets of primitives
    3. No virtual methods (__init_subclass__ overrides, ABCs)
    4. No mutable container fields (list, dict, set)

    Non-standard-layout types must go through to_wire()/from_wire().
    """
    if not hasattr(cls, '__dataclass_fields__'):
        return False

    PRIMITIVE = (int, float, bool, str, bytes, type(None))

    for f in fields(cls):
        ft = f.type
        # Handle string annotations
        if isinstance(ft, str):
            return False  # Cannot resolve at runtime → not standard
        if ft in PRIMITIVE:
            continue
        # Check for tuple of primitives
        origin = getattr(ft, '__origin__', None)
        if origin is tuple:
            args = getattr(ft, '__args__', ())
            if all(a in PRIMITIVE for a in args):
                continue
        # Nested dataclass: recurse
        if hasattr(ft, '__dataclass_fields__'):
            if not is_standard_layout(ft):
                return False
            continue
        return False
    return True


# ---------------------------------------------------------------------------
#  CompositeStateWire — POD transport carrier
# ---------------------------------------------------------------------------

# Wire format:
# [4 bytes: magic 'CSWF']
# [4 bytes: version]
# [4 bytes: type_name_len]
# [N bytes: type_name (UTF-8)]
# [4 bytes: num_fields]
# For each field:
#   [4 bytes: field_name_len]
#   [N bytes: field_name (UTF-8)]
#   [1 byte: field_type_tag]
#   [4 bytes: field_data_len]
#   [N bytes: field_data]
# [4 bytes: CRC32 checksum]

WIRE_MAGIC = b'CSWF'
WIRE_VERSION = 1

# Type tags for wire encoding
TAG_INT = 0x01
TAG_FLOAT = 0x02
TAG_BOOL = 0x03
TAG_STR = 0x04
TAG_BYTES = 0x05
TAG_NONE = 0x06
TAG_LIST = 0x07
TAG_DICT = 0x08
TAG_TUPLE = 0x09
TAG_NESTED = 0x0A  # nested dataclass


def _encode_value(value: Any) -> Tuple[int, bytes]:
    """Encode a single value to (type_tag, data_bytes)."""
    if value is None:
        return TAG_NONE, b''
    elif isinstance(value, bool):
        return TAG_BOOL, struct.pack('?', value)
    elif isinstance(value, int):
        return TAG_INT, struct.pack('<q', value)
    elif isinstance(value, float):
        return TAG_FLOAT, struct.pack('<d', value)
    elif isinstance(value, str):
        encoded = value.encode('utf-8')
        return TAG_STR, struct.pack('<I', len(encoded)) + encoded
    elif isinstance(value, bytes):
        return TAG_BYTES, struct.pack('<I', len(value)) + value
    elif isinstance(value, (list, tuple)):
        tag = TAG_TUPLE if isinstance(value, tuple) else TAG_LIST
        parts = []
        parts.append(struct.pack('<I', len(value)))
        for item in value:
            sub_tag, sub_data = _encode_value(item)
            parts.append(struct.pack('B', sub_tag))
            parts.append(struct.pack('<I', len(sub_data)))
            parts.append(sub_data)
        return tag, b''.join(parts)
    elif isinstance(value, dict):
        parts = []
        parts.append(struct.pack('<I', len(value)))
        for k, v in value.items():
            kt, kd = _encode_value(k)
            vt, vd = _encode_value(v)
            parts.append(struct.pack('B', kt))
            parts.append(struct.pack('<I', len(kd)))
            parts.append(kd)
            parts.append(struct.pack('B', vt))
            parts.append(struct.pack('<I', len(vd)))
            parts.append(vd)
        return TAG_DICT, b''.join(parts)
    elif hasattr(value, '__dataclass_fields__'):
        wire = to_wire(value)
        return TAG_NESTED, wire
    else:
        raise TypeError(f"Cannot encode type {type(value).__name__} to wire format")


def _decode_value(tag: int, data: bytes, offset: int = 0) -> Tuple[Any, int]:
    """Decode a value from (tag, data, offset). Returns (value, bytes_consumed)."""
    if tag == TAG_NONE:
        return None, 0
    elif tag == TAG_BOOL:
        return struct.unpack_from('?', data, offset)[0], 1
    elif tag == TAG_INT:
        return struct.unpack_from('<q', data, offset)[0], 8
    elif tag == TAG_FLOAT:
        return struct.unpack_from('<d', data, offset)[0], 8
    elif tag == TAG_STR:
        slen = struct.unpack_from('<I', data, offset)[0]
        s = data[offset + 4:offset + 4 + slen].decode('utf-8')
        return s, 4 + slen
    elif tag == TAG_BYTES:
        blen = struct.unpack_from('<I', data, offset)[0]
        return data[offset + 4:offset + 4 + blen], 4 + blen
    elif tag in (TAG_LIST, TAG_TUPLE):
        count = struct.unpack_from('<I', data, offset)[0]
        pos = offset + 4
        items = []
        for _ in range(count):
            sub_tag = data[pos]
            pos += 1
            sub_len = struct.unpack_from('<I', data, pos)[0]
            pos += 4
            val, _ = _decode_value(sub_tag, data, pos)
            items.append(val)
            pos += sub_len
        result = tuple(items) if tag == TAG_TUPLE else items
        return result, pos - offset
    elif tag == TAG_DICT:
        count = struct.unpack_from('<I', data, offset)[0]
        pos = offset + 4
        d = {}
        for _ in range(count):
            kt = data[pos]; pos += 1
            kl = struct.unpack_from('<I', data, pos)[0]; pos += 4
            k, _ = _decode_value(kt, data, pos); pos += kl
            vt = data[pos]; pos += 1
            vl = struct.unpack_from('<I', data, pos)[0]; pos += 4
            v, _ = _decode_value(vt, data, pos); pos += vl
            d[k] = v
        return d, pos - offset
    elif tag == TAG_NESTED:
        # The data is a full wire-encoded dataclass
        # We need the type registry to reconstruct
        raise NotImplementedError("Nested decode requires type registry")
    else:
        raise ValueError(f"Unknown wire type tag: 0x{tag:02x}")


# ---------------------------------------------------------------------------
#  to_wire / from_wire
# ---------------------------------------------------------------------------

def to_wire(state: Any) -> bytes:
    """Serialize a dataclass state to wire format (POD bytes).

    Returns a self-describing byte sequence with CRC32 integrity check.
    """
    if not hasattr(state, '__dataclass_fields__'):
        raise TypeError(f"{type(state).__name__} is not a dataclass")

    type_name = type(state).__qualname__.encode('utf-8')
    parts = []

    # Header
    parts.append(WIRE_MAGIC)
    parts.append(struct.pack('<I', WIRE_VERSION))
    parts.append(struct.pack('<I', len(type_name)))
    parts.append(type_name)

    # Fields
    dc_fields = fields(state)
    parts.append(struct.pack('<I', len(dc_fields)))

    for f in dc_fields:
        fname = f.name.encode('utf-8')
        parts.append(struct.pack('<I', len(fname)))
        parts.append(fname)

        value = getattr(state, f.name)
        tag, data = _encode_value(value)
        parts.append(struct.pack('B', tag))
        parts.append(struct.pack('<I', len(data)))
        parts.append(data)

    body = b''.join(parts)
    checksum = zlib.crc32(body) & 0xFFFFFFFF
    return body + struct.pack('<I', checksum)


def from_wire(wire_data: bytes, target_type: Type[T]) -> T:
    """Deserialize wire bytes back to a dataclass instance.

    Verifies CRC32 checksum for data integrity.
    """
    if len(wire_data) < 16:
        raise ValueError("Wire data too short")

    # Verify checksum
    body = wire_data[:-4]
    expected_crc = struct.unpack_from('<I', wire_data, len(wire_data) - 4)[0]
    actual_crc = zlib.crc32(body) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        raise ValueError(
            f"CRC32 mismatch: expected 0x{expected_crc:08x}, "
            f"got 0x{actual_crc:08x} — data corrupted in transit"
        )

    pos = 0
    # Magic
    magic = body[pos:pos + 4]
    if magic != WIRE_MAGIC:
        raise ValueError(f"Invalid wire magic: {magic!r}")
    pos += 4

    # Version
    version = struct.unpack_from('<I', body, pos)[0]
    pos += 4
    if version != WIRE_VERSION:
        raise ValueError(f"Unsupported wire version: {version}")

    # Type name
    tn_len = struct.unpack_from('<I', body, pos)[0]
    pos += 4
    _type_name = body[pos:pos + tn_len].decode('utf-8')
    pos += tn_len

    # Fields
    num_fields = struct.unpack_from('<I', body, pos)[0]
    pos += 4

    field_values = {}
    for _ in range(num_fields):
        fn_len = struct.unpack_from('<I', body, pos)[0]
        pos += 4
        fname = body[pos:pos + fn_len].decode('utf-8')
        pos += fn_len

        tag = body[pos]
        pos += 1
        data_len = struct.unpack_from('<I', body, pos)[0]
        pos += 4

        value, _ = _decode_value(tag, body, pos)
        pos += data_len
        field_values[fname] = value

    # Construct target type with decoded values
    return target_type(**field_values)


# ---------------------------------------------------------------------------
#  MessagePtrWrapper — safe cross-ABI wrapper
# ---------------------------------------------------------------------------

class MessagePtrWrapper:
    """Safe cross-ABI message pointer wrapper.

    For standard-layout types: zero-copy (direct reference).
    For non-standard types: serialize to wire format.

    This replaces the buggy reinterpret_cast pattern that caused data
    corruption when vtable pointers crossed process boundaries.
    """

    __slots__ = ('_data', '_type_name', '_is_wire', '_original_type')

    def __init__(
        self,
        data: Any,
        type_name: str,
        is_wire: bool,
        original_type: type,
    ):
        self._data = data
        self._type_name = type_name
        self._is_wire = is_wire
        self._original_type = original_type

    @classmethod
    def wrap(cls, state: Any) -> 'MessagePtrWrapper':
        """Wrap a state for cross-ABI transport.

        Standard-layout types are wrapped by reference (zero-copy).
        Non-standard types are serialized to wire format (safe but slower).
        """
        state_type = type(state)
        type_name = state_type.__qualname__

        if is_standard_layout(state_type):
            # Zero-copy path: standard layout, safe for shared memory
            return cls(state, type_name, is_wire=False, original_type=state_type)
        else:
            # Wire serialization path: safe for any type
            wire_data = to_wire(state)
            return cls(wire_data, type_name, is_wire=True, original_type=state_type)

    def unwrap(self, target_type: Optional[Type[T]] = None) -> T:
        """Unwrap the message, returning the original state.

        For zero-copy: returns the original reference.
        For wire: deserializes from wire format.
        """
        if target_type is None:
            target_type = self._original_type

        if self._is_wire:
            return from_wire(self._data, target_type)
        else:
            return self._data

    @property
    def is_wire_encoded(self) -> bool:
        return self._is_wire

    @property
    def type_name(self) -> str:
        return self._type_name

    @property
    def size_bytes(self) -> int:
        if self._is_wire:
            return len(self._data)
        else:
            return -1  # reference, not serialized


# ---------------------------------------------------------------------------
#  Wire type registry macro equivalent
# ---------------------------------------------------------------------------

_WIRE_REGISTRY: Dict[str, Type] = {}


def register_wire_type(state_type: Type, wire_type: Optional[Type] = None):
    """Register a state type for wire transport (WORLD_DEFINE_WIRE_TYPE macro).

    If wire_type is None, the state_type is assumed to be its own wire format
    (i.e., it's standard-layout).
    """
    type_name = state_type.__qualname__
    _WIRE_REGISTRY[type_name] = wire_type or state_type


def get_wire_type(type_name: str) -> Optional[Type]:
    return _WIRE_REGISTRY.get(type_name)


def validate_wire_types():
    """Validate all registered wire types are standard-layout.

    Equivalent to compile-time static_assert(is_standard_layout_v<WireType>).
    """
    errors = []
    for name, wt in _WIRE_REGISTRY.items():
        if not is_standard_layout(wt):
            errors.append(f"{name}: wire type {wt.__name__} is NOT standard-layout")
    return errors
