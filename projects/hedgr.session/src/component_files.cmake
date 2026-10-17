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
    "market/series_label.cpp"
    "market/tick_stream.cpp"
    "transport/session_client.cpp"
    "transport/session_error.cpp"
)

# The headers are listed for the install and IDE targets.
set(HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.session/export.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.session/hedgr.session.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.session/transport/hedgr.session.transport.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.session/transport/session_account.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.session/transport/session_client.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.session/transport/session_error.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.session/transport/session_options.hpp"
)
