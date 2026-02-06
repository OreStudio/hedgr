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

``ORES_BUILD_DIR``
  Path to the OreStudio build output directory (e.g.
  ``external/OreStudio/build/output/linux-clang-debug``).

Imported targets:

``ores::client``
  Interface target with OreStudio headers and pre-built libraries needed
  by Hedgr to act as a comms client.

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
# Collect all project include directories from OreStudio source tree.
#
file(GLOB _ores_include_dirs "${ORES_SOURCE_DIR}/projects/*/include")

message(STATUS "OreStudio source: ${ORES_SOURCE_DIR}")
message(STATUS "OreStudio build:  ${ORES_BUILD_DIR}")
message(STATUS "OreStudio include dirs found: ${_ores_include_dirs}")

#
# Create ores::client INTERFACE IMPORTED target
#
if(NOT TARGET ores::client)
    add_library(ores::client INTERFACE IMPORTED)

    set_target_properties(ores::client PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_ores_include_dirs}"
        INTERFACE_LINK_DIRECTORIES "${ORES_BUILD_DIR}/publish/lib"
    )

    target_link_libraries(ores::client INTERFACE
        ores.comms
        ores.iam
        ores.utility
        ores.eventing
        ores.logging
        ores.telemetry
        ores.platform
        ores.database
        OpenSSL::SSL
        OpenSSL::Crypto
        Boost::log
        Boost::iostreams
        Boost::exception
        Boost::uuid
        Boost::crc
        Boost::process
    )
endif()

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
