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

# A component with no tests yet must still configure. Add a source to
# component_files.cmake to switch the test target on.
if(NOT files)
    message(STATUS "hedgr.ui: no tests defined")
    return()
endif()

set(name "hedgr.ui")

# The allocation counter replaces operator new for the whole binary, so it is
# compiled once per test executable from the one copy the model tests own.
set(allocation_counter_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../hedgr.model/tests)
add_executable(${name}.tests ${files}
    ${allocation_counter_dir}/allocation_counter.cpp)
target_include_directories(${name}.tests PRIVATE ${allocation_counter_dir})

target_link_libraries(${name}.tests
    PRIVATE
        hedgr.ui.lib
        hedgr.model.lib
        imgui::imgui
        implot::implot
        Catch2::Catch2WithMain
        Threads::Threads
)

install(TARGETS ${name}.tests RUNTIME DESTINATION bin)

add_test(NAME ${name}.tests
    COMMAND ${name}.tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
