# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
set(name "hedgr.imgui")
set(exe_binary_name ${name})
set(exe_target_name ${name}.exe)

set(files "")
# CONFIGURE_DEPENDS so adding a source re-runs CMake rather than silently
# producing a stale target.
file(GLOB_RECURSE files CONFIGURE_DEPENDS RELATIVE
    "${CMAKE_CURRENT_SOURCE_DIR}/"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${exe_target_name} ${files})
set_target_properties(${exe_target_name} PROPERTIES
    OUTPUT_NAME ${exe_binary_name})

target_include_directories(${exe_target_name} PRIVATE
    ${CMAKE_SOURCE_DIR}/projects/hedgr.imgui/include)

target_link_libraries(${exe_target_name}
    PRIVATE
        hedgr.runtime.lib
        hedgr.ui.lib
        hedgr.session.lib
        hedgr.store.lib
        hedgr.model.lib
        ores::client
        ores::openssl_cleanup
        Boost::program_options
        imgui::imgui
        implot::implot
        GLEW::GLEW
        OpenGL::GL
        glfw
        Threads::Threads)

# Copy background image to publish directory alongside the binary.
set(background_image
    "${CMAKE_SOURCE_DIR}/assets/images/Gemini_Generated_Image_f656v5f656v5f656.jpeg")
set(background_dest "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.imgui/background.jpeg")
add_custom_command(TARGET ${exe_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.imgui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${background_image}" "${background_dest}"
    COMMENT "Copying background image to publish/share/hedgr.imgui/")

install(TARGETS ${exe_target_name} RUNTIME DESTINATION bin)
install(FILES ${background_image} DESTINATION share/hedgr.imgui
    RENAME background.jpeg)
