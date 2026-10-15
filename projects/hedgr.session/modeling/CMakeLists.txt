# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
# PlantUML is optional: the diagram target is only defined when the tool is
# present, so a machine without it still configures and builds.
find_program(PLANTUML_EXECUTABLE plantuml)
if(PLANTUML_EXECUTABLE)
    add_custom_target(generate_hedgr.session_diagram
        COMMENT "Generating PlantUML diagram for hedgr.session" VERBATIM
        COMMAND ${PLANTUML_EXECUTABLE} hedgr.session.puml
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/)
endif()
