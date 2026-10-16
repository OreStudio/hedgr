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
#include "hedgr.imgui/app/application.hpp"

#include "hedgr.imgui/app/capture_input.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/capture.hpp"
#include "ores.logging/make_logger.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <stb_image.h>

#include <boost/log/trivial.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::imgui::app {

/*
 * At namespace scope, not inside lg(): a using-directive in a function body
 * does not reach the BOOST_LOG_SEV call sites in the functions that use it.
 */
using namespace ores::logging;

namespace {

std::string_view logger_name = "hedgr.imgui.app.application";

/*
 * One identity the shell can match a running window against. The Wayland app
 * id, the two X11 WM_CLASS fields and the desktop file's StartupWMClass all
 * have to name this same string, because the shell looks the window up by one
 * and the launcher up by another, and a mismatch leaves it with nothing to
 * attach the window to, which is how the client came to be listed as unknown.
 * The value reads as a reverse DNS name rather than a title because that is
 * what a Wayland app id and a desktop file id are expected to look like.
 */
constexpr const char* window_identity = "org.hedgr.Hedgr";

auto& lg() {
    static auto instance = make_logger(logger_name);
    return instance;
}

void glfw_error_callback(int code, const char* description) {
    /* Named code, not error: a parameter called error shadows the severity. */
    BOOST_LOG_SEV(lg(), error) << "GLFW error " << code << ": " << description;
}

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}

application::application(config::options cfg, std::filesystem::path capture_path,
    int capture_frame)
    : cfg_(std::move(cfg)), capture_path_(std::move(capture_path)),
      capture_frame_(capture_frame) {}

application::~application() = default;

void application::run() {
    BOOST_LOG_SEV(lg(), info) << "Starting Hedgr.";

    /*
     * The configuration describes the instance to seed on a first run. After the
     * store has an instance of its own it is authoritative, so this is read but
     * usually ignored.
     */
    session::transport::session_options seed;
    if (cfg_.connection) {
        seed.nats_url = cfg_.connection->url;
        seed.subject_prefix = cfg_.connection->subject_prefix;
        seed.tls_ca = cfg_.connection->tls_ca_cert;
        seed.tls_cert = cfg_.connection->tls_client_cert;
        seed.tls_key = cfg_.connection->tls_client_key;
    }

    if (auto started = manager_.start(cfg_.database_path, seed); !started) {
        BOOST_LOG_SEV(lg(), error)
            << "Could not open the store: " << started.error();
    }

    /*
     * Armed before the first frame, so an unattended launch unlocks on the
     * frame the prompt is first drawn rather than after a visible pause.
     */
    if (!cfg_.master_password.empty())
        workspace_.unlock_with(cfg_.master_password);

    /*
     * A capture can ask for pieces of interface scratch to be set, because some
     * of what these windows show is reached by a click and a capture is not a
     * person. The variables are read here and nowhere else, so nothing below
     * this line knows whether it is being captured.
     */
    if (const char* folder = std::getenv("HEDGR_CAPTURE_SELECT_FOLDER"))
        ui::render::select_folder_for_capture(folder);
    if (const char* selected = std::getenv("HEDGR_CAPTURE_SELECT_INSTANCE");
        selected != nullptr && *selected != '\0')
        ui::render::select_instance_for_capture(selected);
    if (const char* editing = std::getenv("HEDGR_CAPTURE_EDIT_INSTANCE");
        editing != nullptr && *editing != '\0')
        ui::render::open_instance_edit_for_capture(editing);

    /*
     * A capture has no keyboard, so the frame that shows a changed field needs
     * the draft written for it. The pair is read together, because a value with
     * no field names nothing to write it into.
     */
    if (const char* field = std::getenv("HEDGR_CAPTURE_EDIT_FIELD");
        field != nullptr && *field != '\0') {
        const char* value = std::getenv("HEDGR_CAPTURE_EDIT_VALUE");
        ui::render::change_instance_field_for_capture(field,
            value != nullptr ? value : "");
    }

    /*
     * A capture can also be driven, not only described: the script names rows a
     * person would click and this makes the browser drive those clicks onto its
     * own hit boxes. It is read here, once, before any frame is drawn, because
     * a capture that cannot interact has only ever been able to inspect what
     * the first frame happens to contain, which is how a broken tree shipped.
     */
    ui::widget::start_capture_run();
    auto* window = initialise_graphics();
    if (window == nullptr) {
        BOOST_LOG_SEV(lg(), error) << "Failed to initialise graphics.";
        return;
    }

    /*
     * A hidden capture window is already created at the size fullscreen would
     * have given it, and a fullscreen request would need the shell role it
     * does not have, so the capture skips the call rather than have GLFW log a
     * failure for a request that cannot land.
     */
    if (cfg_.fullscreen && capture_path_.empty())
        toggle_fullscreen(window);

    render_loop(window);

    if (auto saved = manager_.save_windows(); !saved)
        BOOST_LOG_SEV(lg(), warn)
            << "Could not save the window wall: " << saved.error();
    manager_.stop();
    shutdown_graphics(window);

    BOOST_LOG_SEV(lg(), info) << "Hedgr finished.";
}

