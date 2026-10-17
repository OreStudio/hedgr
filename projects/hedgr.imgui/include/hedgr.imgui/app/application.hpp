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
#include <string_view>
#include <unordered_map>
#include "hedgr.ui/render/splash.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/sound/player.hpp"
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
    /** @brief The frame a capture writes unless the environment asks later. */
    static constexpr int default_capture_frame = 8;

    /**
     * @brief Takes the configuration, the frame-dump path and when to dump.
     *
     * @param capture_path Where to write one finished frame, or empty. A dump
     *                     is how a change is inspected without a person
     *                     watching, so the process leaves once it is written.
     * @param capture_frame Which frame to write. Eight by default, which is
     *                     enough for ImGui to have sized every window. A later
     *                     frame is wanted when the surface being inspected
     *                     arrives asynchronously, such as a browser that is
     *                     hidden behind a store whose key derivation is still
     *                     running on a pool thread.
     */
    explicit application(config::options cfg,
                         std::filesystem::path capture_path = {},
                         int capture_frame = default_capture_frame);

    ~application();

    application(const application&) = delete;
    application& operator=(const application&) = delete;

    /** @brief Starts the runtime, opens the window, and loops until closed. */
    void run();

private:
    GLFWwindow* initialise_graphics();
    void shutdown_graphics(GLFWwindow* window);
    void load_background_texture();
    void load_instance_icon_textures();
    void sync_flag_textures();
    unsigned int upload_rgba(const unsigned char* data, int width, int height);
    void release_instance_icon_textures();
    void load_splash_texture();
    void render_loop(GLFWwindow* window);
    void toggle_fullscreen(GLFWwindow* window);
    void capture_frame(GLFWwindow* window);

    config::options cfg_;
    std::filesystem::path capture_path_;
    int capture_frame_ = default_capture_frame;
    /*
     * Held rather than left as a literal, because ImGui stores the pointer
     * rather than copying the name: a temporary would leave it reading freed
     * memory by the time the frame loop saves the layout.
     */
    std::string ini_path_;
    runtime::manager::instance_manager manager_;
    ui::render::workspace workspace_;
    /*
     * The sound player owns its own thread and device, and the frame loop only
     * hands it names. It is the one subscriber to the signal bus today; see
     * ADR 8 for why sound is not special and why nothing plays on the frame
     * loop itself.
     */
    ui::sound::player sound_;
    std::string glsl_version_;
    unsigned int background_texture_ = 0;
    int background_width_ = 0;
    int background_height_ = 0;
    /*
     * The decoded instance illustrations, uploaded once. The frame reads this
     * as a lookup, so no image is ever decoded on the path that draws a frame.
     */
    ui::render::instance_icon_textures instance_icons_;
    std::unordered_map<std::string, unsigned int> flag_textures_;
    /*
     * The splash artwork, decoded and uploaded once like the background. The
     * timing is a value so the policy lives in hedgr.ui and the application
     * only says how long the run has been going and whether the store is open.
     */
    unsigned int splash_texture_ = 0;
    int splash_width_ = 0;
    int splash_height_ = 0;
    ui::render::splash_timing splash_;
    int windowed_x_ = 0;
    int windowed_y_ = 0;
    int windowed_width_ = 1280;
    int windowed_height_ = 720;
    bool fullscreen_ = false;
    bool exit_requested_ = false;
};

}

#endif
