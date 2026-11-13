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
#include "headless_desk.hpp"
#include "hedgr.model/state/tables.hpp"
#include "hedgr.ui/icon/fonts.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/render/windows.hpp"
#include "hedgr.ui/render/workspace.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/cells.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "hedgr.ui/widget/view_state.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"
#include "software_raster.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace model = hedgr::model;
namespace render = hedgr::ui::render;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::testing::desk;
using hedgr::testing::headless_imgui;
using Catch::Approx;

constexpr std::int64_t half_second = 500'000'000;
constexpr std::int64_t series_start_ns = 1'760'000'000'000'000'000;
constexpr std::size_t chart_points = 120;
constexpr std::int64_t newest_at_ns = series_start_ns
    + static_cast<std::int64_t>(chart_points - 1) * half_second;

/** @brief A themed headless context, so the state colours are the real ones. */
struct themed final {
    headless_imgui imgui;
    themed() { theme::apply_theme(); }
};

/** @brief The vertices of the window the headless frame draws into. */
std::vector<ImDrawVert> frame_vertices() {
    const ImGuiWindow* window = ImGui::FindWindowByName("frame");
    REQUIRE(window != nullptr);
    const auto& buffer = window->DrawList->VtxBuffer;
    return {buffer.begin(), buffer.end()};
}

/** @brief The vertices of every list the last frame rendered. */
std::vector<ImDrawVert> all_vertices() {
    std::vector<ImDrawVert> result;
    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists)
        result.insert(result.end(), list->VtxBuffer.begin(),
            list->VtxBuffer.end());
    return result;
}

bool has_colour(const std::vector<ImDrawVert>& vertices, ImU32 colour) {
    return std::ranges::any_of(vertices,
        [&](const ImDrawVert& v) { return v.col == colour; });
}

/** @brief Whether any vertex wears a pill or strip colour of a state tone. */
bool has_state_tone(const std::vector<ImDrawVert>& vertices) {
    for (const auto tone :
        {theme::tone::good, theme::tone::bad, theme::tone::warning}) {
        const auto pill = theme::pill_colours(tone);
        if (has_colour(vertices, pill.fill) || has_colour(vertices, pill.border)
            || has_colour(vertices, pill.text))
            return true;
    }
    return false;
}

bool has_pill(const std::vector<ImDrawVert>& vertices, theme::tone tone) {
    return has_colour(vertices, theme::pill_colours(tone).border);
}

/**
 * @brief The width of the box every solid vertex of @p colour falls in.
 *
 * An antialiased fill keeps its solid vertices half a pixel inside each edge,
 * so this reads about a pixel short of the shape.
 */
float solid_width(const std::vector<ImDrawVert>& vertices, ImU32 colour) {
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    float left = FLT_MAX;
    float right = -FLT_MAX;
    for (const auto& v : vertices) {
        if (v.col != colour || v.uv.x != white.x || v.uv.y != white.y)
            continue;
        left = std::min(left, v.pos.x);
        right = std::max(right, v.pos.x);
    }
    return right > left ? right - left : 0.0f;
}

/** @brief The common pieces every window renderer takes. */
struct window_fixture {
    themed context;
    desk desk;
    render::window_ui_state state;
    widget::status_bar status;
    render::lock_control lock;
};

/** @brief A chart over a series that moves by @p step a point. */
struct chart_fixture final : window_fixture {
    model::state::window_handle window;

    explicit chart_fixture(double step,
        const std::function<double(std::size_t)>& wobble = {}) {
        const auto series = desk.market.intern("i", "a", "s");
        for (std::size_t p = 0; p < chart_points; ++p) {
            const double value = 1.25 + step * static_cast<double>(p)
                + (wobble ? wobble(p) : 0.0);
            static_cast<void>(desk.market.append(series,
                model::domain::market_level::fine,
                model::state::series_point{
                    .at_ns = series_start_ns
                        + static_cast<std::int64_t>(p) * half_second,
                    .value = value},
                1));
        }
        window = desk.open(model::domain::window_kind::chart,
            model::domain::chart_state{.label = "FX GBP/USD",
                .series_id = "s"});
        desk.windows.set_series(window, std::span{&series, 1});
        state.market_view.chart_span = 3;
    }

    /** @brief Whether the chart is drawn into a docked window. */
    bool docked = false;

    void draw(std::int64_t wall_now_ns) {
        auto input = desk.input();
        input.wall_now_ns = wall_now_ns;
        input.now_ns = wall_now_ns;
        const auto body = [&] {
            render::render_chart(window, input, state, status, lock);
        };
        if (docked)
            hedgr::testing::docked_frame(body);
        else
            hedgr::testing::frame(body);
    }
};

struct term_fixture final : window_fixture {
    model::state::window_handle window;

    term_fixture() {
        hedgr::testing::seed_curve(desk.curves, "i", "a", "k",
            "IR GBP SONIA", 4, newest_at_ns);
        window = desk.open(model::domain::window_kind::term_structure,
            model::domain::term_structure_state{.label = "IR GBP SONIA",
                .curve_key = "k"});
        state.market_view.term_history = 4;
    }

