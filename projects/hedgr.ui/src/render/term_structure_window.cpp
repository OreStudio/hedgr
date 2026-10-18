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
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::ui::render {

using hedgr::model::state::window_handle;

namespace {

namespace domain = hedgr::model::domain;
namespace icon = hedgr::ui::icon;
using hedgr::model::command::command;

[[nodiscard]] int tenor_rank(std::string_view tenor) {
    if (tenor.empty())
        return 0;
    int amount = 0;
    std::size_t i = 0;
    while (i < tenor.size()
        && std::isdigit(static_cast<unsigned char>(tenor[i]))) {
        amount = amount * 10 + (tenor[i] - '0');
        ++i;
    }
    if (amount == 0)
        amount = 1;
    if (i >= tenor.size())
        return amount;
    switch (static_cast<char>(std::toupper(
        static_cast<unsigned char>(tenor[i])))) {
    case 'D':
        return amount;
    case 'W':
        return amount * 7;
    case 'M':
        return amount * 30;
    case 'Y':
        return amount * 365;
    default:
        return amount;
    }
}

}

void render_term_structure(window_handle handle, const render_input& input,
    window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(state);
    static_cast<void>(lock);
    using hedgr::model::domain::term_structure_state;

    ImGui::PushID("term-structure");
    widget::foot_state foot;
    foot.identity = identity_text(identity_of(input, handle));

    const auto& shape = input.windows.state(handle);
    const auto* terms = std::get_if<term_structure_state>(&shape);
    if (terms == nullptr) {
        ImGui::TextWrapped("%s", "This window is not a term structure.");
        widget::draw_status_foot(status, input.now_ns, foot);
        ImGui::PopID();
        return;
    }

    const auto session = input.windows.session_of(handle);
    const auto* curve = session.has_value()
        ? input.term_structures.get(input.sessions.instance_id(*session),
            input.sessions.account_id(*session), terms->curve_key)
        : nullptr;

    if (widget::icon_button("refresh", icon::refresh,
            "Read the curve again")) {
        input.commands.push(command{
            .kind = hedgr::model::command::command_kind::refresh_window,
            .window_id = std::string{input.windows.id(handle)}});
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(terms->label.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", terms->curve_key.c_str());

    if (!session.has_value()) {
        ImGui::TextDisabled("Sign in to read this curve.");
    } else if (curve == nullptr || (curve->nodes.empty()
                   && curve->error.empty())) {
        ImGui::TextDisabled("Reading the curve...");
    } else if (!curve->error.empty()) {
        ImGui::TextWrapped("%s", curve->error.c_str());
    } else {
        auto nodes = curve->nodes;
        std::sort(nodes.begin(), nodes.end(),
            [](const auto& a, const auto& b) {
                return tenor_rank(a.tenor) < tenor_rank(b.tenor);
            });
        std::vector<double> xs;
        std::vector<double> ys;
        std::vector<const char*> labels;
        xs.reserve(nodes.size());
        ys.reserve(nodes.size());
        labels.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            xs.push_back(static_cast<double>(i));
            ys.push_back(nodes[i].value);
            labels.push_back(nodes[i].tenor.empty() ? "spot"
                                                    : nodes[i].tenor.c_str());
        }
        if (ImPlot::BeginPlot("##curve", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Tenor", "Rate");
            ImPlot::SetupAxisTicks(ImAxis_X1, xs.data(),
                static_cast<int>(xs.size()), labels.data());
            ImPlot::PlotLine("Rate", xs.data(), ys.data(),
                static_cast<int>(xs.size()));
            ImPlot::EndPlot();
        }
        ImGui::TextDisabled("%zu terms from member series.", nodes.size());
    }

    widget::draw_status_foot(status, input.now_ns, foot);
    ImGui::PopID();
}

}
