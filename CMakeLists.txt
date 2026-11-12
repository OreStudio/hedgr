# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2025 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

#
# vcpkg
#
# Hedgr owns its own vcpkg checkout. It deliberately does not reuse the copy
# vendored inside the OreStudio submodule: the two projects have different
# dependency sets and must be able to move their pins independently.
#
set(CMAKE_TOOLCHAIN_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake
    CACHE STRING "Vcpkg toolchain file")

#
# vcpkg overlay ports owned by Hedgr. These are stock ports with the minimal
# change needed to build on a machine that lacks the corresponding system
# development package; see build/vcpkg-overlays/README.org.
#
set(VCPKG_OVERLAY_PORTS
    ${CMAKE_CURRENT_SOURCE_DIR}/build/vcpkg-overlays
    CACHE STRING "vcpkg overlay ports owned by Hedgr")

# Log CMake version. Useful for debugging CMake problems.
message(STATUS "CMake Version: ${CMAKE_VERSION}")

#
# C++ standard. OreStudio's public headers require C++23 (std::expected,
# std::unreachable), so we default to it when no preset or command line has
# stated a standard.
#
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

project(Hedgr VERSION 0.3.3 LANGUAGES CXX
    DESCRIPTION "Multi-instance ImGui/ImPlot client for OreStudio.")

# The one place the version is written. The client's banner reads it from here
# rather than repeating it, because a hand-copied version is a version that
# eventually lies about what is running.
add_compile_definitions(HEDGR_PROJECT_VERSION="${PROJECT_VERSION}")

# Copy all binaries into the publish directory.
set(HEDGR_PUBLISH_DIRECTORY ${CMAKE_BINARY_DIR}/publish)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${HEDGR_PUBLISH_DIRECTORY}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${HEDGR_PUBLISH_DIRECTORY}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${HEDGR_PUBLISH_DIRECTORY}/bin)

# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH FALSE)

# Build with the install RPATH, because the build tree is the deployable
# artefact. Letting CMake also append the directories of every library it
# linked would leave absolute build-host paths in RUNPATH, and a package run on
# the build host would then resolve against the build tree rather than against
# its own bundle.
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

#
# The publish directory is the deployable artefact, so it has to be relocatable.
#
# One value serves the executable and the libraries alike. From publish/bin it
# resolves to publish/lib, and from publish/lib it resolves to itself. OreStudio
# lives nowhere else on the target, so its libraries are copied in by the
# bundling step in projects/hedgr.imgui and the loader never needs an absolute
# path.
#
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")

if(WIN32 AND (MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(UNIX)
    # The install prefix is where "make install" puts things. It is deliberately
    # not the RPATH: the same tree is deployed by copying the publish directory,
    # and that has to work wherever it lands.
    set(CMAKE_INSTALL_PREFIX "/opt/Hedgr/${CMAKE_PROJECT_VERSION}")
endif()

#
# add our own modules
#
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build/cmake/modules)

#
# options
#
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

#
# c++ standard.
#
message(STATUS "Targeting C++ standard: ${CMAKE_CXX_STANDARD}")

#
# Setup SCCache
#
find_program(CCACHE_PROGRAM sccache)
if (CCACHE_PROGRAM)
    message(STATUS "Found SCCache (${CCACHE_PROGRAM})...")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "")
else()
    message(STATUS "SCCache NOT found.")
endif()

# Threads
find_package(Threads REQUIRED)

# Boost
set(Boost_USE_MULTITHREADED ON)
if(WIN32)
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _winver ${CMAKE_SYSTEM_VERSION})
    math(EXPR _winver "(${CMAKE_MATCH_1} << 8) + ${CMAKE_MATCH_2}" OUTPUT_FORMAT HEXADECIMAL)
    add_compile_definitions(_WIN32_WINNT=${_winver})
    add_compile_definitions(BOOST_USE_WINAPI_VERSION=${_winver})
endif()

find_package(Boost REQUIRED COMPONENTS
    log
    crc
    uuid
    exception
    scope_exit
    iostreams
    process
    program_options)

# OpenSSL
find_package(OpenSSL REQUIRED)

# Third-party headers pulled in by OreStudio's public headers
find_package(reflectcpp CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)

# The store's entities are sqlgen structs, so sqlgen is part of that
# component's public surface rather than a private implementation detail.
find_package(sqlgen CONFIG REQUIRED)

# ImGui
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

# Embedded store and tests
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

# Tests are registered with CTest in every component's tests/ directory.
enable_testing()

#
# OreStudio (pre-built)
#
set(ORES_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/OreStudio/build/output/linux-clang-debug"
    CACHE PATH "Path to pre-built OreStudio build output directory")

find_package(OreStudio REQUIRED)

#
# Build type
#
if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Configuring in DEBUG mode")
elseif(CMAKE_BUILD_TYPE MATCHES Release)
    if (UNIX)
        set(CMAKE_INSTALL_DO_STRIP TRUE)
    endif()
    message(STATUS "Configuring in RELEASE mode")
elseif(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    message(STATUS "Configuring in RELEASE mode WITH symbols")
elseif(CMAKE_BUILD_TYPE MATCHES MinRelSize)
    if (UNIX)
        set(CMAKE_INSTALL_DO_STRIP TRUE)
    endif()
    message(STATUS "Configuring in RELEASE mode with minimised size")
else()
    message(STATUS "Unrecognized build type - will use cmake defaults")
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/projects)

#
# Package the publish directory for deployment.
#
# The archive is the publish tree as it stands, because that tree is already
# relocatable and already complete. There is nothing to stage.
#
set(HEDGR_PACKAGE_NAME
    "hedgr-${CMAKE_PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(HEDGR_PACKAGE_FILE ${CMAKE_BINARY_DIR}/${HEDGR_PACKAGE_NAME}.tar.gz)

add_custom_target(package_hedgr
    COMMENT "Packaging ${HEDGR_PACKAGE_NAME}"
    COMMAND ${CMAKE_COMMAND} -E tar czf ${HEDGR_PACKAGE_FILE}
            --format=gnutar publish
    COMMAND ${CMAKE_COMMAND} -E echo "Wrote ${HEDGR_PACKAGE_FILE}"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS hedgr.imgui.exe
    VERBATIM)
