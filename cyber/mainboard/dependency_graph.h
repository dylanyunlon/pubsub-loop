/******************************************************************************
 * dependency_graph.h — Inter-individual dependency graph for boot ordering
 *
 * PRD #12: Declare inter-individual dependency graph in mainboard world-init
 * package metadata for deterministic layer-parallel bootstrap.
 *
 * Features:
 *   - Kahn's algorithm topological sort with layer extraction
 *   - DFS cycle detection with path reporting
 *   - Transitive dependency closure
 *   - Graphviz DOT export for debug visualization
 *
 * Sources:
 *   - Apollo CyberRT: mainboard/module_controller.cc (serial LoadModule)
 *   - agent-governance-toolkit: dependency_graph.py (concept mapping)
 *
 * Namespace: world::cyber::mainboard
 *****************************************************************************/

#ifndef CYBER_MAINBOARD_DEPENDENCY_GRAPH_H_
#define CYBER_MAINBOARD_DEPENDENCY_GRAPH_H_

#include <algorithm>
#include <functional>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cyber/common/log.h"

namespace world {
namespace cyber {
namespace mainboard {

/**
 * Declaration of an individual node for dependency-ordered boot.
 */
struct IndividualDecl {
  std::string class_name;
  std::vector<std::string> depends_on;
  uint32_t boot_timeout_ms = 3000;
  bool optional = false;
  size_t source_index = 0;  // index in the original config
  bool is_timer = false;    // timer component vs. regular component
};

/**
 * A boot layer: all individuals in this layer can start in parallel,
 * because all their dependencies are in earlier layers.
 */
struct BootLayer {
  size_t layer_index;
  std::vector<IndividualDecl> individuals;
};

/**
 * DependencyGraph — DAG of individual boot dependencies.
 *
 * Usage:
 *   DependencyGraph graph;
 *   graph.AddIndividual({"SensorFusion", {"LidarDriver", "CameraDriver"}});
 *   graph.AddIndividual({"LidarDriver", {}});
 *   graph.AddIndividual({"CameraDriver", {}});
 *
 *   auto cycle = graph.DetectCycle();
 *   if (cycle) { // error: circular dependency }
 *
 *   auto layers = graph.TopologicalSort();
 *   // Layer 0: [LidarDriver, CameraDriver]  (no deps)
 *   // Layer 1: [SensorFusion]                (depends on layer 0)
 */
class DependencyGraph {
 public:
  /**
   * Add an individual to the graph.
   * All dependencies in depends_on must either be pre-added or will be
   * auto-created as leaf nodes.
   */
  void AddIndividual(IndividualDecl decl) {
    const auto& name = decl.class_name;

    // Ensure adjacency entry exists
    adjacency_[name];

    // For each dependency: name depends on dep
    // Graph edge direction: dep -> name (dep must boot before name)
    for (const auto& dep : decl.depends_on) {
      adjacency_[dep];  // ensure dep node exists
      adjacency_[dep].push_back(name);
      reverse_adj_[name].push_back(dep);
    }

    nodes_[name] = std::move(decl);
  }

  /**
   * Detect cycles using DFS 3-color algorithm.
   *
   * @return nullopt if no cycle; otherwise the cycle path as node names.
   */
  std::optional<std::vector<std::string>> DetectCycle() const {
    enum Color { WHITE, GRAY, BLACK };
    std::unordered_map<std::string, Color> color;
    std::unordered_map<std::string, std::string> parent;
    std::vector<std::string> cycle_path;
    bool found = false;

    for (const auto& [name, _] : adjacency_) {
      color[name] = WHITE;
    }

    std::function<bool(const std::string&)> dfs =
        [&](const std::string& u) -> bool {
      color[u] = GRAY;

      auto it = adjacency_.find(u);
      if (it != adjacency_.end()) {
        for (const auto& v : it->second) {
          if (color[v] == GRAY) {
            // Back edge found — reconstruct cycle
            cycle_path.clear();
            cycle_path.push_back(v);
            cycle_path.push_back(u);
            std::string cur = u;
            while (cur != v && parent.count(cur)) {
              cur = parent[cur];
              cycle_path.push_back(cur);
            }
            std::reverse(cycle_path.begin(), cycle_path.end());
            return true;
          }
          if (color[v] == WHITE) {
            parent[v] = u;
            if (dfs(v)) return true;
          }
        }
      }

      color[u] = BLACK;
      return false;
    };

    for (const auto& [name, _] : adjacency_) {
      if (color[name] == WHITE) {
        if (dfs(name)) {
          return cycle_path;
        }
      }
    }

    return std::nullopt;
  }

