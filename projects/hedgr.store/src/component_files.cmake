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
    "crypto/secret_box.cpp"
    "repository/account_repository.cpp"
    "repository/ddl.cpp"
    "repository/folder_repository.cpp"
    "repository/instance_repository.cpp"
    "repository/label_repository.cpp"
    "repository/labelling_repository.cpp"
    "repository/master_repository.cpp"
    "repository/query.cpp"
    "repository/schema.cpp"
    "repository/window_repository.cpp"
    "sqlite/database.cpp"
    "sqlite/error.cpp"
)

# The headers are listed for the install and IDE targets.
set(HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/crypto/hedgr.store.crypto.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/crypto/secret_box.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/export.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/hedgr.store.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/account_repository.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/ddl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/entity.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/folder_repository.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/hedgr.store.repository.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/instance_repository.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/label_repository.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/labelling_repository.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/master_repository.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/query.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/schema.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/repository/window_repository.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/sqlite/database.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/sqlite/error.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.store/sqlite/hedgr.store.sqlite.hpp"
)
