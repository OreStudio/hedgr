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
set(name "hedgr.imgui")
set(exe_binary_name ${name})
set(exe_target_name ${name}.exe)

set(files "")
file(GLOB_RECURSE files RELATIVE
    "${CMAKE_CURRENT_SOURCE_DIR}/"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${exe_target_name} ${files})
set_target_properties(${exe_target_name} PROPERTIES
    OUTPUT_NAME ${exe_binary_name})

target_include_directories(${exe_target_name} PRIVATE
    ${CMAKE_SOURCE_DIR}/projects/hedgr.imgui/include)

target_link_libraries(${exe_target_name}
    PRIVATE
        ores::client
        Boost::program_options
        imgui::imgui
        implot::implot
        GLEW::GLEW
        OpenGL::GL
        glfw
        ores::openssl_cleanup
        Threads::Threads)

# Copy background image to publish directory alongside the binary.
set(background_image "${CMAKE_SOURCE_DIR}/assets/images/large_background.png")
set(background_dest "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.imgui/background.png")
add_custom_command(TARGET ${exe_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.imgui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${background_image}" "${background_dest}"
    COMMENT "Copying background image to publish/share/hedgr.imgui/")

install(TARGETS ${exe_target_name} RUNTIME DESTINATION bin)
install(FILES ${background_image} DESTINATION share/hedgr.imgui
    RENAME background.png)
