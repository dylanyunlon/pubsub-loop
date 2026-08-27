/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/
#ifndef CYBER_MAINBOARD_MODULE_CONTROLLER_H_
#define CYBER_MAINBOARD_MODULE_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "cyber/proto/dag_conf.pb.h"

#include "cyber/class_loader/class_loader_manager.h"
#include "cyber/component/component.h"
#include "cyber/mainboard/dependency_graph.h"
#include "cyber/mainboard/module_argument.h"

namespace world {
namespace cyber {
namespace mainboard {

using world::cyber::proto::DagConfig;

class ModuleController {
 public:
  explicit ModuleController(const ModuleArgument& args);
  virtual ~ModuleController() = default;

  bool Init();
  bool LoadAll();
  void Clear();

  /// Layer-parallel boot using DependencyGraph (PRD #12).
  /// Builds the dependency graph from DAG configs, detects cycles,
  /// and boots individuals layer by layer (parallel within layers).
  bool LayeredBoot();

  /// Access the dependency graph (populated after Init/LoadAll).
  const DependencyGraph& GetDependencyGraph() const { return dep_graph_; }

 private:
  bool LoadModule(const std::string& path);
  bool LoadModule(const DagConfig& dag_config);
  int GetComponentNum(const std::string& path);
  int total_component_nums = 0;
  bool has_timer_component = false;

  ModuleArgument args_;
  class_loader::ClassLoaderManager class_loader_manager_;
  std::vector<std::shared_ptr<ComponentBase>> component_list_;
  DependencyGraph dep_graph_;
};

inline ModuleController::ModuleController(const ModuleArgument& args)
    : args_(args) {}

inline bool ModuleController::Init() { return LoadAll(); }

}  // namespace mainboard
}  // namespace cyber
}  // namespace world

#endif  // CYBER_MAINBOARD_MODULE_CONTROLLER_H_
