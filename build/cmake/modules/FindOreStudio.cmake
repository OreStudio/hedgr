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

#[=======================================================================[.rst:
FindOreStudio
-------------

Find pre-built OreStudio libraries and headers from the git submodule.

Input variables:

``ORES_SOURCE_DIR``
  Path to the OreStudio source tree. Only its public headers are consumed.

``ORES_BUILD_DIR``
  Path to the OreStudio build output directory (e.g.
  ``external/OreStudio/build/output/linux-clang-debug-make``).

Imported targets:

``ores::client``
  Interface target with OreStudio headers and pre-built libraries needed
  by Hedgr to act as a NATS client.

``ores::openssl_cleanup``
  Interface target for executables that call ``OPENSSL_cleanup()``.
#]=======================================================================]

set(ORES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/OreStudio"
    CACHE PATH "Path to OreStudio source tree")

#
# Validate inputs
#
if(NOT EXISTS "${ORES_SOURCE_DIR}/projects")
    message(FATAL_ERROR
        "OreStudio source not found at ${ORES_SOURCE_DIR}. "
        "Did you initialise the git submodule? "
        "(git submodule update --init --recursive)")
endif()

if(NOT EXISTS "${ORES_BUILD_DIR}/publish/lib")
    message(FATAL_ERROR
        "OreStudio pre-built libraries not found at ${ORES_BUILD_DIR}/publish/lib. "
        "Please build OreStudio first: "
        "cd external/OreStudio && cmake --preset <preset> && cmake --build --preset <preset>")
endif()

#
# Collect all public include directories from the OreStudio source tree.
#
# Two layouts coexist: flat (projects/<pkg>/include) and component-grouped
# (projects/<pkg>/<component>/include, e.g. ores.iam/client/include). Both
# must be searched or the grouped components silently lose their headers.
# The Python virtual environments some packages carry under projects/ are
# excluded, since their include/ is not an OreStudio API.
#
file(GLOB _ores_flat_includes "${ORES_SOURCE_DIR}/projects/*/include")
file(GLOB _ores_nested_includes "${ORES_SOURCE_DIR}/projects/*/*/include")

set(_ores_include_dirs "")
foreach(_ores_dir IN LISTS _ores_flat_includes _ores_nested_includes)
    if(_ores_dir MATCHES "/venv/")
        continue()
    endif()
    list(APPEND _ores_include_dirs "${_ores_dir}")
endforeach()

message(STATUS "OreStudio source: ${ORES_SOURCE_DIR}")
message(STATUS "OreStudio build:  ${ORES_BUILD_DIR}")
message(STATUS "OreStudio include dirs found: ${_ores_include_dirs}")

#
# Create ores::client INTERFACE IMPORTED target
#
# OreStudio bakes its third-party dependencies statically into each shared
# library, so only the libores.*.so files are linked. Their public headers do
# however include reflectcpp, magic_enum and jwt-cpp, so those packages must
# be found by the top-level project and are linked here to carry their
# include directories.
#
if(NOT TARGET ores::client)
    add_library(ores::client INTERFACE IMPORTED)

    set_target_properties(ores::client PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_ores_include_dirs}"
        INTERFACE_LINK_DIRECTORIES "${ORES_BUILD_DIR}/publish/lib"
    )

    target_link_libraries(ores::client INTERFACE
        ores.nats
        ores.iam.client
        ores.iam.api
        ores.eventing.api
        ores.platform
        ores.security
        ores.service
        ores.logging
        ores.utility
        ores.database
        ores.telemetry.core
        OpenSSL::SSL
        OpenSSL::Crypto
        Boost::log
        Boost::iostreams
        Boost::exception
        Boost::uuid
        Boost::crc
        Boost::process
        reflectcpp::reflectcpp
        magic_enum::magic_enum
        jwt-cpp::jwt-cpp
    )
endif()

#
# Per-library imported targets.
#
# ores::client is the whole of OreStudio and is kept for the application
# executable. A library component links the narrower target it actually uses,
# so the dependency graph stays honest: hedgr.store does not drag in NATS, and
# hedgr.ui does not drag in the database.
#
# OreStudio bakes its third-party dependencies statically into each shared
# library, but its public headers still include reflectcpp, magic_enum,
# jwt-cpp, Boost and OpenSSL, so those ride along on every target.
#
set(_ores_libraries
    nats
    iam.client
    iam.api
    eventing.api
    platform
    security
    service
    logging
    utility
    database
    telemetry.core)

foreach(_ores_library IN LISTS _ores_libraries)
    if(TARGET ores::${_ores_library})
        continue()
    endif()
    add_library(ores::${_ores_library} INTERFACE IMPORTED)
    set_target_properties(ores::${_ores_library} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_ores_include_dirs}"
        INTERFACE_LINK_DIRECTORIES "${ORES_BUILD_DIR}/publish/lib")
    target_link_libraries(ores::${_ores_library} INTERFACE
        ores.${_ores_library}
        reflectcpp::reflectcpp
        magic_enum::magic_enum
        jwt-cpp::jwt-cpp
        Boost::log
        Boost::system
        Boost::iostreams
        Boost::exception
        Boost::uuid
        Boost::crc
        Boost::process
        OpenSSL::SSL
        OpenSSL::Crypto)
endforeach()

#
# Create ores::openssl_cleanup INTERFACE target (mirrors OreStudio's)
#
if(NOT TARGET ores::openssl_cleanup)
    add_library(ores::openssl_cleanup INTERFACE IMPORTED)
    target_link_libraries(ores::openssl_cleanup INTERFACE OpenSSL::Crypto)
endif()

#
# Report status
#
set(OreStudio_FOUND TRUE)
message(STATUS "Found OreStudio (pre-built at ${ORES_BUILD_DIR})")
