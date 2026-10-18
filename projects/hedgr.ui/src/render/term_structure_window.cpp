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
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/plot_fill.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/flags.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::ui::render {

using hedgr::model::state::window_handle;

namespace {

namespace domain = hedgr::model::domain;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;
using hedgr::model::command::command;

/** @brief The date the curve was observed, in the reader's local zone. */
[[nodiscard]] std::string format_timestamp(std::int64_t at_ns) {
    const auto seconds = static_cast<std::time_t>(at_ns / 1'000'000'000);
    std::tm local{};
    localtime_r(&seconds, &local);
    char buffer[40]{};
    std::strftime(buffer, sizeof(buffer), "%d %b %Y %H:%M:%S", &local);
    return buffer;
}

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

    const auto currencies = widget::currencies_of(terms->label);
    if (!currencies.base.empty()) {
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        constexpr float flag_scale = 2.0f;
        widget::draw_flag(draw, p, currencies.base, flag_scale,
            session ? image_texture(input, *session, currencies.base) : 0);
        ImGui::Dummy(ImVec2(widget::flag_width * flag_scale + 4.0f,
            widget::flag_height * flag_scale));
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(terms->label.c_str());
    ImGui::SetItemTooltip("%s", terms->curve_key.c_str());

    if (curve != nullptr && !curve->nodes.empty()) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::TextDisabled("%zu terms", curve->nodes.size());
    }
    ImGui::NewLine();

    if (!session.has_value()) {
        ImGui::TextDisabled("Sign in to read this curve.");
    } else if (curve != nullptr && curve->observed_at_ns > 0) {
        ImGui::PushFont(icon::numeric_font());
        ImGui::TextDisabled("%s", format_timestamp(curve->observed_at_ns).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("\u00b7 %zu points", curve->nodes.size());
        const auto age_ns = input.now_ns - curve->observed_at_ns;
        const auto age_s = age_ns > 0
            ? static_cast<double>(age_ns) / 1.0e9 : 0.0;
        const ImU32 colour = age_s > 10.0
            ? theme::readiness_colour(domain::readiness::bootstrapping)
            : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        ImGui::SameLine();
        ImGui::TextDisabled("\u00b7");
        ImGui::SameLine();
        if (age_s < 3.0) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour),
                "live");
        } else {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour),
                "stale %.0f s", age_s);
        }
    } else if (curve != nullptr) {
        ImGui::TextDisabled("Reading the curve...");
    }
    if (curve != nullptr && !curve->error.empty()) {
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(theme::failure_colour),
            "%s", curve->error.c_str());
    }

    const float foot_height = widget::status_foot_height(status);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float plot_height = std::max(
        available.y - foot_height - ImGui::GetStyle().ItemSpacing.y, 80.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##curve-area", ImVec2(available.x, plot_height),
            ImGuiChildFlags_None)) {
        if (curve == nullptr || (curve->nodes.empty() && curve->error.empty())) {
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
            double low = nodes.front().value;
            double high = nodes.front().value;
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                xs.push_back(static_cast<double>(i));
                ys.push_back(nodes[i].value);
                labels.push_back(nodes[i].tenor.empty()
                        ? "spot" : nodes[i].tenor.c_str());
                low = std::min(low, nodes[i].value);
                high = std::max(high, nodes[i].value);
            }
            const double span = high - low > 0.0
                ? high - low
                : std::max(std::abs(high) * 0.005, 1e-6);
            const double pad = span * 0.08;

            constexpr ImPlotFlags plot_flags = ImPlotFlags_Crosshairs
                | ImPlotFlags_NoTitle | ImPlotFlags_NoLegend
                | ImPlotFlags_NoMenus;
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding,
                ImVec2(2.0f, 2.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);
            ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                ImVec4(0.33f, 0.35f, 0.38f, 0.35f));
            ImPlot::PushStyleColor(ImPlotCol_AxisText,
                ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::PushFont(icon::numeric_font());
            if (ImPlot::BeginPlot("##curve", ImVec2(-1.0f, -1.0f),
                    plot_flags)) {
                ImPlot::SetupAxes("", "", ImPlotAxisFlags_None,
                    ImPlotAxisFlags_Opposite);
                ImPlot::SetupAxisTicks(ImAxis_X1, xs.data(),
                    static_cast<int>(xs.size()), labels.data());
                ImPlot::SetupAxisLimits(ImAxis_X1, -0.75,
                    static_cast<double>(xs.size()) - 0.25, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, low - pad, high + pad,
                    ImGuiCond_Always);
                ImPlot::SetupAxisFormat(ImAxis_Y1, "%.5f");
                const ImVec4 line_colour = ImPlot::GetColormapColor(0);
                fill_gradient_under_curve(xs.data(), ys.data(),
                    static_cast<int>(xs.size()),
                    ImGui::ColorConvertFloat4ToU32(line_colour), 0.35f);
                ImPlotSpec line;
                line.LineColor = line_colour;
                line.LineWeight = 1.5f;
                ImPlot::PlotLine("Rate", xs.data(), ys.data(),
                    static_cast<int>(xs.size()), line);
                ImPlot::EndPlot();
            }
            ImGui::PopFont();
            ImPlot::PopStyleColor(2);
            ImPlot::PopStyleVar(2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    widget::draw_status_foot(status, input.now_ns, foot);
    ImGui::PopID();
}

}
