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
# SWARM_CORE_TESTS builds the tests and registers them with ctest; it is ON
# when this is the top-level project and OFF when another project adds this
# directory (add_subdirectory, or FetchContent with SOURCE_SUBDIR core), which
# then gets the library target swarm_core (alias swarm::core) alone, with
# no -Werror and no test of ours in its ctest (#100; README "Use core/ from
# another CMake project").
# SWARM_CORE_BENCH=ON adds the microbenchmarks (bench/, dev-only: Google
# Benchmark fetched at a pinned commit, checked by its hash; nothing shipped
# links it).
# SWARM_CORE_INSTALL adds the install rules: the library, the headers under
# include/swarm-core/ and the CMake package swarm-core (#133), so
# `find_package(swarm-core CONFIG REQUIRED)` gives the imported target
# swarm::core. ON at the top level, OFF in someone else's build, so their
# `cmake --install` installs nothing of ours unless they ask for it.
cmake_minimum_required(VERSION 3.20)

# The package version is the repository's VERSION (one level up, in a checkout
# and in the release tarball); SWARM_CORE_VERSION when core/ is used without it.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" _swarm_core_version LIMIT_COUNT 1)
else()
  set(SWARM_CORE_VERSION "0.0.0" CACHE STRING "swarm-core's version when ../VERSION is absent")
  set(_swarm_core_version "${SWARM_CORE_VERSION}")
endif()
project(swarm_core VERSION ${_swarm_core_version} LANGUAGES CXX)

# The C++ standard of the ns-3 release swarm-core supports: ns-3's
# CMAKE_CXX_STANDARD_MINIMUM (C++23 from ns-3.46; docs/api-policy.md "C++
# standard"). tools/checks/check-cxx-standard.py fails CI when this and the
# cxx_std_ feature below fall behind the ns3-dev image's minimum.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Top-level: `cmake -S core`. (PROJECT_IS_TOP_LEVEL needs CMake 3.21.)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(SWARM_CORE_TOP_LEVEL ON)
else()
  set(SWARM_CORE_TOP_LEVEL OFF)
endif()

option(SWARM_CORE_TESTS "Build the tests and register them with ctest" ${SWARM_CORE_TOP_LEVEL})
option(SWARM_CORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SWARM_CORE_BENCH "Build the microbenchmarks (fetches Google Benchmark)" OFF)
option(SWARM_CORE_INSTALL "Add the install rules and the CMake package swarm-core" ${SWARM_CORE_TOP_LEVEL})
# The strict warnings the ns-3 CI applies to this module (ns3-containers'
# swarm-quality.cmake), so a local build catches what CI would.
set(SWARM_CORE_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
                        -Wcast-align -Woverloaded-virtual -Wnull-dereference -Wformat=2)
if(SWARM_CORE_TOP_LEVEL)
  add_compile_options(${SWARM_CORE_WARNINGS} -Werror)
endif()
if(SWARM_CORE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# The source list, shared with the ns-3 module's CMakeLists.txt.
include(${CMAKE_CURRENT_LIST_DIR}/swarm-core-sources.cmake)

add_library(swarm_core STATIC ${SWARM_CORE_SOURCES})
add_library(swarm::core ALIAS swarm_core)
# The imported target of the installed package is swarm::core too (EXPORT_NAME
# and the export's NAMESPACE), the same name a subproject's alias gives.
set_target_properties(swarm_core PROPERTIES EXPORT_NAME core)
include(GNUInstallDirs)
target_include_directories(swarm_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/swarm-core>)
# A consumer's targets that link this one compile at C++23 or later, in a
# subproject and through the installed package alike.
target_compile_features(swarm_core PUBLIC cxx_std_23)
if(SWARM_CORE_SANITIZE)
  # The objects are instrumented, so whatever links them needs the sanitizer
  # runtime: add_link_options above reaches this directory's targets only, not
  # a consumer's executable.
  target_link_options(swarm_core INTERFACE -fsanitize=address,undefined)
endif()
if(NOT SWARM_CORE_TOP_LEVEL)
  # In someone else's build: our warnings on our own sources only, and not as
  # errors (their compiler may know warnings ours does not).
  target_compile_options(swarm_core PRIVATE ${SWARM_CORE_WARNINGS})
endif()

if(SWARM_CORE_INSTALL)
  include(CMakePackageConfigHelpers)
  set(SWARM_CORE_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/swarm-core)
  install(TARGETS swarm_core EXPORT swarm-coreTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(FILES ${SWARM_CORE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/swarm-core)
  # testbench/swarm-testbench.h includes "../swarm-ports.h": the same layout.
  install(FILES ${SWARM_CORE_TESTBENCH_HEADERS}
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/swarm-core/testbench)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/swarm-core)
  install(EXPORT swarm-coreTargets NAMESPACE swarm:: DESTINATION ${SWARM_CORE_CMAKE_DIR})
  configure_package_config_file(swarm-coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/swarm-coreConfig.cmake
    INSTALL_DESTINATION ${SWARM_CORE_CMAKE_DIR})
  # SemVer read against docs/api-policy.md: from 1.0 a release is compatible
  # with every earlier one of its major; before 1.0 any minor may break, so a
  # 0.y release satisfies a request for 0.y only.
  if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(_swarm_core_compat SameMinorVersion)
  else()
    set(_swarm_core_compat SameMajorVersion)
  endif()
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/swarm-coreConfigVersion.cmake
    COMPATIBILITY ${_swarm_core_compat})
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/swarm-coreConfig.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/swarm-coreConfigVersion.cmake
          DESTINATION ${SWARM_CORE_CMAKE_DIR})
endif()

if(SWARM_CORE_TESTS)
  add_executable(swarm_core_tests test/run-core-tests.cc ${SWARM_CORE_TEST_SOURCES})
  target_link_libraries(swarm_core_tests PRIVATE swarm_core)

  enable_testing()
  add_test(NAME swarm-core COMMAND swarm_core_tests)
endif()

if(SWARM_CORE_BENCH)
  include(FetchContent)
  # Google Benchmark v1.9.5 (Apache-2.0), pinned by commit and archive hash
  # (swarm-routing/.github#32). The hash is GitHub's archive of the commit; it
  # equals `git archive --format=tar --prefix=benchmark-<sha>/ <sha> | gzip -cn`
  # of a clone, so a bump can be checked from git alone. Its own tests and install rules stay off, and
  # this project's strict warnings do not apply to it.
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googlebenchmark
    URL https://github.com/google/benchmark/archive/192ef10025eb2c4cdd392bc502f0c852196baa48.tar.gz
    URL_HASH SHA256=f82705a2726d8f6cdcda274b841f6314dbfc6f731cdda06c946f310ec1cc3ad9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # The strict warnings above are this directory's; targets made from here on
  # (Google Benchmark's, the bench's) start without them.
  set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
  FetchContent_MakeAvailable(googlebenchmark)

  add_executable(swarm_core_bench bench/swarm-core-bench.cc)
  target_compile_options(swarm_core_bench PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Werror)
  target_link_libraries(swarm_core_bench PRIVATE swarm_core benchmark::benchmark)
endif()
