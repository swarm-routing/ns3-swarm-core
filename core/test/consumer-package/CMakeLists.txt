# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# A scratch project that uses a released swarm-core the two ways a consumer
# outside this repository does (#133, #134), and builds the same consumer.cc as
# core/test/consumer/:
#
#   SWARM_CORE_FROM=package  find_package(swarm-core ${SWARM_CORE_WANT} CONFIG
#                            REQUIRED) against an install prefix
#                            (CMAKE_PREFIX_PATH), linking the imported swarm::core;
#   SWARM_CORE_FROM=url      FetchContent of a release tarball by URL and
#                            URL_HASH SHA256, SOURCE_SUBDIR core (the tarball's
#                            single top directory, swarm-core/, is stripped).
#
# tools/checks/check-core-consumer.sh builds both. Not part of the build of
# core/ alone or of the ns-3 module.
cmake_minimum_required(VERSION 3.20)
project(swarm_core_package_consumer LANGUAGES CXX)

set(SWARM_CORE_FROM "package" CACHE STRING "package (find_package) or url (FetchContent)")
set(SWARM_CORE_WANT "" CACHE STRING "the version find_package asks for (empty: any)")
set(SWARM_CORE_URL "" CACHE STRING "with url: the release tarball's URL")
set(SWARM_CORE_SHA256 "" CACHE STRING "with url: its sha256, from the release's SHA256SUMS")

if(SWARM_CORE_FROM STREQUAL "package")
  find_package(swarm-core ${SWARM_CORE_WANT} CONFIG REQUIRED)
  message(STATUS "swarm-core ${swarm-core_VERSION} from ${swarm-core_DIR}")
elseif(SWARM_CORE_FROM STREQUAL "url")
  include(FetchContent)
  # DOWNLOAD_EXTRACT_TIMESTAMP is CMake 3.24+; an older CMake would read it as
  # part of URL_HASH's value.
  set(_swarm_core_timestamp)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(_swarm_core_timestamp DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()
  FetchContent_Declare(swarm_core
    URL ${SWARM_CORE_URL}
    URL_HASH SHA256=${SWARM_CORE_SHA256}
    ${_swarm_core_timestamp}
    SOURCE_SUBDIR core)
  FetchContent_MakeAvailable(swarm_core)
else()
  message(FATAL_ERROR "SWARM_CORE_FROM is '${SWARM_CORE_FROM}': package or url")
endif()

add_executable(consumer ${CMAKE_CURRENT_SOURCE_DIR}/../consumer/consumer.cc)
target_link_libraries(consumer PRIVATE swarm::core)

enable_testing()
add_test(NAME consumer COMMAND consumer)
