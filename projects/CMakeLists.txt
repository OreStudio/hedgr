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
#
# Designated initialisers that leave fields to their default member
# initialisers are the intended style, so that -Wextra check stays off. GCC's
# -Wmaybe-uninitialized reports std::string inside std::expected at -O3, which
# is not a defect in this code, so it stays a warning there.
#
add_compile_options(
    "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wnon-virtual-dtor;-Wconversion;-Wsign-conversion;-Werror>"
    "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-missing-designated-field-initializers>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-missing-field-initializers;-Wno-error=maybe-uninitialized>"
    $<$<CXX_COMPILER_ID:MSVC>:/WX>)

#
# Projects
#
# Each is a single-module component: cmake root, include/, src/, tests/ and
# modeling/ directly under the component. Dependencies run one way only.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/hedgr.model)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/hedgr.store)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/hedgr.session)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/hedgr.runtime)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/hedgr.ui)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/hedgr.imgui)
