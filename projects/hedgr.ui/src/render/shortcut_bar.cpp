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
/*
 * ImCoolBar reaches into imgui_internal, which refuses to be included unless
 * the math operators were asked for before imgui.h was first seen. This
 * component's own headers pull imgui.h in, so the request is made here, ahead
 * of all of them, which is the only place it can be made.
 */
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "hedgr.ui/render/shortcut_bar.hpp"

#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/shortcut.hpp"
#include "hedgr.model/signal/names.hpp"
#include "hedgr.model/signal/signal.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/capture.hpp"
#include "ImCoolBar.h"
#include "imgui.h"
#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace {

namespace domain = hedgr::model::domain;
namespace signal = hedgr::model::signal;
namespace theme = hedgr::ui::theme;
namespace widget = hedgr::ui::widget;
using hedgr::ui::render::render_input;
using hedgr::ui::render::shortcut_bar_result;

/**
 * @brief The size an entry is drawn at when the pointer is away from the bar.
 *
 * Forty pixels is close to the height of the icon font's line box and small
 * enough that a bar of ten does not run the width of the viewport. The library
 * magnifies an entry the pointer is near, and the two numbers are the whole of
 * the bar's geometry.
 */
constexpr float entry_size = 40.0f;
constexpr float entry_hovered_size = 150.0f;

/**
 * @brief How fast the bar grows an entry towards its hovered size.
 *
 * The library advances its own bar-wide scale by this fraction every sixtieth
 * of a second and stops at one, so the reciprocal is how long a full grow takes
 * in seconds. Three hundredths spreads that over a little more than half a
 * second, which reads as the bar unfolding under the pointer rather than
 * snapping to it, and it is still short enough that a reader who has settled on
 * an entry is not left waiting to see it arrive. The library's own default of
 * five hundredths covered the same ground in a third of a second, which is the
 * quickness the operator complained of.
 */
constexpr float entry_anim_step = 0.03f;

/** @brief The id the undock control is pushed under, in the bar's id space. */
constexpr const char* undock_all_id = "undock-all";

/**
 * @brief The picture an entry draws, or zero for the kind's own glyph.
 *
 * Only an accounts shortcut stands for an instance, and an instance is the one
 * thing here that can carry an illustration. A shortcut to the Instances window
 * is a shortcut to a surface rather than to a deployment, so it keeps the
 * window kind's glyph even though an environment picture would fit the slot.
 */
[[nodiscard]] std::uint64_t entry_texture(const render_input& input,
    const domain::shortcut& row) {
    if (row.target_kind != domain::window_kind::accounts)
        return 0;
    return hedgr::ui::render::instance_icon_texture(input, row.target_id);
}

/**
 * @brief What hovering an entry says the click will do.
 *
 * The sentence names the instance rather than the shortcut, because two
 * shortcuts to two deployments look alike as icons and the name is the only
 * thing that tells a reader which one they are about to open. A kind the
 * interface cannot open says so rather than staying silent, so a click that
 * does nothing is explained before it is made.
 */
[[nodiscard]] std::string entry_tooltip(const render_input& input,
    const domain::shortcut& row) {
    switch (row.target_kind) {
    case domain::window_kind::instance_manager:
        return "Bring the Instances window to the front";
    case domain::window_kind::accounts: {
        const auto handle = input.instances.find(row.target_id);
        if (!handle) {
            /*
             * The store removes a target's shortcuts when the target goes, so
             * this is not a state the bar is meant to hold. It is drawn rather
             * than skipped because an entry nobody expects is still an entry
             * on screen, and a click has to be answered with something.
             */
            return "The instance this shortcut named is no longer in the store";
        }
        return std::format("Open the accounts of {}",
            input.instances.get(*handle)->name);
    }
    default:
        break;
    }
    return std::format("A {} shortcut. This kind cannot be opened yet",
        domain::to_string(row.target_kind));
}

/**
 * @brief Raises the signal that says an entry was clicked.
 *
 * The bar plays nothing itself. It raises a name, the frame loop drains the
 * bus, and the player decides whether that name has a sound. Keeping the audio
 * out of here is what makes a click free of any device or disk work, and it is
 * what lets a second listener appear later, a status bar saying what was
 * opened, without touching this function.
 *
 * The detail is the entry's tooltip rather than a second sentence, so the
 * words a reader sees on hover and the words a log keeps are the same ones.
 */
void raise_activation(const render_input& input, const domain::shortcut& row) {
    signal::signal value;
    value.name = std::string(signal::names::shortcut_activated);
    value.detail = entry_tooltip(input, row);
    value.at_ns = input.now_ns;
    signal::bus().raise(std::move(value));
}

/**
 * @brief Raises the signal that says the pointer moved onto another entry.
 *
 * The hover is decided in the same place as the click and raised through the
 * same bus, because a tick is a fact about the bar and not a thing the bar
 * plays. The caller only gets here when the entry differs from the one the
 * pointer was on last frame, so a pointer resting on an entry raises nothing
 * and a pointer leaving the bar raises nothing either.
 */