    void draw() {
        auto input = desk.input();
        input.wall_now_ns = newest_at_ns;
        hedgr::testing::frame([&] {
            render::render_term_structure(window, input, state, status, lock);
        });
    }
};

/** @brief A published surface over seven expiries by five strikes. */
struct surface_fixture final : window_fixture {
    model::state::window_handle window;

    surface_fixture() {
        static const std::vector<std::string> expiries{"1W", "1M", "2M", "3M",
            "6M", "9M", "1Y"};
        static const std::vector<std::string> strikes{"DEL/Spot/Put/10",
            "DEL/Spot/Put/25", "ATM", "DEL/Spot/Call/25", "DEL/Spot/Call/10"};

        /*
         * A smile that steepens with expiry, which is the shape a reader is
         * checking for: a bowl along the strikes, a lift along the expiries.
         */
        std::vector<double> values;
        for (std::size_t at = 0; at < expiries.size(); ++at) {
            for (std::size_t column = 0; column < strikes.size(); ++column) {
                const double wing
                    = static_cast<double>(column) - 2.0;
                values.push_back(0.08 + 0.012 * static_cast<double>(at)
                    + 0.006 * wing * wing);
            }
        }
        const auto handle = desk.surfaces.intern("i", "a", "blueprint-1");
        desk.surfaces.apply(handle, "EURUSD_SSVI", "EUR", "USD", newest_at_ns,
            expiries, strikes, std::move(values));
        window = desk.open(model::domain::window_kind::vol_surface,
            model::domain::vol_surface_state{.label = "EURUSD_SSVI",
                .blueprint_id = "blueprint-1", .base_currency = "EUR",
                .quote_currency = "USD"});
    }

    void draw() {
        auto input = desk.input();
        input.wall_now_ns = newest_at_ns;
        hedgr::testing::frame([&] {
            render::render_vol_surface(window, input, state, status, lock);
        });
    }
};

/** @brief A CRM over five currencies whose cells take @p status_of's verdict. */
struct crm_fixture final : window_fixture {
    model::state::window_handle window;

    explicit crm_fixture(
        const std::function<std::string(std::size_t)>& status_of) {
        static const std::vector<std::string> codes{"EUR", "GBP", "JPY",
            "USD", "CHF"};
        std::vector<model::domain::crm_cell> cells;
        std::size_t at = 0;
        for (const auto& base : codes) {
            for (const auto& quote : codes) {
                if (base == quote)
                    continue;
                const double delta = (static_cast<double>(at % 7) - 3.0) * 0.01;
                cells.push_back(model::domain::crm_cell{.crm_name = "majors",
                    .base_currency_code = base, .quote_currency_code = quote,
                    .rate = 1.0 + static_cast<double>(at) / 37.0,
                    .status = status_of(at), .as_of = "2026-10-22T10:00:00Z",
                    .delta_pct = delta});
                ++at;
            }
        }
        desk.crm.set("i", "a", "p", "majors", false, std::move(cells), {},
            newest_at_ns, 4'000'000);
        window = desk.open(model::domain::window_kind::crm_matrix,
            model::domain::crm_matrix_state{.party_id = "p",
                .crm_name = "majors"});
    }

    void draw() {
        auto input = desk.input();
        input.wall_now_ns = newest_at_ns;
        hedgr::testing::frame([&] {
            render::render_crm_matrix(window, input, state, status, lock);
        });
    }
};

struct browser_fixture final : window_fixture {
    model::state::window_handle window;

    browser_fixture() {
        std::vector<model::domain::market_series_row> rows;
        const char* const classes[] = {"FX", "FX", "EQ", "CMDTY"};
        for (std::size_t i = 0; i < 12; ++i) {
            rows.push_back(model::domain::market_series_row{
                .id = std::format("s-{}", i),
                .uri = std::format("fx/gbpusd/{}", i),
                .label = std::format("FX GBP/USD {:02}", i),
                .asset_class = classes[i % 4],
                .provider_code = "ore"});
        }
        desk.market.apply_catalogue("i", "a", std::move(rows));
        window = desk.open(model::domain::window_kind::series_browser,
            model::domain::series_browser_state{});
    }

    void draw() {
        auto input = desk.input();
        hedgr::testing::frame([&] {
            render::render_series_browser(window, input, state, status, lock);
        });
    }
};

struct tenants_fixture final : window_fixture {
    model::state::window_handle window;

    tenants_fixture() {
        model::domain::deployment_snapshot snapshot;
        snapshot.observed_at_ns = newest_at_ns;
        snapshot.tenant_total = 3;
        snapshot.tenants = {
            {.id = "t1", .name = "Northwind", .status = "active",
                .type = "production"},
            {.id = "t2", .name = "Contoso", .status = "suspended",
                .type = "evaluation"},
            {.id = "t3", .name = "Fabrikam", .status = "archived",
                .type = "production"}};
        desk.sessions.set_deployment(desk.session, std::move(snapshot));
        window = desk.open(model::domain::window_kind::command_centre,
            model::domain::command_centre_state{.instance_id = "i"});
        render::open_command_centre_screen_for_capture("tenants");
    }

    void draw() {
        auto input = desk.input();
        hedgr::testing::frame([&] {
            render::render_command_centre_window(window, input, state, status,
                lock);
        });
    }
};

