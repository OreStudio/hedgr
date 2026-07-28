/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2025 Marco Craveiro <marco.craveiro@gmail.com>
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

#include <atomic>
#include <string>
#include <optional>
#include <filesystem>
#include "ores.logging/make_logger.hpp"
#include "ores.comms/net/client_session.hpp"
#include "ores.comms/net/client_options.hpp"

struct GLFWwindow;

namespace hedgr::imgui::app {

/**
 * @brief Owns the ImGui lifecycle and server connection state.
 */
class application final {
private:
    inline static std::string_view logger_name =
        "hedgr.imgui.app.application";

    static auto& lg() {
        using namespace ores::logging;
        static auto instance = make_logger(logger_name);
        return instance;
    }

public:
    explicit application(
        std::optional<ores::comms::net::client_options> connection_config
            = std::nullopt);

    application(const application&) = delete;
    application& operator=(const application&) = delete;

    /**
     * @brief Runs the application.
     *
     * Initialises graphics, optionally auto-connects, enters the render
     * loop, then cleans up.
     */
    void run();

private:
    GLFWwindow* initialise_graphics();
    void setup_theme();
    void load_fonts();
    void load_background_texture();
    void render_background(GLFWwindow* window);
    void render_loop(GLFWwindow* window);
    void render_control_panel();
    void connect(const ores::comms::net::client_options& opts);
    void login(const std::string& username, const std::string& password);
    void logout();
    void disconnect();
    void shutdown_graphics(GLFWwindow* window);

    std::optional<ores::comms::net::client_options> connection_config_;
    ores::comms::net::client_session session_;
    std::atomic<bool> connecting_{false};
    std::atomic<bool> logging_in_{false};
    std::string connection_status_{"Disconnected"};
    std::string login_status_;
    std::string ui_server_address_{"localhost"};
    std::string ui_server_port_{"55555"};
    std::string ui_username_;
    std::string ui_password_;
    float clear_color_[4]{0.05f, 0.05f, 0.05f, 1.00f};
    const char* glsl_version_{nullptr};
    unsigned int background_texture_{0};
    int background_width_{0};
    int background_height_{0};
};

}

#endif