void raise_hover(const render_input& input, const domain::shortcut& row) {
    signal::signal value;
    value.name = std::string(signal::names::shortcut_hovered);
    value.detail = entry_tooltip(input, row);
    value.at_ns = input.now_ns;
    signal::bus().raise(std::move(value));
}

/**
 * @brief Does what an entry means, for the kinds that mean something yet.
 *
 * The accounts case goes through the same request the Instances window's own
 * toolbar uses, so opening an instance's accounts has one description rather
 * than two. A kind with no surface yet is left alone, which is deliberate: the
 * entry draws and answers its tooltip, and the click is inert rather than
 * pretending to have opened something. An inert click raises no signal either,
 * because a sound would be claiming that something happened.
 */
void activate_entry(const render_input& input, const domain::shortcut& row,
    shortcut_bar_result& result) {
    switch (row.target_kind) {
    case domain::window_kind::instance_manager:
        /*
         * The root carries a close control now, and only the runtime can bring
         * it back, because the row is removed when it closes and raising the
         * focus of a window that is gone would do nothing. The request opens
         * the window when the table no longer holds it and sets it open when it
         * does, and the focus is still asked for because a window that was
         * already on screen belongs on top of the one the operator is reading.
         */
        hedgr::ui::render::request_instances_window(input);
        result.focus_instances = true;
        raise_activation(input, row);
        return;
    case domain::window_kind::accounts:
        hedgr::ui::render::request_accounts_window(input, row.target_id);
        raise_activation(input, row);
        return;
    default:
        return;
    }
}

/**
 * @brief What one entry's invisible button reported this frame.
 *
 * A pair rather than two calls into ImGui at the call site, because both
 * answers are read from the same last item and reading one after the other
 * entry has been submitted would be reading the wrong item.
 */
struct entry_interaction final {
    /** @brief Whether the entry was pressed this frame. */
    bool clicked = false;

    /** @brief Whether the pointer is on the entry now. */
    bool hovered = false;
};

/**
 * @brief Draws a tile of the theme's surface colour with @p glyph centred in it.
 *
 * The glyph comes from the icon set and is centred by measuring it, because a
 * bitmap glyph has one size and the entry is the library's to size. Drawing it
 * dimmed keeps it a placeholder rather than a control of its own. The cursor is
 * left where it was found.
 */
void draw_glyph_tile(const char* glyph, float size) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin,
        ImVec2(origin.x + size, origin.y + size),
        ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);

    const ImVec2 glyph_size = ImGui::CalcTextSize(glyph);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (size - glyph_size.x) * 0.5f,
        origin.y + (size - glyph_size.y) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(glyph);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(origin);
}

/**
 * @brief Draws one entry and reports whether it was clicked or hovered.
 *
 * The picture fills the entry's rectangle, and an entry with no picture draws
 * a tile of the theme's own surface colour with the window kind's glyph in it
 * rather than a second texture. That is what an instance typed by hand and a
 * key whose asset is missing both get, so neither is a hole where a picture
 * should be.
 *
 * The size is passed in rather than read here, because the library keys its own
 * per-item state by the current id stack and an id pushed for this entry would
 * change the key it is read under. The caller reads it before it pushes.
 *
 * The button is invisible and exactly covers the entry, so the click is caught
 * while the drawing stays the library's business.
 */
[[nodiscard]] entry_interaction draw_entry(const render_input& input,
    const domain::shortcut& row, float size, const char* id) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner(origin.x + size, origin.y + size);
    ImDrawList* list = ImGui::GetWindowDrawList();

    const std::uint64_t texture = entry_texture(input, row);
    if (texture != 0)
        list->AddImage(static_cast<ImTextureID>(texture), origin, corner);
    else
        draw_glyph_tile(theme::window_kind_icon(row.target_kind), size);

    ImGui::InvisibleButton(id, ImVec2(size, size));

    /*
     * The entry offers itself to a capture by its row id, so a script can click
     * the bar the way a person does rather than through a hook beside it. The
     * entry is the one control here that is painted rather than submitted as a
     * word, so its rectangle has to be recorded where ImGui's geometry for it
     * is still current.
     */
    widget::capture_element element;
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    element.min_x = item_min.x;
    element.min_y = item_min.y;
    element.max_x = item_max.x;
    element.max_y = item_max.y;
    element.label_min_x = item_min.x;
    widget::observe_capture_element(row.id, element);

    return entry_interaction{
        .clicked = ImGui::IsItemClicked(),
        .hovered = ImGui::IsItemHovered(),
    };
}

