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
    "icon/fonts.cpp"
    "render/accounts_window.cpp"
    "render/chart_window.cpp"
    "render/grid_window.cpp"
    "render/instances_window.cpp"
    "render/notice_window.cpp"
    "render/panels.cpp"
    "render/renderer.cpp"
    "render/shortcut_bar.cpp"
    "render/splash.cpp"
    "render/workspace.cpp"
    "render/workspace_window.cpp"
    "sound/pack.cpp"
    "sound/player.cpp"
    "theme/theme.cpp"
    "../third_party/imcoolbar/ImCoolBar.cpp"
    "widget/browser.cpp"
    "widget/capture.cpp"
    "widget/cards.cpp"
    "widget/label_pill.cpp"
    "widget/panel_defaults.cpp"
    "widget/panels.cpp"
    "widget/password_meter.cpp"
    "widget/status_bar.cpp"
    "widget/status_foot.cpp"
)

# The headers are listed for the install and IDE targets.
set(HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/export.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/hedgr.ui.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/icon/fonts.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/icon/hedgr.ui.icon.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/icon/icons.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/render/hedgr.ui.render.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/render/panels.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/render/renderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/render/shortcut_bar.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/render/splash.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/render/windows.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/render/workspace.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/sound/pack.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/sound/player.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/theme/hedgr.ui.theme.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/theme/theme.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/browser.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/browser_state.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/capture.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/cards.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/controls.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/hedgr.ui.widget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/label_pill.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/panel_defaults.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/panels.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/password_meter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/status_bar.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/hedgr.ui/widget/status_foot.hpp"
)
