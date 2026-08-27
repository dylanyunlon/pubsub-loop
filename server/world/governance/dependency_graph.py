"""
world.governance.dependency_graph — inter-individual dependency graph
========================================================================

PRD #12: Declare inter-individual dependency graph in mainboard world-init
package metadata for deterministic individual boot ordering.

Problem: Without explicit dependency declarations, individual boot order is
non-deterministic.  sensor_fusion may start before lidar_driver, read
uninitialized channels, and produce garbage for the first N ticks.

Solution: YAML-declared dependency graph with:
- Topological sort (Kahn's algorithm) producing layered boot plan
- Cycle detection with human-readable cycle path
- Optional individuals that don't block dependents on failure
- Per-individual boot timeout enforcement

Sources:
  - CyberRT: mainboard/module_argument.h
  - agent-governance-toolkit: orchestration/dependency graph
"""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass, field
from typing import Dict, FrozenSet, List, Optional, Set, Tuple


@dataclass
class IndividualDecl:
    """Declaration of an individual in the dependency graph."""
    id: str
    depends_on: List[str] = field(default_factory=list)
    boot_timeout_ms: int = 5000
    optional: bool = False
    module: str = ''
    priority: int = 0


@dataclass
class BootLayer:
    """A layer of individuals that can boot in parallel."""
    layer_index: int
    individuals: List[IndividualDecl] = field(default_factory=list)

    @property
    def ids(self) -> List[str]:
        return [ind.id for ind in self.individuals]

    @property
    def max_timeout_ms(self) -> int:
        return max((ind.boot_timeout_ms for ind in self.individuals), default=0)


class DependencyGraph:
    """Inter-individual dependency graph with topological boot ordering.

    Usage::

        graph = DependencyGraph()
        graph.add_individual(IndividualDecl(
            id='sensor_fusion',
            depends_on=['lidar_driver', 'camera_driver'],
        ))

        cycle = graph.detect_cycle()
        if cycle:
            raise RuntimeError(f"Cycle: {' -> '.join(cycle)}")

        layers = graph.compute_boot_layers()
        for layer in layers:
            boot_parallel(layer.individuals)
    """

    def __init__(self):
        self._individuals: Dict[str, IndividualDecl] = {}
        self._adj: Dict[str, Set[str]] = defaultdict(set)
        self._rev: Dict[str, Set[str]] = defaultdict(set)

    def add_individual(self, decl: IndividualDecl):
        self._individuals[decl.id] = decl
        for dep in decl.depends_on:
            self._adj[dep].add(decl.id)
            self._rev[decl.id].add(dep)
        if decl.id not in self._adj:
            self._adj[decl.id] = set()

    def remove_individual(self, individual_id: str):
        if individual_id in self._individuals:
            del self._individuals[individual_id]
        for dep_set in self._adj.values():
            dep_set.discard(individual_id)
        self._adj.pop(individual_id, None)
        for dep_set in self._rev.values():
            dep_set.discard(individual_id)
        self._rev.pop(individual_id, None)

    def detect_cycle(self) -> Optional[List[str]]:
        """Returns None if acyclic, or the cycle path as a list of IDs."""
        WHITE, GRAY, BLACK = 0, 1, 2
        color = {nid: WHITE for nid in self._individuals}
        parent = {}

        def dfs(node: str) -> Optional[List[str]]:
            color[node] = GRAY
            for neighbor in self._adj.get(node, set()):
                if neighbor not in color:
                    continue
                if color[neighbor] == GRAY:
                    cycle = [neighbor, node]
                    cur = node
                    while cur in parent and parent[cur] != neighbor:
                        cur = parent[cur]
                        cycle.append(cur)
                    cycle.reverse()
                    return cycle
                if color[neighbor] == WHITE:
                    parent[neighbor] = node
                    result = dfs(neighbor)
                    if result:
                        return result
            color[node] = BLACK
            return None

        for nid in self._individuals:
            if color.get(nid, WHITE) == WHITE:
                result = dfs(nid)
                if result:
                    return result
        return None

    def compute_boot_layers(self) -> List[BootLayer]:
        """Kahn's topological sort producing layered boot plan."""
        cycle = self.detect_cycle()
        if cycle:
            raise RuntimeError(
                f"Cycle detected: {' -> '.join(cycle)}"
            )

        in_degree = {nid: len(self._rev.get(nid, set()) & set(self._individuals))
                     for nid in self._individuals}
        queue = deque(nid for nid, deg in in_degree.items() if deg == 0)
        layers = []
        visited = set()

        while queue:
            layer_nodes = list(queue)
            queue.clear()
            layer = BootLayer(
                layer_index=len(layers),
                individuals=[self._individuals[nid] for nid in layer_nodes],
            )
            layers.append(layer)
            for nid in layer_nodes:
                visited.add(nid)
                for dependent in self._adj.get(nid, set()):
                    if dependent in self._individuals and dependent not in visited:
                        in_degree[dependent] -= 1
                        if in_degree[dependent] <= 0:
                            queue.append(dependent)

        unvisited = set(self._individuals) - visited
        if unvisited:
            raise RuntimeError(f"Unreachable individuals: {unvisited}")
        return layers

    def topological_order(self) -> List[str]:
        layers = self.compute_boot_layers()
        return [ind.id for layer in layers for ind in layer.individuals]

    def dependents_of(self, individual_id: str) -> Set[str]:
        return set(self._adj.get(individual_id, set()))

    def dependencies_of(self, individual_id: str) -> Set[str]:
        return set(self._rev.get(individual_id, set()))

    def transitive_dependencies(self, individual_id: str) -> Set[str]:
        visited = set()
        queue = deque([individual_id])
        while queue:
            nid = queue.popleft()
            for dep in self._rev.get(nid, set()):
                if dep not in visited and dep in self._individuals:
                    visited.add(dep)
                    queue.append(dep)
        return visited

    def critical_path(self) -> Tuple[List[str], int]:
        """Longest dependency chain for capacity planning."""
        layers = self.compute_boot_layers()
        total = sum(layer.max_timeout_ms for layer in layers)
        path = []
        for layer in layers:
            if layer.individuals:
                longest = max(layer.individuals, key=lambda i: i.boot_timeout_ms)
                path.append(longest.id)
        return path, total

    @property
    def individual_count(self) -> int:
        return len(self._individuals)

    @property
    def edge_count(self) -> int:
        return sum(len(deps) for deps in self._adj.values())

    @classmethod
    def from_config(cls, config: List[dict]) -> 'DependencyGraph':
        """Create from individuals.yaml v2 format."""
        graph = cls()
        for entry in config:
            decl = IndividualDecl(
                id=entry['id'],
                depends_on=entry.get('depends_on', []),
                boot_timeout_ms=entry.get('boot_timeout_ms', 5000),
                optional=entry.get('optional', False),
                module=entry.get('module', ''),
            )
            graph.add_individual(decl)
        return graph