GLFWwindow* application::initialise_graphics() {
    glfwSetErrorCallback(glfw_error_callback);

    /*
     * GLFW is built with both the X11 and the Wayland backend. Prefer Wayland
     * when the session offers one, since that is the session's own display
     * server rather than a compatibility layer; if it cannot be reached, let
     * GLFW choose among whatever remains.
     */
#if defined(GLFW_PLATFORM_WAYLAND)
    const bool prefer_wayland = std::getenv("WAYLAND_DISPLAY") != nullptr;
    if (prefer_wayland)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    bool initialised = glfwInit() == GLFW_TRUE;

#if defined(GLFW_PLATFORM_WAYLAND)
    if (!initialised && prefer_wayland) {
        BOOST_LOG_SEV(lg(), warn) << "Wayland initialisation failed; falling back to X11.";
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
        initialised = glfwInit() == GLFW_TRUE;
    }
#endif

    if (!initialised) {
        BOOST_LOG_SEV(lg(), error) << "Failed to initialise GLFW.";
        return nullptr;
    }

#if defined(IMGUI_IMPL_OPENGL_ES2)
    glsl_version_ = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    glsl_version_ = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    glsl_version_ = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    /*
     * All three are string hints, so they go through the string entry point
     * rather than glfwWindowHint, which takes an int and would read the
     * pointer as one. They are set on every backend rather than behind a
     * platform test, because GLFW keeps a hint it has no use for and ignores
     * it, and setting them unconditionally keeps the one identity in one place
     * instead of letting a build that prefers Wayland forget the X11 half of
     * the pair.
     */
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, window_identity);
    glfwWindowHintString(GLFW_X11_CLASS_NAME, window_identity);
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, window_identity);

    /*
     * A capture exists to write one frame and leave, and nobody is watching
     * while that frame is drawn. A hidden window is one the compositor never
     * maps, so the operator's own desktop keeps its focus and never shows a
     * client that is only there to be photographed. An ordinary run leaves the
     * hint alone because a person is using the window.
     */
    const bool capturing = !capture_path_.empty();
    if (capturing)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    windowed_width_ = 1600;
    windowed_height_ = 900;
    /*
     * A capture used to reach the size of its frame by going fullscreen, and a
     * hidden window cannot, because GLFW puts the fullscreen request on a
     * shell role that only a shown window creates. The same size is therefore
     * asked for at creation, from the mode fullscreen would have used, so the
     * frame keeps the dimensions every check in this project reads.
     */
    if (capturing && cfg_.fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode =
            monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (mode != nullptr) {
            windowed_width_ = mode->width;
            windowed_height_ = mode->height;
        }
    }

    GLFWwindow* window =
        glfwCreateWindow(windowed_width_, windowed_height_, "Hedgr", nullptr, nullptr);
    if (window == nullptr) {
        BOOST_LOG_SEV(lg(), error) << "Failed to create GLFW window.";
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        BOOST_LOG_SEV(lg(), error) << "Failed to initialise OpenGL loader.";
        glfwDestroyWindow(window);
        glfwTerminate();
        return nullptr;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    /*
     * ImGui writes a window's position back here when the process leaves, so
     * this path is where a layout lives and a capture that wants the same
     * arrangement twice has to own it. Named through the environment because a
     * capture runs from a directory it is given, and the file beside the binary
     * is one session's arrangement rather than a fixture.
     */
    ini_path_ = "imgui.ini";
    if (const char* ini = std::getenv("HEDGR_IMGUI_INI");
        ini != nullptr && *ini != '\0')
        ini_path_ = ini;
    io.IniFilename = ini_path_.c_str();

    ui::theme::apply_theme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version_.c_str());

    /*
     * After the backend, because the atlas registers its icons as custom
     * rectangles and only a backend that announces texture updates hands those
     * pixels to the GPU. Loaded first, the rectangles would be written into a
     * texture the renderer never uploads and every glyph would draw blank.
     */
    ui::icon::load_ui_fonts(15.0f);

    /*
     * After both backends, because it replaces the GLFW callback the backend
     * installed. The result is used rather than discarded: a capture is the
     * only run it changes, and a reader of this line is owed that answer.
     */
    static_cast<void>(install_capture_input(window));

    load_background_texture();
    load_instance_icon_textures();

    BOOST_LOG_SEV(lg(), info) << "Graphics initialised successfully.";
    return window;
}

