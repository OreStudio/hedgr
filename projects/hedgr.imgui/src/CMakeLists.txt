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

#
# The source tree this build was configured from, baked in.
#
# A development build keeps the local store inside the checkout, under .cache,
# and finding the checkout by walking up from the executable would be four
# levels of guessing that a relocated package breaks. The value is a string and
# not a path, so a build made somewhere else carries a path that is simply not
# there, and the client falls back to the user's data directory rather than
# refusing to start.
#
target_compile_definitions(${exe_target_name} PRIVATE
    HEDGR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

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

#
# Copy the desktop entry to the publish directory, in the applications
# subdirectory the desktop specification names rather than beside the
# background image, because a shell only scans share/applications for launchers.
# The name is the window identity the executable sets as its Wayland app id, so
# a shell that looks the launcher up by app id finds this file without a second
# mapping to keep in step. The file is carried beside the exe because the
# packaging target tars the publish tree, and install() alone never touches it.
#
set(application_desktop_file
    "${CMAKE_SOURCE_DIR}/assets/applications/org.hedgr.Hedgr.desktop")
add_custom_command(TARGET ${exe_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/applications"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${application_desktop_file}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/applications"
    COMMENT "Copying the desktop entry to publish/share/applications/")

install(FILES ${application_desktop_file} DESTINATION share/applications)

#
# Make the publish directory self-contained.
#
# OreStudio's libraries are not installed on the target and live outside this
# tree on the build host, so they are copied in. The step runs after the
# executable is linked, which is the point at which every library it needs has
# been produced.
#
# The command is not conditioned on UNIX because the bundler reports that a
# platform it does not understand has nothing to do, and a Windows build simply
# has no RUNPATH to make relative.
#
set(HEDGR_BUNDLE_SCRIPT ${CMAKE_SOURCE_DIR}/build/scripts/bundle-dependencies.py)
set(HEDGR_ORES_LIB_DIR ${ORES_BUILD_DIR}/publish/lib)

find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND AND TARGET hedgr.imgui.exe)
    add_custom_command(TARGET ${exe_target_name} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${HEDGR_BUNDLE_SCRIPT}
            --binary $<TARGET_FILE:${exe_target_name}>
            --destination ${HEDGR_PUBLISH_DIRECTORY}/lib
            --search ${HEDGR_PUBLISH_DIRECTORY}/lib
            --search ${HEDGR_ORES_LIB_DIR}
        COMMENT "Bundling the shared library dependencies into publish/lib"
        VERBATIM)
else()
    message(STATUS "Dependency bundling skipped: python3 or the executable target is missing")
endif()

