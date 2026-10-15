# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
set(files
    "command/command.cpp"
    "domain/session.cpp"
    "domain/tier.cpp"
    "domain/window.cpp"
    "domain/window_json_io.cpp"
    "event/event.cpp"
    "state/handle.cpp"
    "state/identity_table.cpp"
    "state/instance_table.cpp"
    "state/session_table.cpp"
    "state/window_table.cpp"
)

# The headers are listed for the install and IDE targets.
set(HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/command/channel.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/command/command.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/command/hedgr.model.command.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/domain/hedgr.model.domain.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/domain/identity.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/domain/instance.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/domain/session.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/domain/tier.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/domain/window.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/domain/window_json_io.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/event/event.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/event/hedgr.model.event.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/export.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/hedgr.model.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/state/handle.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/state/hedgr.model.state.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.model/state/tables.hpp"
)