void application::load_background_texture() {
    namespace fs = std::filesystem;
    const auto exe_path = fs::read_symlink("/proc/self/exe");
    const auto image_path =
        exe_path.parent_path().parent_path() / "share" / "hedgr.imgui" / "background.jpeg";

    if (!fs::exists(image_path)) {
        BOOST_LOG_SEV(lg(), warn) << "Background image not found: " << image_path;
        return;
    }

    int channels = 0;
    unsigned char* data =
        stbi_load(image_path.c_str(), &background_width_, &background_height_, &channels, 4);
    if (data == nullptr) {
        BOOST_LOG_SEV(lg(), warn)
            << "Failed to load background image: " << stbi_failure_reason();
        return;
    }

    glGenTextures(1, &background_texture_);
    glBindTexture(GL_TEXTURE_2D, background_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, background_width_, background_height_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    BOOST_LOG_SEV(lg(), info)
        << "Loaded background image: " << background_width_ << "x" << background_height_;
}

void application::load_instance_icon_textures() {
    namespace fs = std::filesystem;

    /*
     * The directory is walked rather than a list of keys being read from the
     * store, because this runs before the store is open and a bar has to be
     * able to draw every illustration the install shipped. The decode and the
     * upload happen once each here, which is what keeps a frame that draws ten
     * entries reading a table rather than ten files.
     */
    const auto directory = ui::icon::instance_icon_directory();
    if (directory.empty()) {
        BOOST_LOG_SEV(lg(), warn)
            << "Instance icon directory not found; no illustrations will be drawn.";
        return;
    }

    /*
     * The directory travels with the pictures, because the details pane shows
     * the operator where to drop a new file and the answer has to be the folder
     * this walk actually read rather than whichever candidate a later lookup
     * happens to resolve.
     */
    instance_icons_.directory = directory.string();

    std::error_code ec;
    const fs::directory_iterator entries(directory, ec);
    if (ec) {
        BOOST_LOG_SEV(lg(), warn)
            << "Could not read the instance icon directory: " << ec.message();
        return;
    }

    for (const auto& entry : entries) {
        if (!entry.is_regular_file() || entry.path().extension() != ".png")
            continue;

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data =
            stbi_load(entry.path().c_str(), &width, &height, &channels, 4);
        if (data == nullptr) {
            /*
             * One unreadable asset does not cost the others. The entry simply
             * has no texture and the interface draws its own fallback for it,
             * which is the same thing it does for an instance with no key.
             */
            BOOST_LOG_SEV(lg(), warn) << "Failed to load instance icon "
                << entry.path() << ": " << stbi_failure_reason();
            continue;
        }

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);

        instance_icons_.entries.push_back(
            ui::render::instance_icon_textures::entry{
                .key = entry.path().stem().string(),
                .texture = texture,
            });
    }

    BOOST_LOG_SEV(lg(), info) << "Loaded " << instance_icons_.entries.size()
                              << " instance icon(s).";
}

void application::release_instance_icon_textures() {
    for (auto& row : instance_icons_.entries) {
        if (row.texture == 0)
            continue;
        const auto texture = static_cast<unsigned int>(row.texture);
        glDeleteTextures(1, &texture);
        row.texture = 0;
    }
    instance_icons_.entries.clear();
}

void application::toggle_fullscreen(GLFWwindow* window) {
    fullscreen_ = !fullscreen_;
    if (fullscreen_) {
        glfwGetWindowPos(window, &windowed_x_, &windowed_y_);
        glfwGetWindowSize(window, &windowed_width_, &windowed_height_);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor != nullptr && mode != nullptr) {
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                                 mode->refreshRate);
        }
        return;
    }
    glfwSetWindowMonitor(window, nullptr, windowed_x_, windowed_y_, windowed_width_,
                         windowed_height_, 0);
}

