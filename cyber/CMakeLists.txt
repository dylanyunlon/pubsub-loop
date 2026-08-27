cmake_minimum_required(VERSION 3.14)
project(world_cyber VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Dependencies ──
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(pybind11 QUIET)

# Optional: FastDDS for RTPS transport
find_package(fastrtps QUIET)
if(fastrtps_FOUND)
  add_compile_definitions(CYBER_TRANSPORT_RTPS=1)
endif()

# ── Proto generation ──
file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# ── Core library ──
file(GLOB_RECURSE CYBER_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/base/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/blocker/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/class_loader/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/component/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/croutine/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/event/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/io/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/logger/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/mainboard/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/message/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/node/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/parameter/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/scheduler/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/service/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/service_discovery/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/sysmo/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/task/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/time/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/timer/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/transport/*.cc"
)

# Exclude test files
list(FILTER CYBER_SOURCES EXCLUDE REGEX ".*_test\\.cc$")

add_library(world_cyber SHARED
  ${CYBER_SOURCES}
  ${PROTO_SRCS}
)

target_include_directories(world_cyber PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_BINARY_DIR}
  ${PROTOBUF_INCLUDE_DIRS}
)

target_link_libraries(world_cyber
  ${PROTOBUF_LIBRARIES}
  Threads::Threads
  dl
  rt
)

if(fastrtps_FOUND)
  target_link_libraries(world_cyber fastrtps)
endif()

# ── Python binding ──
if(pybind11_FOUND)
  pybind11_add_module(_world_cyber
    python/py_world_binding.cc
  )
  target_link_libraries(_world_cyber PRIVATE world_cyber)
  target_include_directories(_world_cyber PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_BINARY_DIR}
  )
endif()

# ── Install ──
install(TARGETS world_cyber
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(DIRECTORY base/ common/ component/ croutine/ data/ event/ io/
  logger/ mainboard/ message/ node/ parameter/ scheduler/
  service/ service_discovery/ sysmo/ task/ time/ timer/ transport/
  DESTINATION include/cyber
  FILES_MATCHING PATTERN "*.h"
)

install(FILES ${PROTO_HDRS} DESTINATION include/cyber/proto)
