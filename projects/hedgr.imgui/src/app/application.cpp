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
#include "hedgr.imgui/app/application.hpp"

#include <cstdio>
#include <thread>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "ores.iam/client/auth_helpers.hpp"

namespace {

void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

}

namespace hedgr::imgui::app {

using namespace ores::logging;

application::application(
    std::optional<ores::comms::net::client_options> connection_config)
    : connection_config_(std::move(connection_config)) {
    if (connection_config_) {
        ui_server_address_ = connection_config_->host;
        ui_server_port_ = std::to_string(connection_config_->port);
    }
}

void application::run() {
    BOOST_LOG_SEV(lg(), info) << "Starting Hedgr ImGui application.";

    auto* window = initialise_graphics();
    if (!window) {
        BOOST_LOG_SEV(lg(), error) << "Failed to initialise graphics.";
        return;
    }

    if (connection_config_) {
        BOOST_LOG_SEV(lg(), info) << "Auto-connecting to "
            << connection_config_->host << ":"
            << connection_config_->port;
        connect(*connection_config_);
    }

    render_loop(window);
    disconnect();
    shutdown_graphics(window);

    BOOST_LOG_SEV(lg(), info) << "Hedgr ImGui application finished.";
}

GLFWwindow* application::initialise_graphics() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        BOOST_LOG_SEV(lg(), error) << "Failed to initialise GLFW.";
        return nullptr;
    }

#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    glsl_version_ = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    glsl_version_ = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    // GL 3.0 + GLSL 130
    glsl_version_ = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720,
        "Hedgr - Remote UI", nullptr, nullptr);
    if (window == nullptr) {
        BOOST_LOG_SEV(lg(), error) << "Failed to create GLFW window.";
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const bool err = glewInit() != GLEW_OK;
    if (err) {
        BOOST_LOG_SEV(lg(), error) << "Failed to initialise OpenGL loader.";
        glfwDestroyWindow(window);
        glfwTerminate();
        return nullptr;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    setup_theme();
    load_fonts();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version_);

    load_background_texture();

    BOOST_LOG_SEV(lg(), info) << "Graphics initialised successfully.";
    return window;
}

void application::setup_theme() {
    auto& style = ImGui::GetStyle();
    auto* colors = style.Colors;

    /*
     * Geometry: compact padding, subtle rounding, thin borders.
     */
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;
    style.SeparatorTextBorderSize = 1.0f;

    /*
     * Dark trading terminal palette.
     *
     *   bg-base     #0D0D0D   near-black base
     *   bg-window   #141414   window body
     *   bg-title    #1A1A1A   title bars
     *   bg-frame    #1E1E1E   input fields / frames
     *   border      #2A2A2A   subtle borders
     *   text        #D4D4D4   primary text
     *   text-dim    #808080   secondary / disabled text
     *   accent      #2D8CFF   blue accent (selection, active tabs)
     *   green       #00C853   positive / connected
     *   red         #FF1744   negative / error
     *   yellow      #FFD600   warning / in-progress
     */
    const ImVec4 bg_base(0.05f, 0.05f, 0.05f, 1.00f);
    const ImVec4 bg_window(0.08f, 0.08f, 0.08f, 0.95f);
    const ImVec4 bg_title(0.10f, 0.10f, 0.10f, 1.00f);
    const ImVec4 bg_frame(0.12f, 0.12f, 0.12f, 1.00f);
    const ImVec4 bg_frame_hover(0.16f, 0.16f, 0.16f, 1.00f);
    const ImVec4 border(0.16f, 0.16f, 0.16f, 1.00f);
    const ImVec4 text(0.83f, 0.83f, 0.83f, 1.00f);
    const ImVec4 text_dim(0.50f, 0.50f, 0.50f, 1.00f);
    const ImVec4 accent(0.18f, 0.55f, 1.00f, 1.00f);
    const ImVec4 accent_hover(0.24f, 0.62f, 1.00f, 1.00f);
    const ImVec4 accent_active(0.12f, 0.44f, 0.87f, 1.00f);

    colors[ImGuiCol_Text]                   = text;
    colors[ImGuiCol_TextDisabled]           = text_dim;
    colors[ImGuiCol_WindowBg]               = bg_window;
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.98f);
    colors[ImGuiCol_Border]                 = border;
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = bg_frame;
    colors[ImGuiCol_FrameBgHovered]         = bg_frame_hover;
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBg]                = bg_title;
    colors[ImGuiCol_TitleBgActive]          = bg_title;
    colors[ImGuiCol_TitleBgCollapsed]       = bg_base;
    colors[ImGuiCol_MenuBarBg]              = bg_title;
    colors[ImGuiCol_ScrollbarBg]            = bg_base;
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_CheckMark]              = accent;
    colors[ImGuiCol_SliderGrab]             = accent;
    colors[ImGuiCol_SliderGrabActive]       = accent_active;
    colors[ImGuiCol_Button]                 = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = accent_active;
    colors[ImGuiCol_Header]                 = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_Separator]              = border;
    colors[ImGuiCol_SeparatorHovered]       = accent;
    colors[ImGuiCol_SeparatorActive]        = accent_active;
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.14f, 0.14f, 0.14f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]      = accent_hover;
    colors[ImGuiCol_ResizeGripActive]       = accent_active;
    colors[ImGuiCol_Tab]                    = bg_title;
    colors[ImGuiCol_TabHovered]             = accent_hover;
    colors[ImGuiCol_TabSelected]            = accent;
    colors[ImGuiCol_TabDimmed]              = bg_base;
    colors[ImGuiCol_TabDimmedSelected]      = bg_title;
    colors[ImGuiCol_TableHeaderBg]          = bg_title;
    colors[ImGuiCol_TableBorderStrong]      = border;
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.05f, 0.05f, 0.05f, 0.50f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.18f, 0.55f, 1.00f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = accent;
    colors[ImGuiCol_NavCursor]              = accent;
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.58f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.58f);
}

