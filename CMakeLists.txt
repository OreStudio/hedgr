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
set(CMAKE_TOOLCHAIN_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/OreStudio/vcpkg/scripts/buildsystems/vcpkg.cmake
    CACHE STRING "Vcpkg toolchain file")

# Log CMake version. Useful for debugging CMake problems.
message(STATUS "CMake Version: ${CMAKE_VERSION}")

project(Hedgr VERSION 0.0.1 LANGUAGES CXX
    DESCRIPTION "Remote ImGui/ImPlot client for OreStudio.")

# Copy all binaries into the publish directory.
set(HEDGR_PUBLISH_DIRECTORY ${CMAKE_BINARY_DIR}/publish)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${HEDGR_PUBLISH_DIRECTORY}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${HEDGR_PUBLISH_DIRECTORY}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${HEDGR_PUBLISH_DIRECTORY}/bin)

# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH FALSE)

# when building, don't use the install RPATH already, but later on when
# installing.
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

if(WIN32 AND (MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(UNIX)
    set(CMAKE_INSTALL_PREFIX "/opt/Hedgr/${CMAKE_PROJECT_VERSION}")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

    # add the automatically determined parts of the RPATH
    # which point to directories outside the build tree to the install RPATH
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

    # the RPATH to be used when installing, but only if it's not a system directory
    list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
    if("${isSystemDir}" STREQUAL "-1")
        set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
    endif()
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

# ImGui
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

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
