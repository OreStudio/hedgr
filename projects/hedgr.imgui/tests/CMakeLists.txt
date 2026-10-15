# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
include(${CMAKE_CURRENT_SOURCE_DIR}/component_files.cmake)

# The executable is glue: it opens a GL context, calls the runtime and hands the
# frame to hedgr.ui. There is no honest unit test for that, because a test which
# asserted the defaults of a config struct would pass with every function stubbed
# out. It is verified by running it: build-target.sh, then a capture that drives
# the interface -- HEDGR_CAPTURE with HEDGR_CAPTURE_INPUT -- and reading the
# frame it writes.
if(NOT files)
    message(STATUS "hedgr.imgui: no unit tests; verified by HEDGR_CAPTURE")
    return()
endif()

set(name "hedgr.imgui")

add_executable(${name}.tests ${files})

target_include_directories(${name}.tests PRIVATE
    ${CMAKE_SOURCE_DIR}/projects/hedgr.imgui/include)

target_link_libraries(${name}.tests
    PRIVATE
        hedgr.model.lib
        Catch2::Catch2WithMain
        Threads::Threads)

install(TARGETS ${name}.tests RUNTIME DESTINATION bin)

add_test(NAME ${name}.tests
    COMMAND ${name}.tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
