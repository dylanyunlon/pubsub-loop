"""
world.tools.doc_gen — auto-generate API docs from individual node channel definitions
========================================================================================

PRD #10: Auto-generation of world model API documentation from individual node
channel definitions, state schemas, and pub/sub topic metadata.

Problem: 47 IndividualNode types, only 26% have docs, 8 of those are stale.
WORLD_INDIVIDUAL_NODE_REGISTER macros contain all channel metadata.

Solution: Parse channel declarations from code/registry, generate Markdown
API docs per IndividualNode type, with CI auto-rebuild.

Sources:
  - CyberRT: tools/codegen.cpp
  - pubsub-loop: node/node.py, component/component.py
"""

from __future__ import annotations

import os
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Set, Tuple, Type


@dataclass
class ChannelDoc:
    """Documentation for a single pub/sub channel."""
    name: str
    msg_type: str               # type name
    direction: str              # 'pub' or 'sub'
    qos: str = 'default'        # QoS profile name
    description: str = ''
    frequency_hz: float = 0.0   # expected publish frequency
    msg_fields: List[Tuple[str, str]] = field(default_factory=list)  # (name, type)


@dataclass
class IndividualNodeDoc:
    """Documentation for a single IndividualNode type."""
    type_name: str
    module: str
    description: str = ''
    channels: List[ChannelDoc] = field(default_factory=list)
    capabilities: Set[str] = field(default_factory=set)
    lifecycle_hooks: List[str] = field(default_factory=list)
    dependencies: List[str] = field(default_factory=list)
    source_file: str = ''
    generated_at: str = ''


class DocGenerator:
    """Generate API documentation from IndividualNode registrations.

    Usage::

        gen = DocGenerator()
        gen.scan_registry(registry)       # scan from runtime registry
        gen.scan_components(components)    # scan from component instances
        docs = gen.generate_markdown()     # produce markdown for each node type
        gen.write_to_dir('/docs/api/')     # write files
    """

    def __init__(self):
        self._nodes: Dict[str, IndividualNodeDoc] = {}

    def register_node(
        self,
        type_name: str,
        module: str = '',
        description: str = '',
        source_file: str = '',
    ) -> IndividualNodeDoc:
        """Register a node type for documentation."""
        doc = IndividualNodeDoc(
            type_name=type_name,
            module=module,
            description=description,
            source_file=source_file,
            generated_at=time.strftime('%Y-%m-%d %H:%M:%S'),
        )
        self._nodes[type_name] = doc
        return doc

    def add_channel(
        self,
        type_name: str,
        channel_name: str,
        msg_type: str,
        direction: str,
        qos: str = 'default',
        description: str = '',
        frequency_hz: float = 0.0,
        msg_fields: Optional[List[Tuple[str, str]]] = None,
    ):
        """Add a channel doc to a registered node type."""
        doc = self._nodes.get(type_name)
        if doc is None:
            doc = self.register_node(type_name)

        ch = ChannelDoc(
            name=channel_name,
            msg_type=msg_type,
            direction=direction,
            qos=qos,
            description=description,
            frequency_hz=frequency_hz,
            msg_fields=msg_fields or [],
        )
        doc.channels.append(ch)

    def scan_component_class(self, cls: type):
        """Scan a UnifiedComponentBase subclass for channel declarations.

        Instantiates the component in a mock context and captures publish/subscribe
        calls to build documentation.
        """
        type_name = cls.__qualname__
        module = cls.__module__ if hasattr(cls, '__module__') else ''
        doc_str = cls.__doc__ or ''

        self.register_node(type_name, module, doc_str.strip().split('\n')[0])

        # Introspect the class for declared channels
        if hasattr(cls, 'published_channels'):
            try:
                # Try to get channel metadata from class annotations
                for attr_name in dir(cls):
                    if attr_name.startswith('_'):
                        continue
                    attr = getattr(cls, attr_name, None)
                    if callable(attr) and hasattr(attr, '_channel_meta'):
                        meta = attr._channel_meta
                        self.add_channel(
                            type_name,
                            meta.get('channel', attr_name),
                            meta.get('msg_type', 'Any'),
                            meta.get('direction', 'pub'),
                        )
            except Exception:
                pass

    def generate_markdown(self, type_name: Optional[str] = None) -> Dict[str, str]:
        """Generate Markdown documentation.

        If type_name is given, generate for that type only.
        Returns {type_name: markdown_content}.
        """
        result = {}
        nodes = (
            {type_name: self._nodes[type_name]}
            if type_name and type_name in self._nodes
            else self._nodes
        )

        for name, doc in nodes.items():
            lines = []
            lines.append(f'# {name}')
            lines.append('')
            if doc.description:
                lines.append(doc.description)
                lines.append('')
            lines.append(f'**Module:** `{doc.module}`')
            if doc.source_file:
                lines.append(f'**Source:** `{doc.source_file}`')
            lines.append(f'**Generated:** {doc.generated_at}')
            lines.append('')

            # Channels table
            pub_channels = [c for c in doc.channels if c.direction == 'pub']
            sub_channels = [c for c in doc.channels if c.direction == 'sub']

            if pub_channels:
                lines.append('## Published Channels')
                lines.append('')
                lines.append('| Channel | Message Type | QoS | Frequency |')
                lines.append('|---------|-------------|-----|-----------|')
                for ch in pub_channels:
                    freq = f'{ch.frequency_hz:.1f} Hz' if ch.frequency_hz else '-'
                    lines.append(f'| `{ch.name}` | `{ch.msg_type}` | {ch.qos} | {freq} |')
                lines.append('')

            if sub_channels:
                lines.append('## Subscribed Channels')
                lines.append('')
                lines.append('| Channel | Message Type | QoS |')
                lines.append('|---------|-------------|-----|')
                for ch in sub_channels:
                    lines.append(f'| `{ch.name}` | `{ch.msg_type}` | {ch.qos} |')
                lines.append('')

            # Message field details
            for ch in doc.channels:
                if ch.msg_fields:
                    lines.append(f'### `{ch.name}` — `{ch.msg_type}`')
                    lines.append('')
                    lines.append('| Field | Type |')
                    lines.append('|-------|------|')
                    for fname, ftype in ch.msg_fields:
                        lines.append(f'| `{fname}` | `{ftype}` |')
                    lines.append('')

            if doc.capabilities:
                lines.append('## Capabilities')
                lines.append('')
                for cap in sorted(doc.capabilities):
                    lines.append(f'- `{cap}`')
                lines.append('')

            result[name] = '\n'.join(lines)

        return result

    def write_to_dir(self, output_dir: str):
        """Write generated docs to a directory."""
        os.makedirs(output_dir, exist_ok=True)
        docs = self.generate_markdown()
        for name, content in docs.items():
            safe_name = name.replace('::', '_').replace('.', '_')
            path = os.path.join(output_dir, f'{safe_name}.md')
            with open(path, 'w') as f:
                f.write(content)

    @property
    def node_count(self) -> int:
        return len(self._nodes)

    def summary(self) -> str:
        """Return a summary of documented nodes."""
        total = len(self._nodes)
        with_channels = sum(1 for d in self._nodes.values() if d.channels)
        total_channels = sum(len(d.channels) for d in self._nodes.values())
        return (
            f"Documented {total} node types, "
            f"{with_channels} with channels, "
            f"{total_channels} total channels"
        )
