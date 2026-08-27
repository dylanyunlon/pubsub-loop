/******************************************************************************
 * py_world_binding.cc — pybind11 module for pubsub-loop world
 *
 * Usage from Python:
 *   import world_cyber
 *   world_cyber.init("my_app")
 *   resolver = world_cyber.WorldResolver("my_world")
 *   ind = resolver.register_individual(1, "agent_0")
 *   ind.request_motion(1.0, 0.0, 0.0, ...)
 *   resolver.tick(1, 0.016)
 *   state = ind.read_confirmed_state()
 *****************************************************************************/

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "cyber/python/py_world.h"

namespace py = pybind11;

PYBIND11_MODULE(_world_cyber, m) {
  m.doc() = "pubsub-loop World CyberRT Python bindings";

  // Module-level functions
  m.def("init", &world::cyber::py_init, py::arg("module_name"),
        "Initialize CyberRT runtime");
  m.def("ok", &world::cyber::py_ok, "Check if CyberRT is running");
  m.def("shutdown", &world::cyber::py_shutdown, "Shutdown CyberRT");
  m.def("is_shutdown", &world::cyber::py_is_shutdown,
        "Check if shutdown was called");
  m.def("wait_for_shutdown", &world::cyber::py_waitforshutdown,
        "Block until shutdown");

  // PyWriter
  py::class_<world::cyber::PyWriter>(m, "Writer")
      .def("write", &world::cyber::PyWriter::write, py::arg("data"),
           "Write serialized proto data to channel");

  // PyReader
  py::class_<world::cyber::PyReader>(m, "Reader")
      .def("read", &world::cyber::PyReader::read,
           py::arg("wait") = false,
           "Read next message from channel");

  // PyIndividual — the entity in the world
  py::class_<world::cyber::PyIndividual>(m, "Individual")
      .def("request_motion", &world::cyber::PyIndividual::request_motion,
           py::arg("dx"), py::arg("dy"), py::arg("dz"),
           py::arg("dqx") = 0.0, py::arg("dqy") = 0.0,
           py::arg("dqz") = 0.0, py::arg("dqw") = 1.0,
           py::arg("vx") = 0.0, py::arg("vy") = 0.0, py::arg("vz") = 0.0,
           py::arg("collision_mask") = 0xFFFFFFFF,
           py::arg("priority") = 0,
           "Submit motion request — '我要动了' notification to world")
      .def("read_confirmed_state",
           &world::cyber::PyIndividual::read_confirmed_state,
           "Read world-confirmed state after resolution")
      .def_property_readonly("individual_id",
                             &world::cyber::PyIndividual::individual_id)
      .def_property_readonly("name", &world::cyber::PyIndividual::name);

  // PyWorldResolver — the central world authority
  py::class_<world::cyber::PyWorldResolver>(m, "WorldResolver")
      .def(py::init<const std::string&>(), py::arg("world_name"))
      .def("register_individual",
           &world::cyber::PyWorldResolver::register_individual,
           py::arg("id"), py::arg("name"),
           py::return_value_policy::reference_internal,
           "Register individual into the world")
      .def("tick", &world::cyber::PyWorldResolver::tick,
           py::arg("tick_id"), py::arg("dt"),
           "Advance world by one tick — resolve all pending motions")
      .def_property_readonly("individual_count",
                             &world::cyber::PyWorldResolver::individual_count)
      .def("shutdown", &world::cyber::PyWorldResolver::shutdown);

  // PyNode — general pub/sub node
  py::class_<world::cyber::PyNode>(m, "Node")
      .def(py::init<const std::string&>(), py::arg("node_name"))
      .def("create_writer", &world::cyber::PyNode::create_writer,
           py::arg("channel"), py::arg("type"),
           py::arg("qos_depth") = 1,
           py::return_value_policy::take_ownership)
      .def("create_reader", &world::cyber::PyNode::create_reader,
           py::arg("channel"), py::arg("type"),
           py::return_value_policy::take_ownership)
      .def("create_individual", &world::cyber::PyNode::create_individual,
           py::arg("id"), py::arg("name"),
           py::return_value_policy::take_ownership)
      .def("shutdown", &world::cyber::PyNode::shutdown);
}
