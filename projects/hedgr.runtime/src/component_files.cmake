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
    "async/executor.cpp"
    "manager/instance_manager.cpp"
    "worker/session_worker.cpp"
)

# The headers are listed for the install and IDE targets.
set(HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/async/executor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/async/hedgr.runtime.async.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/export.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/hedgr.runtime.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/manager/hedgr.runtime.manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/manager/instance_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/hedgr.runtime.worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/session_worker.hpp"
)
