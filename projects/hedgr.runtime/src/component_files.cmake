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
    "manager/connection_options.cpp"
    "manager/instance_manager.cpp"
    "seed/seeder.cpp"
    "service/catalogue_service.cpp"
    "service/curve_mapping.cpp"
    "service/lock_service.cpp"
    "service/market_feed.cpp"
    "service/password_policy_service.cpp"
    "service/probe_service.cpp"
    "service/read_service.cpp"
    "service/session_service.cpp"
    "service/window_service.cpp"
    "worker/policy_worker.cpp"
    "worker/deployment_worker.cpp"
    "worker/image_map_worker.cpp"
    "worker/image_worker.cpp"
    "worker/crm_worker.cpp"
    "worker/crm_names_worker.cpp"
    "worker/market_worker.cpp"
    "worker/session_actor.cpp"
    "worker/tick_sink.cpp"
    "worker/probe_worker.cpp"
    "worker/term_structure_worker.cpp"
    "worker/read_request.cpp"
)

# The headers are listed for the install and IDE targets.
set(HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/async/executor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/async/hedgr.runtime.async.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/export.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/hedgr.runtime.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/manager/connection_options.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/manager/hedgr.runtime.manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/manager/instance_manager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/refusal.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/seed/hedgr.runtime.seed.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/seed/seeder.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/catalogue_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/curve_mapping.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/hedgr.runtime.service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/lock_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/market_feed.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/password_policy_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/pending_reads.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/probe_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/read_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/session_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/window_service.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/service/window_traits.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/hedgr.runtime.worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/policy_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/probe_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/deployment_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/image_map_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/term_structure_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/image_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/crm_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/crm_names_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/market_worker.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/tick_silence.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/session_actor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/tick_feed.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/tick_sink.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.runtime/worker/read_request.hpp"
)