void application::capture_frame(GLFWwindow* window) {
    int display_w = 0;
    int display_h = 0;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    if (display_w <= 0 || display_h <= 0)
        return;

    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(display_w) * static_cast<std::size_t>(display_h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, display_w, display_h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    namespace fs = std::filesystem;
    if (capture_path_.has_parent_path())
        fs::create_directories(capture_path_.parent_path());

    std::ofstream out(capture_path_, std::ios::binary);
    if (!out) {
        BOOST_LOG_SEV(lg(), error) << "Could not open capture file: " << capture_path_;
        return;
    }
    out << "P6\n" << display_w << " " << display_h << "\n255\n";
    for (int y = display_h - 1; y >= 0; --y) {
        out.write(reinterpret_cast<const char*>(
                      pixels.data() + static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(display_w) * 3),
                  static_cast<std::streamsize>(display_w) * 3);
    }
    BOOST_LOG_SEV(lg(), info) << "Captured frame to " << capture_path_;
}

void application::render_loop(GLFWwindow* window) {
    /*
     * A hidden Wayland window is never mapped, so the compositor never sends
     * the frame callbacks a visible swap waits on and the loop would run as
     * fast as it can draw. A capture counts frames rather than seconds because
     * a surface behind the store's key derivation is not ready at the usual
     * eighth frame, so the interval a swap would have waited out is waited out
     * here instead, and a capture frame keeps the place in the run it was
     * chosen for. An ordinary run leaves the pacing to the swap.
     */
    double capture_interval = 0.0;
    if (!capture_path_.empty()) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode =
            monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        const int refresh =
            mode != nullptr && mode->refreshRate > 0 ? mode->refreshRate : 60;
        capture_interval = 1.0 / static_cast<double>(refresh);
    }

    while (glfwWindowShouldClose(window) == 0 && !exit_requested_) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        /*
         * A capture's gesture is queued before the frame begins, because ImGui
         * drains its input queue inside NewFrame: a gesture added after this
         * point belongs to the frame after this one. The frame boundary is here
         * rather than in a browser because a frame holds several browsers, and
         * a gesture queued once per browser would be queued several times.
         *
         * Both calls are inert unless the run was given a script.
         */
        const std::uint32_t frame = ImGui::GetFrameCount() + 1;
        ui::widget::begin_capture_frame(frame);
        ui::widget::drive_capture_steps(frame);

        /*
         * The clipboard a chord wrote is read at both ends of the frame. The
         * platform backend may publish the selection as the frame renders or
         * only when the window system is next polled, and a read that only
         * happened after the render would miss the second case. Reading a
         * value twice costs nothing because only a change is recorded.
         */
        ui::widget::record_capture_clipboard();

        ImGui::NewFrame();
        ui::widget::hold_capture_pointer();

        if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
            toggle_fullscreen(window);

        /*
         * The runtime applies the commands the previous frame pushed and then
         * the events the workers produced, so this frame draws the result of
         * both without the interface having touched a table.
         */
        manager_.pump();

        const ui::render::render_input input{
            .instances = manager_.instances(),
            .accounts = manager_.accounts(),
            .folders = manager_.folders(),
            .labels = manager_.labels(),
            .labellings = manager_.labellings(),
            .sessions = manager_.sessions(),
            .probes = manager_.probes(),
            .password_policy = manager_.password_policy(),
            .windows = manager_.windows(),
            .shortcuts = manager_.shortcuts(),
            .commands = manager_.commands(),
            .store_lock = manager_.store_lock(),
            .unlocking = manager_.is_unlocking(),
            .lock_message = manager_.lock_error(),
            .now_ns = now_ns(),
            .background_texture = background_texture_,
            .startup_failure = manager_.startup_failure(),
            .runtime_error = manager_.last_error(),
            .instance_icons = &instance_icons_,
        };
        workspace_.render(input);

        /*
         * The copy a frame made is read here, after the frame has been drawn
         * and before the next poll of the window system. ImGui writes the
         * clipboard as it renders the selected input, and under Wayland the
         * compositor can take the selection back when it next polls, so a read
         * left to the top of the next frame can miss the run's own value and
         * find the desk's. Inert without a script.
         */
        ui::widget::record_capture_clipboard();

        /*
         * The frame is sealed after it is submitted, which is what a gesture of
         * a later frame resolves against: a rectangle from a frame nobody saw
         * the end of is not a rectangle an operator clicked.
         */
        ui::widget::end_capture_frame();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        /*
         * One frame dump, then out. Frame eight rather than one so the layout
         * has settled and ImGui has sized every window.
         */
        if (!capture_path_.empty()
            && ImGui::GetFrameCount() == static_cast<unsigned int>(capture_frame_)) {
            /* Read the back buffer before it is swapped away, not after. */
            capture_frame(window);
            glfwSwapBuffers(window);
            exit_requested_ = true;
            continue;
        }

        glfwSwapBuffers(window);

        if (capture_interval > 0.0)
            glfwWaitEventsTimeout(capture_interval);
    }
}

void application::shutdown_graphics(GLFWwindow* window) {
    if (background_texture_ != 0)
        glDeleteTextures(1, &background_texture_);
    release_instance_icon_textures();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

}