/**
 * @brief Puts the bar at the front of the ordinary windows, and every question
 * above it.
 *
 * A window submitted last is not a window kept in front. ImGui raises a window
 * when it takes focus, so a window the operator clicks moves above the bar and
 * stays there, because submission decides the frame a window is created in and
 * not the ones after it. Raising the bar once the walk has submitted every
 * window puts it back on top, and raising it on each frame keeps it there.
 *
 * The raise does not focus the bar. BringWindowToDisplayFront moves it in the
 * display order alone, so the field the operator is typing into keeps the
 * keyboard and the next keystroke still lands there.
 *
 * A popup or a modal is a window too and is drawn among the ordinary ones, so
 * the same raise would put the dock over a question the operator is answering.
 * Every open popup is raised again after the bar, in stack order, which leaves
 * the topmost question at the very front. The bar yields to those and to
 * nothing else.
 */
void raise_bar_to_front() {
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    ImGuiContext& context = *GImGui;
    for (const ImGuiPopupData& popup : context.OpenPopupStack) {
        if (popup.Window != nullptr)
            ImGui::BringWindowToDisplayFront(popup.Window);
    }
}

}

namespace hedgr::ui::render {

bool hover_is_new_entry(std::string_view previous,
    std::string_view hovered) noexcept {
    return !hovered.empty() && hovered != previous;
}

float shortcut_bar_height(const render_input& input, bool desk_docked) {
    if ((input.shortcuts.size() == 0 && !desk_docked)
        || !shows_instances(input))
        return 0.0f;
    return entry_size + 2.0f * ImGui::GetStyle().WindowPadding.y;
}

void draw_shortcut_bar(const render_input& input, bool desk_docked,
    shortcut_bar_result& result, shortcut_bar_state& state) {
    /*
     * An empty bar draws nothing, and so does one whose store is shut: the
     * entries are catalogue and the catalogue is what the master password
     * opens. Drawing a bar of entries the interface cannot read would be a bar
     * of another session's shortcuts.
     */
    if (shortcut_bar_height(input, desk_docked) == 0.0f)
        return;

    /*
     * Anchored to the bottom centre of the viewport, which is where a dock
     * belongs and where it covers the least of what an operator is reading.
     * The library owns the bar's own drawing, so nothing here restyles it.
     */
    const ImGui::ImCoolBarSettings settings(ImVec2(0.5f, 1.0f), entry_size,
        entry_hovered_size, entry_anim_step, 0.5f, ImCoolBarFlags_Horizontal);

    /*
     * BeginCoolBar ends the window itself when it returns false, so the only
     * End is the one on the true path.
     */
    if (!ImGui::BeginCoolBar("##hedgr-shortcuts", settings))
        return;

    /*
     * The walk has already submitted every ordinary window, so this is the
     * first point in the frame at which the bar can be put above all of them.
     */
    raise_bar_to_front();

    /*
     * The one entry the pointer is on, remembered until the walk ends so the
     * tick is decided from a single answer rather than from whichever entry
     * happened to be visited last.
     */
    const domain::shortcut* hovered_row = nullptr;

    for (const auto& row : input.shortcuts.rows()) {
        /*
         * An item that fails to submit has nothing to draw into, which the
         * library reports rather than asserts, so the loop simply moves on.
         */
        if (!ImGui::CoolBarItem())
            continue;

        /*
         * Read before the id is pushed: the library stores each item's own
         * size under a key built from the current id stack, and a push between
         * its write and this read would make the read miss.
         */
        const float size = ImGui::GetCoolBarItemWidth();

        ImGui::PushID(row.id.c_str());
        const entry_interaction interaction = draw_entry(input, row, size,
            "entry");
        ImGui::SetItemTooltip("%s", entry_tooltip(input, row).c_str());
        ImGui::PopID();

        if (interaction.hovered)
            hovered_row = &row;

        if (interaction.clicked)
            activate_entry(input, row, result);
    }

    /*
     * The last control, present while anything is docked in the desk and
     * after the shortcuts, so it never moves an entry a reader has learnt.
     * It is on the bar because the bar is the one surface that is not itself a
     * window and so is still there when every window is docked.
     */
    if (desk_docked && ImGui::CoolBarItem()) {
        const float size = ImGui::GetCoolBarItemWidth();
        ImGui::PushID(undock_all_id);
        draw_glyph_tile(hedgr::ui::icon::move, size);
        ImGui::InvisibleButton("entry", ImVec2(size, size));
        ImGui::SetItemTooltip("Undock all windows from the desk");
        if (ImGui::IsItemClicked())
            result.undock_all = true;
        ImGui::PopID();
    }

    /*
     * The tick is decided after the walk, from the one entry the pointer is on
     * and the one it was on last frame. No hovered entry is the pointer having
     * left the bar: that raises nothing, but the memory is cleared so coming
     * back onto the same entry later is a fresh approach rather than a
     * continuation of the last one.
     */
    if (hovered_row != nullptr
        && hover_is_new_entry(state.hovered_id, hovered_row->id))
        raise_hover(input, *hovered_row);
    state.hovered_id = hovered_row != nullptr ? hovered_row->id : std::string{};

    ImGui::EndCoolBar();
}

}
