# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
set(name "hedgr.runtime")
set(lib_binary_name ${name})
set(lib_target_name ${name}.lib)

include(${CMAKE_CURRENT_SOURCE_DIR}/component_files.cmake)

# A component with no sources yet must still configure.
if(NOT files)
    message(STATUS "hedgr.runtime: no sources yet")
    return()
endif()

add_library(${lib_target_name} ${files})
target_compile_definitions(${lib_target_name} PRIVATE HEDGR_RUNTIME_LIBRARY)

#
# Where the OreStudio checkouts live, baked in at configure time.
#
# The seeder looks for the development deployments beside this repository so a
# fresh store comes up with the environments an operator actually runs. The
# lookup is a directory scan rather than a walk up from the executable, for the
# same reason the store's path is: the executable's location says nothing about
# where its source came from. A machine with no such tree -- a deployed package
# -- finds nothing, warns, and falls back to the one instance its own options
# describe. HEDGR_ORESTUDIO_ROOT overrides it at run time.
#
target_compile_definitions(${lib_target_name} PRIVATE
    HEDGR_ORESTUDIO_ROOT="${CMAKE_SOURCE_DIR}/../../OreStudio")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${lib_target_name} PRIVATE
        -fvisibility=hidden -fvisibility-inlines-hidden)
endif()
set_target_properties(${lib_target_name} PROPERTIES
    OUTPUT_NAME ${lib_binary_name})
set_target_properties(${lib_target_name}
    PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(${lib_target_name} PUBLIC
    ${CMAKE_SOURCE_DIR}/projects/hedgr.runtime/include)
# The component's own src/ is on the include path for support.hpp, the helpers
# the manager, the seeder and the services share and do not export.
target_include_directories(${lib_target_name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${lib_target_name}
    PUBLIC
        hedgr.model.lib
        hedgr.session.lib
        hedgr.store.lib
        ores::logging
        Threads::Threads
    PRIVATE
        ores::platform
)

install(TARGETS ${lib_target_name} LIBRARY DESTINATION lib)
