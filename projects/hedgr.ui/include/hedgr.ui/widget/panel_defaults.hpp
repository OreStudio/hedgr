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
#ifndef HEDGR_UI_WIDGET_PANEL_DEFAULTS_HPP
#define HEDGR_UI_WIDGET_PANEL_DEFAULTS_HPP

#include "hedgr.ui/export.hpp"
#include <cstdint>

namespace hedgr::ui::widget {

/**
 * @brief A collapsible panel a window draws.
 *
 * Named rather than an anonymous bool, so a panel's default is stated once,
 * here, and a test can read it. The Instances window's chart is not one of
 * these: it is drawn in the window and has no collapsed state to default.
 */
enum class panel_kind : std::uint8_t {
    /** @brief The Workspace's session facts: instance, principal, tenant. */
    session_facts = 0,
};

/**
 * @brief Whether a collapsible panel is open the first time it is drawn.
 *
 * The caller supplies this through @c ImGui::SetNextItemOpen, which is the only
 * thing that decides it. ImGui's collapsing header keeps its own open state
 * under the header's id, and a header is drawn with no default of its own: a
 * bool initialised to true in the window state and handed to the header does
 * nothing, because the header overwrites it from its storage on the first frame
 * it is drawn. That is why this rule has to be applied as a call rather than as
 * an initialiser, and why it is a function here that a test can read.
 *
 * The remaining panel is a summary of what its window is about, so it opens.
 * The state is the interface's own scratch and is not persisted between runs;
 * it is not written to the store, and the ini carries window geometry rather
 * than the open state of a header.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool panel_default_open(panel_kind kind) noexcept;

}

#endif
