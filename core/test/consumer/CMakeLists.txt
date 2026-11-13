# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# A scratch project that uses core/ the way another CMake project does (#100):
# it adds the directory and links swarm::core. tools/checks/check-core-consumer.sh
# builds it and checks what must not leak into a consumer: our test target, our
# ctest entry, our -Werror. Not part of the build of core/ alone or of the
# ns-3 module.
#
# SWARM_CORE_DIR is core/ (default: this checkout's). A project outside this
# repository fetches it instead: README "Use core/ from another CMake project".
cmake_minimum_required(VERSION 3.20)
project(swarm_core_consumer LANGUAGES CXX)

set(SWARM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "swarm-core's core/ directory")
add_subdirectory(${SWARM_CORE_DIR} swarm-core)

add_executable(consumer consumer.cc)
target_link_libraries(consumer PRIVATE swarm::core)

enable_testing()
add_test(NAME consumer COMMAND consumer)
