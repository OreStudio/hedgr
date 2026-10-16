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
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "ImCoolBar.h"
#include "imgui.h"
#include <cstdint>
#include <format>
#include <string>

namespace {

namespace domain = hedgr::model::domain;
namespace theme = hedgr::ui::theme;
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
 * @brief Does what an entry means, for the kinds that mean something yet.
 *
 * The accounts case goes through the same request the Instances window's own
 * toolbar uses, so opening an instance's accounts has one description rather
 * than two. A kind with no surface yet is left alone, which is deliberate: the
 * entry draws and answers its tooltip, and the click is inert rather than
 * pretending to have opened something.
 */
void activate_entry(const render_input& input, const domain::shortcut& row,
    shortcut_bar_result& result) {
    switch (row.target_kind) {
    case domain::window_kind::instance_manager:
        result.focus_instances = true;
        return;
    case domain::window_kind::accounts:
        hedgr::ui::render::request_accounts_window(input, row.target_id);
        return;
    default:
        return;
    }
}

/**
 * @brief Draws one entry and reports whether it was clicked.
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
[[nodiscard]] bool draw_entry(const render_input& input,
    const domain::shortcut& row, float size, const char* id) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner(origin.x + size, origin.y + size);
    ImDrawList* list = ImGui::GetWindowDrawList();

    const std::uint64_t texture = entry_texture(input, row);
    if (texture != 0) {
        list->AddImage(static_cast<ImTextureID>(texture), origin, corner);
    } else {
        list->AddRectFilled(origin, corner, ImGui::GetColorU32(ImGuiCol_FrameBg),
            ImGui::GetStyle().FrameRounding);

        /*
         * The glyph comes from the icon set and is centred by measuring it,
         * because a bitmap glyph has one size and the entry is the library's to
         * size. Drawing it dimmed keeps it a placeholder rather than a control
         * of its own.
         */
        const char* glyph = theme::window_kind_icon(row.target_kind);
        const ImVec2 glyph_size = ImGui::CalcTextSize(glyph);
        ImGui::SetCursorScreenPos(
            ImVec2(origin.x + (size - glyph_size.x) * 0.5f,
                origin.y + (size - glyph_size.y) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(glyph);
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(origin);
    }

    ImGui::InvisibleButton(id, ImVec2(size, size));
    return ImGui::IsItemClicked();
}

}

namespace hedgr::ui::render {

void draw_shortcut_bar(const render_input& input, shortcut_bar_result& result) {
    /*
     * An empty bar draws nothing, and so does one whose store is shut: the
     * entries are catalogue and the catalogue is what the master password
     * opens. Drawing a bar of entries the interface cannot read would be a bar
     * of another session's shortcuts.
     */
    if (input.shortcuts.size() == 0 || !shows_instances(input))
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
        const bool clicked = draw_entry(input, row, size, "entry");
        ImGui::SetItemTooltip("%s", entry_tooltip(input, row).c_str());
        ImGui::PopID();

        if (clicked)
            activate_entry(input, row, result);
    }

    ImGui::EndCoolBar();
}

}
