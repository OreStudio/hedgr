# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
set(name "hedgr.ui")
set(lib_binary_name ${name})
set(lib_target_name ${name}.lib)

include(${CMAKE_CURRENT_SOURCE_DIR}/component_files.cmake)

# A component with no sources yet must still configure.
if(NOT files)
    message(STATUS "hedgr.ui: no sources yet")
    return()
endif()

add_library(${lib_target_name} ${files})
target_compile_definitions(${lib_target_name} PRIVATE HEDGR_UI_LIBRARY)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${lib_target_name} PRIVATE
        -fvisibility=hidden -fvisibility-inlines-hidden)
endif()
set_target_properties(${lib_target_name} PROPERTIES
    OUTPUT_NAME ${lib_binary_name})
set_target_properties(${lib_target_name}
    PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(${lib_target_name} PUBLIC
    ${CMAKE_SOURCE_DIR}/projects/hedgr.ui/include)
# The component's own src/ is on the include path for the renderers' shared
# private headers, which are deliberately not part of the public interface.
target_include_directories(${lib_target_name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${lib_target_name}
    PUBLIC
        hedgr.model.lib
        imgui::imgui
        implot::implot
)

install(TARGETS ${lib_target_name} LIBRARY DESTINATION lib)

# Copy the vendored Font Awesome face to the publish directory alongside the
# library, the way hedgr.imgui publishes its background image, so a run out of
# the build tree resolves icons through the same <exe>/../share/<component>
# rule that an installed run uses.
set(icon_font "${CMAKE_SOURCE_DIR}/assets/fonts/fa-solid-900.ttf")
add_custom_command(TARGET ${lib_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${icon_font}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/fa-solid-900.ttf"
    COMMENT "Copying Font Awesome face to publish/share/hedgr.ui/")

install(FILES ${icon_font} DESTINATION share/hedgr.ui)
