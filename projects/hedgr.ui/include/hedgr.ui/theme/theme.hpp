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
#ifndef HEDGR_UI_THEME_THEME_HPP
#define HEDGR_UI_THEME_THEME_HPP

#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/export.hpp"
#include "imgui.h"

namespace hedgr::ui::theme {

/**
 * @brief Applies the dark, dense trading-desk style.
 *
 * Call once, after the ImGui context exists. The palette is deliberately
 * muted and semi-transparent so the photographic background reads through it:
 * a panel is a pane of smoked glass over the desk, not an opaque card.
 */
HEDGR_UI_EXPORT void apply_theme();

/**
 * @brief The colour of a session status.
 *
 * Disconnected grey, connecting amber, authenticated green, failed red.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
status_colour(hedgr::model::domain::session_status value) noexcept;

/** @brief The icon naming a session status. */
[[nodiscard]] HEDGR_UI_EXPORT const char*
status_icon(hedgr::model::domain::session_status value) noexcept;

/**
 * @brief The colour of the reachability dot.
 *
 * Grey for unknown, red for unreachable, green for reachable. Grey is not a
 * shade of either answer: it is the colour of a question nobody has asked yet,
 * and drawing it as red would report an outage on a first run.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
reachability_colour(hedgr::model::domain::reachability value) noexcept;

/**
 * @brief The colour of the readiness marker.
 *
 * Grey for unknown, amber for bootstrapping, green for ready. Unknown and
 * bootstrapping are deliberately different colours rather than two greys: a
 * deployment whose IAM is down is not a deployment that is setting itself up,
 * and an operator who reads one as the other waits for something that is not
 * happening.
 */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
readiness_colour(hedgr::model::domain::readiness value) noexcept;

/** @brief The colour a label's name is drawn in. */
[[nodiscard]] HEDGR_UI_EXPORT ImU32
label_colour(hedgr::model::domain::label_colour value) noexcept;

/** @brief The icon naming a window kind. */
[[nodiscard]] HEDGR_UI_EXPORT const char*
window_kind_icon(hedgr::model::domain::window_kind value) noexcept;

}

#endif
