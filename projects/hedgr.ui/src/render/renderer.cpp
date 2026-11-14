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

#include <array>

namespace hedgr::ui::render {

/*
 * The renderer of each kind, registered in one place.
 *
 * Indexed by the window kind's ordinal, which is why the enum's values start
 * at zero and run without gaps. Adding a kind to the enum, and nothing else,
 * fails here: the initialiser count no longer matches, and the consteval check
 * below rejects a nil entry, so a kind that has no renderer cannot compile.
 */
void render_instances_window(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_accounts_window(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_workspace_window(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_crm_matrix(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_term_structure(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_vol_surface(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_rate_ladder(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_command_centre_window(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_series_browser(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_mini_plot(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_chart(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_grid(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);
void render_notice(hedgr::model::state::window_handle,
    const render_input&, window_ui_state&, hedgr::ui::widget::status_bar&, lock_control&);

namespace {

/**
 * @brief Rejects a table in which some kind has no renderer.
 *
 * A missing initialiser in the array below would otherwise become a null
 * pointer that only fails at run time, when that window happened to open.
 * Checking at compile time turns it into a build error instead.
 */
consteval bool every_kind_has_a_renderer(
    const std::array<renderer_fn, hedgr::model::domain::window_kind_count>& table) {
    for (const auto entry : table) {
        if (entry == nullptr)
            return false;
    }
    return true;
}

}

renderer_fn renderer_for(hedgr::model::domain::window_kind kind) noexcept {
    constexpr std::array<renderer_fn, hedgr::model::domain::window_kind_count> table{
        &render_chart,
        &render_grid,
        &render_notice,
        &render_instances_window,
        &render_accounts_window,
        &render_workspace_window,
        &render_command_centre_window,
        &render_series_browser,
        &render_term_structure,
        &render_crm_matrix,
        &render_mini_plot,
        &render_vol_surface,
        &render_rate_ladder,
    };
    static_assert(every_kind_has_a_renderer(table),
        "every window kind must have a renderer in the table above");

    const auto index = static_cast<std::size_t>(kind);
    if (index >= table.size())
        return nullptr;
    return table[index];
}

}
