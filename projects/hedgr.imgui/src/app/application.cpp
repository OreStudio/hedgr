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
#include "imgui.h"
#include "imgui_stdlib.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GL/glew.h>
#include <GLFW/glfw3.h>

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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version_);

    BOOST_LOG_SEV(lg(), info) << "Graphics initialised successfully.";
    return window;
}

void application::render_loop(GLFWwindow* window) {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (show_demo_window_)
            ImGui::ShowDemoWindow(&show_demo_window_);

        render_control_panel();

        if (show_another_window_) {
            ImGui::Begin("Another Window", &show_another_window_);
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window_ = false;
            ImGui::End();
        }

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
    static float f = 0.0f;
    static int counter = 0;

    ImGui::Begin("Hedgr Control Panel");

    ImGui::Text("Server Connection Status: %s", connection_status_.c_str());

    ImGui::InputText("Server Address", &ui_server_address_);
    ImGui::InputText("Server Port", &ui_server_port_);

    if (session_.is_connected()) {
        if (ImGui::Button("Disconnect")) {
            disconnect();
        }
    } else {
        if (ImGui::Button("Connect") && !connecting_) {
            ores::comms::net::client_options opts;
            opts.host = ui_server_address_;
            opts.port = static_cast<std::uint16_t>(
                std::stoi(ui_server_port_));
            opts.client_identifier = "hedgr-imgui";
            connect(opts);
        }
    }

    ImGui::Separator();

    ImGui::Checkbox("Demo Window", &show_demo_window_);
    ImGui::Checkbox("Another Window", &show_another_window_);

    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
    ImGui::ColorEdit3("clear color", clear_color_);

    if (ImGui::Button("Button"))
        counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / io.Framerate, io.Framerate);
    ImGui::End();
}

void application::connect(const ores::comms::net::client_options& opts) {
    connecting_ = true;
    connection_status_ = "Connecting...";

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

void application::disconnect() {
    if (session_.is_connected()) {
        BOOST_LOG_SEV(lg(), info) << "Disconnecting.";
        session_.disconnect();
        connection_status_ = "Disconnected";
    }
}

void application::shutdown_graphics(GLFWwindow* window) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    BOOST_LOG_SEV(lg(), info) << "Graphics shut down.";
}

}
