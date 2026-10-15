/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#ifndef HEDGR_IMGUI_APP_APPLICATION_HPP
#define HEDGR_IMGUI_APP_APPLICATION_HPP

#include "hedgr.imgui/config/options.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include <filesystem>
#include <string>

struct GLFWwindow;

namespace hedgr::imgui::app {

/**
 * @brief The executable: it owns the window, the frame loop and the wiring.
 *
 * Everything else lives in a library. The application's whole job is to open a
 * GL context, hand the frame to hedgr.ui, and let hedgr.runtime apply what the
 * interface asked for. It holds no domain state of its own and makes no
 * connection of its own.
 */
class application final {
public:
    /**
     * @brief Takes the configuration and the frame-dump path.
     *
     * @param capture_path Where to write one finished frame, or empty. A dump
     *                     is how a change is inspected without a person
     *                     watching, so the process leaves once it is written.
     */
    explicit application(config::options cfg,
                         std::filesystem::path capture_path = {});

    ~application();

    application(const application&) = delete;
    application& operator=(const application&) = delete;

    /** @brief Starts the runtime, opens the window, and loops until closed. */
    void run();

private:
    GLFWwindow* initialise_graphics();
    void shutdown_graphics(GLFWwindow* window);
    void load_background_texture();
    void render_loop(GLFWwindow* window);
    void toggle_fullscreen(GLFWwindow* window);
    void capture_frame(GLFWwindow* window);

    config::options cfg_;
    std::filesystem::path capture_path_;
    runtime::manager::instance_manager manager_;
    ui::render::workspace workspace_;
    std::string glsl_version_;
    unsigned int background_texture_ = 0;
    int background_width_ = 0;
    int background_height_ = 0;
    int windowed_x_ = 0;
    int windowed_y_ = 0;
    int windowed_width_ = 1280;
    int windowed_height_ = 720;
    bool fullscreen_ = false;
    bool exit_requested_ = false;
};

}

#endif