  /**
   * Topological sort with layer extraction (Kahn's algorithm).
   *
   * Returns layers where all individuals in layer N depend only on
   * individuals in layers 0..N-1. Individuals within a layer can
   * boot in parallel.
   */
  std::vector<BootLayer> TopologicalSort() const {
    // Compute in-degree
    std::unordered_map<std::string, int> in_degree;
    for (const auto& [name, _] : adjacency_) {
      in_degree[name];  // init to 0
    }
    for (const auto& [u, neighbors] : adjacency_) {
      for (const auto& v : neighbors) {
        in_degree[v]++;
      }
    }

    // Start with zero-degree nodes
    std::queue<std::string> current;
    for (const auto& [name, deg] : in_degree) {
      if (deg == 0) current.push(name);
    }

    std::vector<BootLayer> layers;
    size_t layer_idx = 0;

    while (!current.empty()) {
      BootLayer layer;
      layer.layer_index = layer_idx++;
      std::queue<std::string> next;

      while (!current.empty()) {
        auto name = current.front();
        current.pop();

        // Add to layer (with its declaration if it exists)
        auto node_it = nodes_.find(name);
        if (node_it != nodes_.end()) {
          layer.individuals.push_back(node_it->second);
        } else {
          // Implicit leaf node (declared as dependency but not added)
          IndividualDecl implicit;
          implicit.class_name = name;
          layer.individuals.push_back(std::move(implicit));
        }

        // Decrement in-degree of successors
        auto adj_it = adjacency_.find(name);
        if (adj_it != adjacency_.end()) {
          for (const auto& v : adj_it->second) {
            if (--in_degree[v] == 0) {
              next.push(v);
            }
          }
        }
      }

      if (!layer.individuals.empty()) {
        layers.push_back(std::move(layer));
      }
      current = std::move(next);
    }

    return layers;
  }

  /**
   * Compute transitive dependency closure for a given individual.
   */
  std::vector<std::string> TransitiveDeps(const std::string& id) const {
    std::unordered_set<std::string> visited;
    std::vector<std::string> result;

    std::function<void(const std::string&)> dfs =
        [&](const std::string& node) {
      auto it = reverse_adj_.find(node);
      if (it == reverse_adj_.end()) return;
      for (const auto& dep : it->second) {
        if (visited.insert(dep).second) {
          result.push_back(dep);
          dfs(dep);
        }
      }
    };

    dfs(id);
    return result;
  }

  /**
   * Export as Graphviz DOT for visualization/debugging.
   */
  std::string ToDot() const {
    std::ostringstream ss;
    ss << "digraph BootDependency {\n";
    ss << "  rankdir=LR;\n";
    ss << "  node [shape=box, style=rounded];\n";

    for (const auto& [u, neighbors] : adjacency_) {
      for (const auto& v : neighbors) {
        ss << "  \"" << u << "\" -> \"" << v << "\";\n";
      }
    }

    // Mark optional nodes
    for (const auto& [name, decl] : nodes_) {
      if (decl.optional) {
        ss << "  \"" << name << "\" [style=\"rounded,dashed\"];\n";
      }
    }

    ss << "}\n";
    return ss.str();
  }

  size_t Size() const { return nodes_.size(); }

  const std::unordered_map<std::string, IndividualDecl>& nodes() const {
    return nodes_;
  }

 private:
  std::unordered_map<std::string, IndividualDecl> nodes_;
  // Forward edges: dep -> dependent (dep must boot before dependent)
  std::unordered_map<std::string, std::vector<std::string>> adjacency_;
  // Reverse edges: dependent -> [deps] (what does this node depend on)
  std::unordered_map<std::string, std::vector<std::string>> reverse_adj_;
};

}  // namespace mainboard
}  // namespace cyber
}  // namespace world

#endif  // CYBER_MAINBOARD_DEPENDENCY_GRAPH_H_