/** @brief Draws @p draw a few times and returns the last frame's vertices. */
template<class Draw, class Read>
auto settled(Draw&& draw, Read&& read) {
    for (int i = 0; i < 3; ++i)
        draw();
    return read();
}

}

TEST_CASE("a rising live chart wears a good change pill and no bad one",
    "[look][chart]") {
    chart_fixture chart{0.0001};
    const auto vertices = settled([&] { chart.draw(newest_at_ns); },
        frame_vertices);
    CHECK(has_pill(vertices, theme::tone::good));
    CHECK_FALSE(has_pill(vertices, theme::tone::bad));
    CHECK_FALSE(has_pill(vertices, theme::tone::warning));
}

TEST_CASE("a falling live chart wears a bad change pill and no good one",
    "[look][chart]") {
    chart_fixture chart{-0.0001};
    const auto vertices = settled([&] { chart.draw(newest_at_ns); },
        frame_vertices);
    CHECK(has_pill(vertices, theme::tone::bad));
    CHECK_FALSE(has_pill(vertices, theme::tone::good));
}

TEST_CASE("a delayed chart's change wears the delay, a stale one no state",
    "[look][chart]") {
    {
        chart_fixture delayed{0.0001};
        const auto late = settled(
            [&] { delayed.draw(newest_at_ns + 45'000'000'000); },
            frame_vertices);
        CHECK(has_pill(late, theme::tone::warning));
        CHECK_FALSE(has_pill(late, theme::tone::good));
    }
    {
        chart_fixture stale{0.0001};
        const auto old = settled(
            [&] { stale.draw(newest_at_ns + 600'000'000'000); },
            frame_vertices);
        CHECK_FALSE(has_state_tone(old));
        CHECK(has_pill(old, theme::tone::neutral));
    }
}

TEST_CASE("a chart names its series in a neutral pill", "[look][chart]") {
    chart_fixture chart{0.0};
    const auto vertices = settled([&] { chart.draw(newest_at_ns); },
        frame_vertices);
    const ImU32 fill = theme::pill_colours(theme::tone::neutral).fill;
    CHECK(solid_width(vertices, fill)
        >= widget::value_pill_size("FX GBP/USD").x - 1.5f);
    CHECK_FALSE(has_state_tone(vertices));
}

/** @brief The top of the chart's plot area, below its one header row. */
float plot_top() {
    for (const ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if (std::string_view(w->Name).find("##plot-area") != std::string_view::npos)
            return w->Pos.y;
    }
    FAIL("no plot area");
    return 0.0f;
}

TEST_CASE("a chart heads its plot with one row and a large price",
    "[look][chart]") {
    chart_fixture chart{0.0001};
    const auto vertices = settled([&] { chart.draw(newest_at_ns); },
        frame_vertices);
    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    REQUIRE(frame != nullptr);
    const float header_top = frame->DC.CursorStartPos.y;
    const float header = plot_top() - header_top;
    CHECK(header == Approx(ImGui::GetFrameHeight()
        + ImGui::GetStyle().ItemSpacing.y));

    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const auto extent = [&](const std::vector<ImDrawVert>& drawn, float top,
                            float bottom) {
        float low = FLT_MAX;
        float high = -FLT_MAX;
        for (const auto& v : drawn) {
            if (v.col != text || (v.uv.x == white.x && v.uv.y == white.y)
                || v.pos.y < top || v.pos.y > bottom)
                continue;
            low = std::min(low, v.pos.y);
            high = std::max(high, v.pos.y);
        }
        return high > low ? high - low : 0.0f;
    };
    const float price = extent(vertices, header_top, plot_top());

    float line_top = 0.0f;
    float line_bottom = 0.0f;
    hedgr::testing::frame([&] {
        ImGui::TextUnformatted("1.2345 FX 120");
        line_top = ImGui::GetItemRectMin().y;
        line_bottom = ImGui::GetItemRectMax().y;
    });
    const auto body = frame_vertices();
    CHECK(price > extent(body, line_top, line_bottom) + 2.0f);
}

/** @brief The child window drawn this frame whose name holds @p part, if any. */
const ImGuiWindow* active_child(std::string_view part) {
    for (const ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if (w->Active && std::string_view(w->Name).find(part)
                != std::string_view::npos)
            return w;
    }
    return nullptr;
}

/** @brief How far the chart's plot area starts below the top of its content. */
float header_height() {
    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    REQUIRE(frame != nullptr);
    return plot_top() - frame->DC.CursorStartPos.y;
}

TEST_CASE("a docked chart draws the full header, plot and foot",
    "[look][chart][docking]") {
    chart_fixture chart{0.0001};
    chart.docked = true;
    settled([&] { chart.draw(newest_at_ns); }, frame_vertices);

    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    REQUIRE(frame != nullptr);
    REQUIRE(frame->DockIsActive);
    const ImGuiWindow* foot = active_child("##hedgr-status-line");
    REQUIRE(foot != nullptr);
    const ImGuiWindow* plot = active_child("##plot-area");
    REQUIRE(plot != nullptr);
    CHECK(plot->Pos.y + plot->Size.y <= foot->Pos.y);
    CHECK(header_height() == Approx(ImGui::GetFrameHeight()
        + ImGui::GetStyle().ItemSpacing.y));
}

TEST_CASE("a floating chart still draws its foot", "[look][chart][docking]") {
    chart_fixture chart{0.0001};
    settled([&] { chart.draw(newest_at_ns); }, frame_vertices);

    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    REQUIRE(frame != nullptr);
    CHECK_FALSE(frame->DockIsActive);
    const ImGuiWindow* foot = active_child("##hedgr-status-line");
    REQUIRE(foot != nullptr);
    const ImGuiWindow* plot = active_child("##plot-area");
    REQUIRE(plot != nullptr);
    CHECK(plot->Pos.y + plot->Size.y <= foot->Pos.y);
}

TEST_CASE("a docked chart's header is as tall as a floating chart's",
    "[look][chart][docking]") {
    float full = 0.0f;
    {
        chart_fixture chart{0.0001};
        settled([&] { chart.draw(newest_at_ns); }, frame_vertices);
        full = header_height();
    }
    chart_fixture chart{0.0001};
    chart.docked = true;
    settled([&] { chart.draw(newest_at_ns); }, frame_vertices);
    CHECK(full > 0.0f);
    CHECK(header_height() == Approx(full));
}

TEST_CASE("a docked chart wears the same pills as a floating one",
    "[look][chart][docking]") {
    {
        chart_fixture rising{0.0001};
        rising.docked = true;
        const auto vertices = settled([&] { rising.draw(newest_at_ns); },
            frame_vertices);
        CHECK(has_pill(vertices, theme::tone::good));
        CHECK_FALSE(has_pill(vertices, theme::tone::bad));
        const ImU32 fill = theme::pill_colours(theme::tone::neutral).fill;
        CHECK(solid_width(vertices, fill)
            >= widget::value_pill_size("FX GBP/USD").x - 1.5f);
    }
    {
        chart_fixture delayed{-0.0001};
        delayed.docked = true;
        const auto vertices = settled(
            [&] { delayed.draw(newest_at_ns + 45'000'000'000); },
            frame_vertices);
        CHECK(has_pill(vertices, theme::tone::warning));
        CHECK_FALSE(has_pill(vertices, theme::tone::bad));
    }
}

namespace {

/** @brief Moves the mouse to @p at and draws a frame. */
void hover(chart_fixture& chart, ImVec2 at) {
    ImGui::GetIO().AddMousePosEvent(at.x, at.y);
    chart.draw(newest_at_ns);
}

/** @brief Presses and releases @p button at @p at, a frame for each. */
void click(chart_fixture& chart, ImVec2 at, ImGuiMouseButton button) {
    hover(chart, at);
    ImGui::GetIO().AddMouseButtonEvent(button, true);
    chart.draw(newest_at_ns);
    ImGui::GetIO().AddMouseButtonEvent(button, false);
    chart.draw(newest_at_ns);
}

/** @brief The popup open on top, or null. */
const ImGuiWindow* open_popup() {
    const auto& stack = ImGui::GetCurrentContext()->OpenPopupStack;
    return stack.empty() ? nullptr : stack.back().Window;
}

ImVec2 centre_of(const ImGuiWindow* w) {
    return ImVec2(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.5f);
}

}

TEST_CASE("a right click on a chart's plot opens no menu, docked or floating",
    "[look][chart][docking]") {
    for (const bool docked : {false, true}) {
        chart_fixture chart{0.0001};
        chart.docked = docked;
        settled([&] { chart.draw(newest_at_ns); }, frame_vertices);
        const ImGuiWindow* plot = active_child("##plot-area");
        REQUIRE(plot != nullptr);
        click(chart, centre_of(plot), ImGuiMouseButton_Right);
        CHECK(open_popup() == nullptr);
    }
}

namespace {

/** @brief The id of option @p option of the header control @p control. */
ImGuiID segment_id(const char* control, const char* const* labels, int option) {
    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    REQUIRE(frame != nullptr);
    ImGuiID id = ImHashStr("chart", 0, frame->ID);
    id = ImHashStr(control, 0, id);
    id = ImHashData(&option, sizeof(option), id);
    return ImHashStr(labels[option], 0, id);
}

ImGuiID period_segment_id(int option) {
    return segment_id("period", render::chart_span_labels.data(), option);
}

ImGuiID style_segment_id(int option) {
    return segment_id("style", render::chart_style_labels.data(), option);
}

ImGuiID ema_segment_id(int option) {
    return segment_id("ema", render::chart_ema_labels.data(), option);
}

/** @brief The id of the dropdown button the period control used to end in. */
ImGuiID period_more_id() {
    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    REQUIRE(frame != nullptr);
    ImGuiID id = ImHashStr("chart", 0, frame->ID);
    id = ImHashStr("period", 0, id);
    id = ImHashStr("more", 0, id);
    return ImHashStr("##more", 0, id);
}

/**
 * @brief The x at which the pointer is over the item @p wanted in the header.
 *
 * Sweeps the header row from its right end, because the header's controls are
 * placed by the layout and the test should not repeat it.
 */
float header_x_of(chart_fixture& chart, ImGuiID wanted) {
    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    REQUIRE(frame != nullptr);
    const float y = frame->DC.CursorStartPos.y + ImGui::GetFrameHeight() * 0.5f;
    for (float x = 990.0f; x > 0.0f; x -= 3.0f) {
        hover(chart, ImVec2(x, y));
        if (ImGui::GetCurrentContext()->HoveredId == wanted)
            return x;
    }
    FAIL("no header item under the pointer");
    return 0.0f;
}

/** @brief The x of item @p wanted in the header row, or a negative if none. */
float header_x_if_any(chart_fixture& chart, ImGuiID wanted) {
    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    REQUIRE(frame != nullptr);
    const float y = frame->DC.CursorStartPos.y + ImGui::GetFrameHeight() * 0.5f;
    for (float x = 990.0f; x > 0.0f; x -= 3.0f) {
        hover(chart, ImVec2(x, y));
        if (ImGui::GetCurrentContext()->HoveredId == wanted)
            return x;
    }
    return -1.0f;
}

float header_y() {
    const ImGuiWindow* frame = ImGui::FindWindowByName("frame");
    return frame->DC.CursorStartPos.y + ImGui::GetFrameHeight() * 0.5f;
}

}

TEST_CASE("a period chosen in the header changes the span and is saved",
    "[look][chart][period]") {
    chart_fixture chart{0.0001};
    settled([&] { chart.draw(newest_at_ns); }, frame_vertices);
    static_cast<void>(chart.desk.commands.drain());
    REQUIRE(chart.state.market_view.chart_span == 3);

    const float x = header_x_of(chart, period_segment_id(0));
    click(chart, ImVec2(x, header_y()), ImGuiMouseButton_Left);

    CHECK(chart.state.market_view.chart_span == 0);
    CHECK(chart.desk.market_view.chart_span == 0);
    std::vector<model::command::command> saved;
    for (auto& c : chart.desk.commands.drain()) {
        if (c.kind == model::command::command_kind::configure_chart)
            saved.push_back(std::move(c));
    }
    REQUIRE(saved.size() == 1);
    CHECK(saved[0].window_id == "w");
    CHECK(saved[0].chart_view.span_ns == 60'000'000'000);
}

TEST_CASE("the header strip shows every period and has no dropdown",
    "[look][chart][period]") {
    chart_fixture chart{0.0001};
    settled([&] { chart.draw(newest_at_ns); }, frame_vertices);

    float previous = -1.0f;
    for (std::size_t option = 0; option < render::chart_spans.size(); ++option) {
        const float x = header_x_if_any(chart,
            period_segment_id(static_cast<int>(option)));
        INFO(render::chart_spans[option].label);
        CHECK(x > previous);
        previous = x;
    }
    CHECK(header_x_if_any(chart, period_more_id()) < 0.0f);
}

TEST_CASE("the period strip's last segment is All and it picks the whole series",
    "[look][chart][period]") {
    chart_fixture chart{0.0001};
    settled([&] { chart.draw(newest_at_ns); }, frame_vertices);

    const int all = static_cast<int>(render::chart_spans.size()) - 1;
    const float x = header_x_of(chart, period_segment_id(all));
    click(chart, ImVec2(x, header_y()), ImGuiMouseButton_Left);
    CHECK(chart.state.market_view.chart_span == all);
    CHECK(render::chart_span_ns(all) == 0);
}

TEST_CASE("the plot type and EMA controls are in the header, after the strip",
    "[look][chart][period]") {
    chart_fixture chart{0.0001};
    settled([&] { chart.draw(newest_at_ns); }, frame_vertices);
    const float all = header_x_if_any(chart,
        period_segment_id(static_cast<int>(render::chart_spans.size()) - 1));
    REQUIRE(all > 0.0f);

    const float candles = header_x_if_any(chart, style_segment_id(0));
    const float line = header_x_if_any(chart, style_segment_id(1));
    const float off = header_x_if_any(chart, ema_segment_id(0));
    const float on = header_x_if_any(chart, ema_segment_id(1));
    CHECK(candles > all);
    CHECK(line > candles);
    CHECK(off > line);
    CHECK(on > off);

    /* Choosing in the header changes the chart and is saved as its view. */
    static_cast<void>(chart.desk.commands.drain());
    click(chart, ImVec2(on, header_y()), ImGuiMouseButton_Left);
    click(chart, ImVec2(candles, header_y()), ImGuiMouseButton_Left);
    CHECK(chart.state.market_view.chart_ema == 1);
    CHECK(chart.state.market_view.chart_style == 0);
    std::vector<model::command::command> saved;
    for (auto& c : chart.desk.commands.drain()) {
        if (c.kind == model::command::command_kind::configure_chart)
            saved.push_back(std::move(c));
    }
    REQUIRE_FALSE(saved.empty());
    CHECK(saved.back().chart_view.ema);
    CHECK(saved.back().chart_view.style
        == model::domain::chart_style::candles);
}

TEST_CASE("a chart's foot holds the identity and status and no controls",
    "[look][chart][period]") {
    chart_fixture chart{0.0001};
    std::string text;
    hedgr::testing::frame([&] {
        ImGui::LogToBuffer();
        auto input = chart.desk.input();
        input.wall_now_ns = newest_at_ns;
        input.now_ns = newest_at_ns;
        render::render_chart(chart.window, input, chart.state, chart.status,
            chart.lock);
        text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
    });
    CHECK(text.find("Line") == std::string::npos);
    CHECK(text.find("EMA") == std::string::npos);
    CHECK(text.find("15m") == std::string::npos);
}

namespace {

/** @brief The vertices the chart's plot area drew in @p colour or its fade. */
std::vector<ImDrawVert> plot_vertices_of(ImU32 colour) {
    const ImGuiWindow* plot = active_child("##plot-area");
    REQUIRE(plot != nullptr);
    std::vector<ImDrawVert> found;
    for (const auto& v : plot->DrawList->VtxBuffer) {
        if (v.col == colour || v.col == (colour & ~IM_COL32_A_MASK))
            found.push_back(v);
    }
    return found;
}

}

TEST_CASE("a chart's line and fill are the chart purple, not the accent blue",
    "[look][chart][plot]") {
    chart_fixture chart{0.0001};
    chart.state.market_view.chart_style = 1;
    settled([&] { chart.draw(newest_at_ns); }, frame_vertices);
    CHECK_FALSE(plot_vertices_of(ImGui::ColorConvertFloat4ToU32(
        theme::chart_line_colour())).empty());
    CHECK(plot_vertices_of(ImGui::ColorConvertFloat4ToU32(
        theme::accent_colour())).empty());
}

TEST_CASE("a chart's plot draws no border and no frame",
    "[look][chart][plot]") {
    for (const int style : {1, 0}) {
        chart_fixture chart{0.0001};
        chart.state.market_view.chart_style = style;
        settled([&] { chart.draw(newest_at_ns); }, frame_vertices);
        CHECK(plot_vertices_of(ImGui::GetColorU32(ImGuiCol_Border)).empty());
        CHECK(plot_vertices_of(ImGui::GetColorU32(ImGuiCol_FrameBg)).empty());
    }
}

TEST_CASE("a chart's grid is faint dashed rules across the plot and no vertical ones",
    "[look][chart][plot]") {
    for (const int style : {1, 0}) {
        chart_fixture chart{0.0001};
        chart.state.market_view.chart_style = style;
        settled([&] { chart.draw(newest_at_ns); }, frame_vertices);
        const auto grid = plot_vertices_of(theme::plot_grid_colour);
        REQUIRE_FALSE(grid.empty());

        /* Vertices within two pixels of one another in y are one rule. */
        auto by_y = grid;
        std::ranges::sort(by_y, {}, [](const ImDrawVert& v) { return v.pos.y; });
        std::vector<float> extents;
        std::size_t begin = 0;
        for (std::size_t i = 1; i <= by_y.size(); ++i) {
            if (i < by_y.size() && by_y[i].pos.y - by_y[i - 1].pos.y <= 2.0f)
                continue;
            float low = FLT_MAX;
            float high = -FLT_MAX;
            for (std::size_t k = begin; k < i; ++k) {
                low = std::min(low, by_y[k].pos.x);
                high = std::max(high, by_y[k].pos.x);
            }
            extents.push_back(high - low);
            begin = i;
        }
        CHECK(extents.size() >= 3);
        CHECK(*std::ranges::max_element(extents) > 200.0f);

        /*
         * ImPlot's own rules are solid and run along both axes, so neither
         * axis may draw them, and neither may mark its ticks.
         */
        const ImGuiWindow* plot_area = active_child("##plot-area");
        const ImPlotPlot* series = ImPlot::GetCurrentContext()->Plots.GetByKey(
            ImHashStr("##series", 0, plot_area->ID));
        REQUIRE(series != nullptr);
        for (const ImAxis axis : {ImAxis_X1, ImAxis_Y1}) {
            CHECK_FALSE(series->Axes[axis].HasGridLines());
            CHECK_FALSE(series->Axes[axis].HasTickMarks());
        }
    }
}

TEST_CASE("a term structure names its curve in a pill and wears no state",
    "[look][rates]") {
    term_fixture terms;
    const auto vertices = settled([&] { terms.draw(); }, frame_vertices);
    const ImU32 fill = theme::pill_colours(theme::tone::neutral).fill;
    CHECK(solid_width(vertices, fill)
        == Approx(widget::value_pill_size("IR GBP SONIA").x).margin(1.5));
    CHECK_FALSE(has_state_tone(vertices));
}

TEST_CASE("a CRM of fresh cells counts them in a good pill", "[look][crm]") {
    crm_fixture crm{[](std::size_t) { return std::string{"fresh"}; }};
    const auto vertices = settled([&] { crm.draw(); }, frame_vertices);
    CHECK(has_pill(vertices, theme::tone::good));
    CHECK(has_colour(vertices, theme::pill_colours(theme::tone::quiet).fill));
    CHECK_FALSE(has_pill(vertices, theme::tone::warning));
    CHECK_FALSE(has_pill(vertices, theme::tone::bad));
}

TEST_CASE("a CRM with stale cells counts them in a warning pill",
    "[look][crm]") {
    crm_fixture crm{[](std::size_t at) {
        return std::string{at % 4 == 0 ? "stale" : "fresh"};
    }};
    const auto vertices = settled([&] { crm.draw(); }, frame_vertices);
    CHECK(has_pill(vertices, theme::tone::good));
    CHECK(has_pill(vertices, theme::tone::warning));
}

TEST_CASE("a CRM with no fresh cell claims no good state", "[look][crm]") {
    crm_fixture crm{[](std::size_t) { return std::string{"stale"}; }};
    const auto vertices = settled([&] { crm.draw(); }, frame_vertices);
    CHECK_FALSE(has_pill(vertices, theme::tone::good));
    CHECK(has_pill(vertices, theme::tone::warning));
}

TEST_CASE("a series browser heads its pickers with captions",
    "[look][browser]") {
    browser_fixture browser;
    const auto vertices = settled([&] { browser.draw(); }, frame_vertices);
    CHECK(has_colour(vertices, ImGui::GetColorU32(theme::dim_text())));
    CHECK_FALSE(has_state_tone(vertices));
}

TEST_CASE("a tenant's status is a pill in its state's tone",
    "[look][command-centre]") {
    tenants_fixture centre;
    const auto vertices = settled([&] { centre.draw(); }, all_vertices);
    CHECK(has_pill(vertices, theme::tone::good));
    CHECK(has_pill(vertices, theme::tone::bad));
    CHECK_FALSE(has_pill(vertices, theme::tone::warning));
    CHECK(has_colour(vertices, theme::pill_colours(theme::tone::quiet).fill));
}

TEST_CASE("the tenants tab counts the roster in an accent badge",
    "[look][command-centre]") {
    tenants_fixture centre;
    const auto vertices = settled([&] { centre.draw(); }, all_vertices);
    const ImU32 number = ImGui::ColorConvertFloat4ToU32(theme::base_surface());
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    const auto glyphs = std::ranges::count_if(vertices, [&](const ImDrawVert& v) {
        return v.col == number && (v.uv.x != white.x || v.uv.y != white.y);
    });
    CHECK(glyphs == 4);
}

namespace {

/** @brief Four charts on four periods, docked two by two through the workspace. */
struct docked_desk_fixture final {
    themed context;
    desk desk;
    hedgr::ui::render::workspace workspace;
    int frames = 0;

    docked_desk_fixture() {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        const char* const labels[] = {"FX GBP/USD", "FX EUR/USD",
            "FX USD/JPY", "FX AUD/USD"};
        const double bases[] = {1.27, 1.08, 151.2, 0.66};
        const std::int64_t spans[] = {5 * 60, 15 * 60, 3600, 3600};
        for (std::size_t c = 0; c < 4; ++c) {
            const auto id = std::format("s-{}", c);
            const auto series = desk.market.intern("i", "a", id);
            for (std::size_t p = 0; p < 600; ++p) {
                const double wobble = std::sin(static_cast<double>(p + c * 40)
                    / (9.0 + static_cast<double>(c) * 3.0));
                static_cast<void>(desk.market.append(series,
                    model::domain::market_level::fine,
                    model::state::series_point{
                        .at_ns = newest_at_ns
                            - static_cast<std::int64_t>(599 - p) * 6'000'000'000,
                        .value = bases[c] * (1.0 + 0.002 * wobble
                            + 0.00001 * static_cast<double>(p))},
                    1));
            }
            const auto window = desk.open(model::domain::window_kind::chart,
                model::domain::chart_state{.label = labels[c],
                    .series_id = id,
                    .view = model::domain::chart_view{
                        .span_ns = spans[c] * 1'000'000'000,
                        .style = c == 3 ? model::domain::chart_style::candles
                                        : model::domain::chart_style::line,
                        .ema = c == 2}},
                std::format("chart-{}", c));
            desk.windows.set_series(window, std::span{&series, 1});
        }
    }

    void draw() {
        auto input = desk.input();
        input.wall_now_ns = newest_at_ns;
        input.now_ns = newest_at_ns;
        ImGui::NewFrame();
        if (frames++ == 0) {
            const ImGuiID root = ImGui::GetID("gallery-dock");
            ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
            ImGui::DockBuilderSetNodePos(root, ImVec2(0.0f, 0.0f));
            ImGui::DockBuilderSetNodeSize(root, ImVec2(1000.0f, 700.0f));
            ImGuiID right = 0;
            const ImGuiID left = ImGui::DockBuilderSplitNode(root,
                ImGuiDir_Left, 0.5f, nullptr, &right);
            ImGuiID left_bottom = 0;
            const ImGuiID left_top = ImGui::DockBuilderSplitNode(left,
                ImGuiDir_Up, 0.5f, nullptr, &left_bottom);
            ImGuiID right_bottom = 0;
            const ImGuiID right_top = ImGui::DockBuilderSplitNode(right,
                ImGuiDir_Up, 0.5f, nullptr, &right_bottom);
            ImGui::DockBuilderDockWindow("###chart-0", left_top);
            ImGui::DockBuilderDockWindow("###chart-1", right_top);
            ImGui::DockBuilderDockWindow("###chart-2", left_bottom);
            ImGui::DockBuilderDockWindow("###chart-3", right_bottom);
            ImGui::DockBuilderFinish(root);
        }
        if (frames == 2)
            ImGui::SetWindowFocus("###chart-0");
        workspace.render(input);
        ImGui::Render();
    }
};

}

/*
 * Hidden from the suite: it paints the market windows for a person to look at
 * and asserts nothing about them. Run it by name with HEDGR_WINDOW_GALLERY set
 * to a directory; it writes one PPM per window there.
 */
TEST_CASE("window gallery", "[.gallery]") {
    const char* out = std::getenv("HEDGR_WINDOW_GALLERY");
    REQUIRE(out != nullptr);
    const std::filesystem::path dir{out};

    const auto paint = [&](const char* name, auto& fixture, auto draw) {
        hedgr::ui::icon::load_ui_fonts(15.0f);
        ImGui::GetIO().DisplaySize = ImVec2(1000.0f, 700.0f);
        hedgr::testing::software_raster raster(1000, 700);
        for (int i = 0; i < 4; ++i) {
            draw(fixture);
            raster.update_textures();
        }
        raster.draw(*ImGui::GetDrawData(),
            ImGui::ColorConvertFloat4ToU32(
                ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)));
        CHECK(raster.write_ppm(dir / std::format("{}.ppm", name)));
    };

    {
        chart_fixture chart{0.00002, [](std::size_t p) {
            return 0.0006 * std::sin(static_cast<double>(p) / 7.0);
        }};
        paint("chart", chart, [](auto& f) { f.draw(newest_at_ns); });
    }
    {
        term_fixture terms;
        paint("term-structure", terms, [](auto& f) { f.draw(); });
    }
    {
        surface_fixture surface;
        paint("vol-surface", surface, [](auto& f) { f.draw(); });
    }
    {
        crm_fixture crm{[](std::size_t at) {
            return std::string{at % 6 == 0 ? "stale" : "fresh"};
        }};
        paint("crm", crm, [](auto& f) { f.draw(); });
    }
    {
        browser_fixture browser;
        paint("series-browser", browser, [](auto& f) { f.draw(); });
    }
    {
        tenants_fixture centre;
        paint("tenants", centre, [](auto& f) { f.draw(); });
    }
    {
        docked_desk_fixture grid;
        paint("docked-charts", grid, [](auto& f) { f.draw(); });
    }
}

TEST_CASE("a surface window draws a mesh, and a sentence when none is published",
    "[window-look][surfaces]") {
    headless_imgui imgui;
    desk desk;
    static_cast<void>(imgui);

    const auto window = desk.open(model::domain::window_kind::vol_surface,
        model::domain::vol_surface_state{.label = "EURUSD_SSVI",
            .blueprint_id = "blueprint-1", .base_currency = "EUR",
            .quote_currency = "USD"});
    render::window_ui_state state;
    hedgr::ui::widget::status_bar status;
    render::lock_control lock;
    auto input = desk.input();

    /* Nothing read yet: the window is reading, and draws no mesh. */
    std::size_t reading_vertices = 0;
    hedgr::testing::frame([&] {
        const auto before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        render::render_vol_surface(window, input, state, status, lock);
        reading_vertices = static_cast<std::size_t>(
            ImGui::GetWindowDrawList()->VtxBuffer.Size - before);
    });
    CHECK(state.traced.state
        == static_cast<int>(hedgr::ui::widget::view_state::reading));

    /* A published surface: seven expiries by five strikes fills the child. */
    const auto handle = desk.surfaces.intern("i", "a", "blueprint-1");
    desk.surfaces.apply(handle, "EURUSD_SSVI", "EUR", "USD", 1'760'000'000'000'000'000,
        {"1W", "1M", "2M", "3M", "6M", "9M", "1Y"},
        {"DEL/Spot/Put/25", "ATM", "DEL/Spot/Call/25"},
        {0.09, 0.08, 0.09, 0.10, 0.09, 0.10, 0.11, 0.10, 0.11, 0.12, 0.11,
            0.12, 0.13, 0.12, 0.13, 0.14, 0.13, 0.14, 0.15, 0.14, 0.15});
    auto ready_input = desk.input();
    std::size_t ready_vertices = 0;
    hedgr::testing::frame([&] {
        const auto before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        render::render_vol_surface(window, ready_input, state, status, lock);
        ready_vertices = static_cast<std::size_t>(
            ImGui::GetWindowDrawList()->VtxBuffer.Size - before);
    });
    CHECK(state.traced.state
        == static_cast<int>(hedgr::ui::widget::view_state::none));
    /*
     * The mesh is one filled quad and one edge per cell, so a surface of six by
     * four cells is hundreds of vertices - and the reading frame, which draws
     * only chrome, is not.
     */
    CHECK(ready_vertices > 200);

    /*
     * The deployment refusing the read is a fact, and the window draws its
     * sentence rather than an empty plot: "nothing is published at or before
     * this instant" is a state an operator has to be told.
     */
    desk.surfaces.fail(handle,
        "No surface of EURUSD_SSVI is published at or before this instant.");
    auto failed_input = desk.input();
    hedgr::testing::frame([&] {
        render::render_vol_surface(window, failed_input, state, status, lock);
    });
    CHECK(state.traced.state
        == static_cast<int>(hedgr::ui::widget::view_state::error));
    CHECK(state.traced.reason
        == "No surface of EURUSD_SSVI is published at or before this instant.");
}
