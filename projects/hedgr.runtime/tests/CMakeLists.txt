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
    message(STATUS "hedgr.runtime: no tests defined")
    return()
endif()

set(name "hedgr.runtime")

add_executable(${name}.tests ${files})

target_link_libraries(${name}.tests
    PRIVATE
        hedgr.runtime.lib
        hedgr.model.lib
        hedgr.store.lib
        hedgr.session.lib
        ores::logging
        ores::platform
        Catch2::Catch2WithMain
        Threads::Threads
)

install(TARGETS ${name}.tests RUNTIME DESTINATION bin)

add_test(NAME ${name}.tests
    COMMAND ${name}.tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
