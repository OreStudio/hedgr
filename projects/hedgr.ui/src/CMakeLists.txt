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

# miniaudio is the sound backend ADR 8 names, a single header with no
# dependencies. The vcpkg port ships the header and no CMake config, so it is
# found by path rather than by find_package, and only this component sees it.
find_path(HEDGR_MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
find_package(lunasvg CONFIG REQUIRED)

# A component with no sources yet must still configure.
if(NOT files)
    message(STATUS "hedgr.ui: no sources yet")
    return()
endif()

add_library(${lib_target_name} ${files})
target_compile_definitions(${lib_target_name} PRIVATE HEDGR_UI_LIBRARY)
# Vendored upstream code is kept byte-for-byte apart from its patch, so it is
# not held to the project's warning set.
set_source_files_properties(../third_party/imcoolbar/ImCoolBar.cpp
    PROPERTIES COMPILE_OPTIONS $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w>)
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
# private headers, which are deliberately not part of the public interface. The
# vendored ImCoolBar directory is on it as well, so a ui source reaches the
# widget by its upstream name rather than by a path into the vendored tree.
target_include_directories(${lib_target_name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/imcoolbar
    ${HEDGR_MINIAUDIO_INCLUDE_DIR})

target_link_libraries(${lib_target_name}
    PUBLIC
        hedgr.model.lib
        ores::logging
        imgui::imgui
        implot::implot
    PRIVATE
        ores::platform
        ores::utility
        # miniaudio's Linux backends are loaded at run time, so what it needs
        # from the host is the dynamic loader, threads and the math library
        # rather than a link against ALSA or PulseAudio.
        ${CMAKE_DL_LIBS}
        Threads::Threads
        lunasvg::lunasvg
        $<$<PLATFORM_ID:Linux>:m>
)

install(TARGETS ${lib_target_name} LIBRARY DESTINATION lib)

# Copy the generated icon sheet to the publish directory alongside the library,
# the way hedgr.imgui publishes its background image, so a run out of the build
# tree resolves icons through the same <exe>/../share/<component> rule that an
# installed run uses. The sheet is regenerated from the icon table by
# build/scripts/build-fluent-icons.py, and it is committed so the build needs no
# rasteriser.
set(icon_sheet "${CMAKE_SOURCE_DIR}/assets/fonts/fluent-icons.pgm")
add_custom_command(TARGET ${lib_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${icon_sheet}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/fluent-icons.pgm"
    COMMENT "Copying the icon sheet to publish/share/hedgr.ui/")

install(FILES ${icon_sheet} DESTINATION share/hedgr.ui)

# Copy the per-environment instance icons to the publish directory too, so a
# deployed bundle can draw an environment's picture on a host that has no
# checkout beside it. They go in an instances subdirectory rather than beside
# the sheet, because a reader can then tell the one sheet the loader names from
# the ten files it looks up by environment label. The list is written out
# rather than globbed, so a file that goes missing fails at configure time
# instead of quietly shipping nine icons.
set(instance_icon_dir "${CMAKE_SOURCE_DIR}/assets/instances")
set(instance_icons
    "${instance_icon_dir}/brave_hopper.png"
    "${instance_icon_dir}/bright_faraday.png"
    "${instance_icon_dir}/clever_dijkstra.png"
    "${instance_icon_dir}/eager_maxwell.png"
    "${instance_icon_dir}/festive_dijkstra.png"
    "${instance_icon_dir}/jolly_knuth.png"
    "${instance_icon_dir}/merry_newton.png"
    "${instance_icon_dir}/prime_origin.png"
    "${instance_icon_dir}/solid_dirac.png"
    "${instance_icon_dir}/swift_curie.png")
add_custom_command(TARGET ${lib_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/instances"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${instance_icons}
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/instances"
    COMMENT "Copying the instance icons to publish/share/hedgr.ui/instances/")

install(FILES ${instance_icons} DESTINATION share/hedgr.ui/instances)

# Copy the splash artwork to the publish directory, for the same reason the icon
# sheet and the instance pictures go there: the deployed host has no checkout
# beside it, and a splash whose image is missing draws as an empty box. It lives
# in a branding subdirectory so a reader can tell it apart from the sheet and
# the per-environment illustrations.
set(splash_image "${CMAKE_SOURCE_DIR}/assets/branding/ore-studio-splash.png")
add_custom_command(TARGET ${lib_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/branding"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${splash_image}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/branding/ore-studio-splash.png"
    COMMENT "Copying the splash image to publish/share/hedgr.ui/branding/")

install(FILES "${splash_image}" DESTINATION share/hedgr.ui/branding)

# Copy the curated sound pack to the publish directory, for the same reason the
# icon sheet goes there: a deployed bundle resolves it through the
# <exe>/../share/<component> rule with no checkout beside it. The list is
# written out rather than globbed, so a missing file fails at configure time
# and the licence text ships beside the sounds it covers.
set(sound_dir "${CMAKE_SOURCE_DIR}/assets/sounds")
set(sounds
    "${sound_dir}/click1.wav"
    "${sound_dir}/confirmation_001.wav"
    "${sound_dir}/confirmation_002.wav"
    "${sound_dir}/error_001.wav"
    "${sound_dir}/error_002.wav"
    "${sound_dir}/bong_001.wav")
add_custom_command(TARGET ${lib_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${sounds}
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${sound_dir}/LICENSE-Kenney.txt"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../share/hedgr.ui/sounds/LICENSE-Kenney.txt"
    COMMENT "Copying the sound pack to publish/share/hedgr.ui/sounds/")

install(FILES ${sounds} DESTINATION share/hedgr.ui/sounds)
install(FILES "${sound_dir}/LICENSE-Kenney.txt" DESTINATION share/hedgr.ui/sounds)