void application::load_fonts() {
    namespace fs = std::filesystem;
    ImGuiIO& io = ImGui::GetIO();

    /*
     * Font paths in order of preference. The finance dashboard style uses
     * a compact condensed sans-serif for UI and monospace for data.
     */
    const std::vector<fs::path> ui_font_candidates{
        "/usr/share/fonts/truetype/roboto/hinted/RobotoCondensed-Regular.ttf",
        "/usr/share/fonts/truetype/roboto/hinted/Roboto-Medium.ttf",
        "/usr/share/fonts/truetype/gfonts/roboto/roboto-v20-latin-regular.ttf"
    };

    const std::vector<fs::path> mono_font_candidates{
        fs::path(getenv("HOME") ? getenv("HOME") : "") /
            ".local/share/fonts/NerdFonts/RobotoMonoNerdFont-Regular.ttf",
        fs::path(getenv("HOME") ? getenv("HOME") : "") /
            ".local/share/fonts/adobe-fonts/source-code-pro/TTF/SourceCodePro-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
    };

    const float ui_font_size = 15.0f;
    const float mono_font_size = 14.0f;

    bool loaded_ui = false;
    for (const auto& path : ui_font_candidates) {
        if (fs::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path.c_str(), ui_font_size);
            BOOST_LOG_SEV(lg(), info) << "Loaded UI font: " << path;
            loaded_ui = true;
            break;
        }
    }

    for (const auto& path : mono_font_candidates) {
        if (fs::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path.c_str(), mono_font_size);
            BOOST_LOG_SEV(lg(), info) << "Loaded mono font: " << path;
            break;
        }
    }

    if (!loaded_ui) {
        BOOST_LOG_SEV(lg(), warn) << "No UI font found, using ImGui default.";
    }

    io.Fonts->Build();
}

