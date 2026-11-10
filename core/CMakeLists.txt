# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# The simulator-free primitives on their own: a static library and its tests,
# with no ns-3 tree. The ns-3 module (../CMakeLists.txt) compiles the same
# sources into libswarm-core and runs the same tests under test.py.
#
#   cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core
#
# SWARM_CORE_SANITIZE=ON adds ASan + UBSan, as the sanitized CI leg.
cmake_minimum_required(VERSION 3.20)
project(swarm_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SWARM_CORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
# The strict warnings the ns-3 CI applies to this module (ns3-containers'
# swarm-quality.cmake), so a local build catches what CI would.
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
                    -Wcast-align -Woverloaded-virtual -Wnull-dereference -Wformat=2 -Werror)
if(SWARM_CORE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(swarm_core STATIC
  swarm-dedup-history.cc
  swarm-pheromone-table.cc
  swarm-selection.cc
)
target_include_directories(swarm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(swarm_core_tests
  test/all-tests.cc
  test/run-core-tests.cc
  test/test-selection.cc
  test/test-structures.cc
  test/test-wire.cc
)
target_link_libraries(swarm_core_tests PRIVATE swarm_core)

enable_testing()
add_test(NAME swarm-core COMMAND swarm_core_tests)
