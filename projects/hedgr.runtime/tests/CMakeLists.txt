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
        Catch2::Catch2WithMain
        Threads::Threads
)

install(TARGETS ${name}.tests RUNTIME DESTINATION bin)

add_test(NAME ${name}.tests
    COMMAND ${name}.tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

#
# The seeder scans a directory of OreStudio checkouts for development
# deployments, which is a fact about the host and not about this program. A test
# suite that read the operator's own machine would pass or fail depending on how
# many checkouts happen to be beside the repository, so the scan is pointed at
# nowhere here and the suite sees the deployed-package case: no checkouts, one
# instance from the process's own options.
#
# The cases that are about seeding a list of environments set the variable
# themselves, against a fixture, and say so.
#
set_tests_properties(${name}.tests PROPERTIES
    ENVIRONMENT "HEDGR_ORESTUDIO_ROOT=")