void application::load_background_texture() {
    /*
     * Look for the background image relative to the executable:
     * <exe_dir>/../share/hedgr.imgui/background.png
     */
    namespace fs = std::filesystem;
    const auto exe_path = fs::read_symlink("/proc/self/exe");
    const auto share_dir = exe_path.parent_path().parent_path()
        / "share" / "hedgr.imgui";
    const auto image_path = share_dir / "background.png";

    if (!fs::exists(image_path)) {
        BOOST_LOG_SEV(lg(), warn)
            << "Background image not found: " << image_path;
        return;
    }

    int channels = 0;
    unsigned char* data = stbi_load(image_path.c_str(),
        &background_width_, &background_height_, &channels, 4);
    if (!data) {
        BOOST_LOG_SEV(lg(), warn)
            << "Failed to load background image: " << stbi_failure_reason();
        return;
    }

    glGenTextures(1, &background_texture_);
    glBindTexture(GL_TEXTURE_2D, background_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
        background_width_, background_height_, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    BOOST_LOG_SEV(lg(), info)
        << "Loaded background image: " << background_width_
        << "x" << background_height_;
}

void application::render_background(GLFWwindow* window) {
    if (background_texture_ == 0)
        return;

    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(
        static_cast<float>(display_w), static_cast<float>(display_h)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##Background", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Image(static_cast<ImTextureID>(
        static_cast<intptr_t>(background_texture_)),
        ImVec2(static_cast<float>(display_w),
            static_cast<float>(display_h)));
    ImGui::End();
    ImGui::PopStyleVar();
}

void application::render_loop(GLFWwindow* window) {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render_background(window);
        render_control_panel();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(
            clear_color_[0] * clear_color_[3],
            clear_color_[1] * clear_color_[3],
            clear_color_[2] * clear_color_[3],
            clear_color_[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }
}

void application::render_control_panel() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 200), ImVec2(500, 800));
    ImGui::Begin("Hedgr Control Panel");

    const ImVec4 col_green(0.00f, 0.78f, 0.33f, 1.00f);
    const ImVec4 col_red(1.00f, 0.09f, 0.27f, 1.00f);
    const ImVec4 col_yellow(1.00f, 0.84f, 0.00f, 1.00f);
    const ImVec4 col_dim(0.50f, 0.50f, 0.50f, 1.00f);

    /*
     * Connection section.
     */
    ImGui::SeparatorText("Connection");

    ImGui::Text("Status:");
    ImGui::SameLine();
    if (session_.is_connected()) {
        ImGui::TextColored(col_green, "%s", connection_status_.c_str());
    } else if (connecting_) {
        ImGui::TextColored(col_yellow, "%s", connection_status_.c_str());
    } else if (connection_status_.find("failed") != std::string::npos) {
        ImGui::TextColored(col_red, "%s", connection_status_.c_str());
    } else {
        ImGui::TextColored(col_dim, "%s", connection_status_.c_str());
    }

    if (!session_.is_connected()) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##addr", "Server address",
            &ui_server_address_);

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##port", "Port", &ui_server_port_);

        const bool busy = connecting_;
        if (busy) ImGui::BeginDisabled();

        const float button_width = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.00f, 0.55f, 0.23f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(0.00f, 0.65f, 0.28f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(0.00f, 0.45f, 0.19f, 1.00f));
        if (ImGui::Button("Connect", ImVec2(button_width, 0))) {
            ores::comms::net::client_options opts;
            opts.host = ui_server_address_;
            opts.port = static_cast<std::uint16_t>(
                std::stoi(ui_server_port_));
            opts.client_identifier = "hedgr-imgui";
            opts.verify_certificate = false; // FIXME: for now
            connect(opts);
        }
        ImGui::PopStyleColor(3);
        if (busy) ImGui::EndDisabled();
    } else {
        ImGui::TextColored(col_dim, "Server:");
        ImGui::SameLine();
        ImGui::Text("%s:%s",
            ui_server_address_.c_str(), ui_server_port_.c_str());

        const float button_width = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.60f, 0.05f, 0.10f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(0.70f, 0.08f, 0.14f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(0.50f, 0.03f, 0.08f, 1.00f));
        if (ImGui::Button("Disconnect", ImVec2(button_width, 0))) {
            disconnect();
        }
        ImGui::PopStyleColor(3);
    }

    /*
     * Login section - always visible when not logged in.
     */
    if (!session_.is_logged_in()) {
        ImGui::SeparatorText("Authentication");

        if (!login_status_.empty()) {
            const bool is_error =
                login_status_.find("failed") != std::string::npos;
            ImGui::TextColored(is_error ? col_red : col_yellow,
                "%s", login_status_.c_str());
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##user", "Username", &ui_username_);

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pass", "Password", &ui_password_,
            ImGuiInputTextFlags_Password);

        const bool can_login = session_.is_connected()
            && !logging_in_ && !ui_username_.empty()
            && !ui_password_.empty();
        if (!can_login) ImGui::BeginDisabled();

        const float button_width = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.12f, 0.44f, 0.87f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(0.18f, 0.55f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(0.08f, 0.35f, 0.70f, 1.00f));
        if (ImGui::Button("Login", ImVec2(button_width, 0))) {
            login(ui_username_, ui_password_);
        }
        ImGui::PopStyleColor(3);
        if (!can_login) ImGui::EndDisabled();
    } else {
        /*
         * Session section - shown when logged in.
         */
        ImGui::SeparatorText("Session");

        ImGui::TextColored(col_dim, "User:");
        ImGui::SameLine();
        ImGui::TextColored(col_green, "%s", session_.username().c_str());

        ImGui::TextColored(col_dim, "Email:");
        ImGui::SameLine();
        ImGui::Text("%s", session_.email().c_str());

        if (!login_status_.empty()) {
            ImGui::TextColored(col_red, "%s", login_status_.c_str());
        }

        const float button_width = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.60f, 0.05f, 0.10f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(0.70f, 0.08f, 0.14f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(0.50f, 0.03f, 0.08f, 1.00f));
        if (ImGui::Button("Logout", ImVec2(button_width, 0))) {
            logout();
        }
        ImGui::PopStyleColor(3);
    }

    /*
     * Status bar.
     */
    ImGui::Separator();
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::TextColored(col_dim, "%.1f FPS", io.Framerate);

    ImGui::End();
}

