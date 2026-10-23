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
#ifndef HEDGR_UI_TESTS_HEADLESS_DESK_HPP
#define HEDGR_UI_TESTS_HEADLESS_DESK_HPP

#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

/*
 * Frames drawn without a window: an ImGui context whose renderer is declared
 * to take textures itself, so the font atlas needs no backend and nothing is
 * ever uploaded. That is enough to run a window's whole frame path and count
 * what it does.
 */

namespace hedgr::testing {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;

/** @brief A texture source that hands out one texture and counts the asks. */
struct counting_textures final : render::image_texture_source {
    std::size_t gets = 0;

    ImTextureID get(std::string_view, std::string_view, std::string_view,
        const std::vector<std::uint8_t>&) override {
        ++gets;
        return static_cast<ImTextureID>(1);
    }

    /** @brief What the last SVG was asked for, so a test can read the size. */
    std::size_t svg_gets = 0;
    int svg_width = 0;
    int svg_height = 0;
    std::string svg_id;

    ImTextureID svg(std::string_view image_id, std::string_view, int width,
        int height) override {
        ++svg_gets;
        svg_id = image_id;
        svg_width = width;
        svg_height = height;
        return static_cast<ImTextureID>(7);
    }
};

/** @brief One ImGui and ImPlot context for the life of a test. */
struct headless_imgui final {
    headless_imgui() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 800.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }
    ~headless_imgui() {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
    headless_imgui(const headless_imgui&) = delete;
    headless_imgui& operator=(const headless_imgui&) = delete;
};

/** @brief Every table a frame borrows, with one signed-in session. */
struct desk final {
    model::state::instance_table instances;
    model::state::account_table accounts;
    model::state::folder_table folders;
    model::state::label_table labels;
    model::state::labelling_table labellings;
    model::state::session_table sessions;
    model::state::series_table market;
    model::state::term_structure_table term_structures;
    model::state::crm_table crm;
    model::state::tenant_image_map currency_images;
    model::state::image_cache images;
    model::state::probe_table probes;
    model::state::password_policy_state password_policy;
    model::state::window_table windows;
    model::state::shortcut_table shortcuts;
    model::command::command_queue commands;
    std::unordered_set<std::string> failed_images;
    std::unordered_set<std::string> pending_images;
    render::market_view_state market_view;
    counting_textures textures;
    model::state::session_handle session;

    desk() : session(sessions.insert("i", "a")) {
        sessions.set_status(session, model::domain::session_status::authenticated);
        sessions.set_token(session, model::domain::secret_string{"token"});
        sessions.set_tenant_id(session, "t");
        currency_images.set("t", {{"GBP", "img-gb"}, {"USD", "img-us"}});
        static_cast<void>(images.put("t", "img-gb", {.mime_type = "image/png",
            .data = std::vector<std::uint8_t>(4, 1)}));
        static_cast<void>(images.put("t", "img-us", {.mime_type = "image/png",
            .data = std::vector<std::uint8_t>(4, 2)}));
    }

    [[nodiscard]] render::render_input input() {
        return render::render_input{
            .instances = instances,
            .accounts = accounts,
            .folders = folders,
            .labels = labels,
            .labellings = labellings,
            .sessions = sessions,
            .market = market,
            .term_structures = term_structures,
            .crm = crm,
            .currency_images = currency_images,
            .images = images,
            .failed_images = failed_images,
            .pending_images = pending_images,
            .textures = &textures,
            .probes = probes,
            .password_policy = password_policy,
            .windows = windows,
            .shortcuts = shortcuts,
            .commands = commands,
            .store_lock = model::domain::store_lock::unlocked,
            .market_view = market_view,
        };
    }

    [[nodiscard]] model::state::window_handle open(model::domain::window_kind kind,
        model::domain::window_state state, std::string id = "w") {
        const auto handle = windows.insert(model::domain::window_spec{
            .id = std::move(id), .kind = kind, .state = std::move(state)});
        windows.bind_session(handle, session);
        windows.set_open(handle, true);
        windows.set_next_refresh_ns(handle,
            std::numeric_limits<std::int64_t>::max());
        return handle;
    }
};

/** @brief Draws one frame of @p draw inside a fixed window. */
template<class Draw>
inline void frame(Draw&& draw) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 700.0f));
    ImGui::Begin("frame");
    draw();
    ImGui::End();
    ImGui::Render();
}

/**
 * @brief Draws one frame of @p draw inside a window docked beside another.
 *
 * The same "frame" window, 1000 by 700, but split off a dock node the dock
 * builder made on the first call, which is where an operator's drag ends.
 */
template<class Draw>
inline void docked_frame(Draw&& draw) {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::NewFrame();
    const ImGuiID root = ImGui::GetID("test-dock");
    if (ImGui::DockBuilderGetNode(root) == nullptr) {
        ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodePos(root, ImVec2(0.0f, 0.0f));
        ImGui::DockBuilderSetNodeSize(root, ImVec2(1250.0f, 700.0f));
        ImGuiID right = 0;
        const ImGuiID left = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left,
            0.8f, nullptr, &right);
        ImGui::DockBuilderDockWindow("frame", left);
        ImGui::DockBuilderDockWindow("neighbour", right);
        ImGui::DockBuilderFinish(root);
    }
    ImGui::Begin("frame");
    draw();
    ImGui::End();
    ImGui::Begin("neighbour");
    ImGui::End();
    ImGui::Render();
}

}

#endif