void application::connect(const ores::comms::net::client_options& opts) {
    connecting_ = true;
    connection_status_ = "Connecting...";
    login_status_.clear();

    BOOST_LOG_SEV(lg(), info) << "Connecting to " << opts.host
        << ":" << opts.port;

    std::thread([this, opts]() {
        auto result = session_.connect(opts);
        if (result) {
            connection_status_ = "Connected";
            BOOST_LOG_SEV(lg(), info) << "Connected to " << opts.host
                << ":" << opts.port;
        } else {
            connection_status_ = "Connection failed: "
                + ores::comms::net::to_string(result.error());
            BOOST_LOG_SEV(lg(), error) << "Connection failed: "
                << ores::comms::net::to_string(result.error());
        }
        connecting_ = false;
    }).detach();
}

void application::login(const std::string& username,
    const std::string& password) {
    logging_in_ = true;
    login_status_ = "Logging in...";

    BOOST_LOG_SEV(lg(), info) << "Logging in as " << username;

    std::thread([this, username, password]() {
        auto result = ores::iam::client::login(session_, username, password);
        if (result.success) {
            login_status_.clear();
            ui_password_.clear();
            BOOST_LOG_SEV(lg(), info) << "Logged in as " << result.username
                << ", tenant: " << result.tenant_name;
        } else {
            login_status_ = "Login failed: " + result.error_message;
            BOOST_LOG_SEV(lg(), error) << "Login failed: "
                << result.error_message;
        }
        logging_in_ = false;
    }).detach();
}

void application::logout() {
    BOOST_LOG_SEV(lg(), info) << "Logging out.";
    auto result = ores::iam::client::logout(session_);
    if (result.success) {
        login_status_.clear();
        connection_status_ = "Disconnected";
        BOOST_LOG_SEV(lg(), info) << "Logged out successfully.";
    } else {
        login_status_ = "Logout failed: " + result.error_message;
        BOOST_LOG_SEV(lg(), error) << "Logout failed: "
            << result.error_message;
    }
}

void application::disconnect() {
    if (session_.is_connected()) {
        BOOST_LOG_SEV(lg(), info) << "Disconnecting.";
        session_.disconnect();
        connection_status_ = "Disconnected";
        login_status_.clear();
    }
}

void application::shutdown_graphics(GLFWwindow* window) {
    if (background_texture_ != 0)
        glDeleteTextures(1, &background_texture_);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    BOOST_LOG_SEV(lg(), info) << "Graphics shut down.";
}

}
