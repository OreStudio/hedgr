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
#ifndef HEDGR_UI_RENDER_SHARED_CROSSHAIR_HPP
#define HEDGR_UI_RENDER_SHARED_CROSSHAIR_HPP

#include "hedgr.model/state/handle.hpp"
#include <imgui.h>
#include <optional>

namespace hedgr::ui::render {

/** @brief The colour of the shared line and its tag. */
inline constexpr ImU32 shared_crosshair_colour = IM_COL32(191, 199, 209, 217);

/**
 * @brief The time under the pointer in one docked chart, shown in the others.
 *
 * Interface scratch, like ImGui's own hover state. It is double buffered: a
 * chart reads the value the last frame wrote and writes the one the next frame
 * reads, so the order the windows are walked in never changes what a frame
 * shows. The session is part of the value because only charts of the same
 * session share a crosshair.
 */
class shared_crosshair final {
public:
    /** @brief Makes what was written last frame the value charts read. */
    void next_frame() noexcept {
        shown_ = written_;
        written_.reset();
    }

    /** @brief Records the time under the pointer for the next frame. */
    void write(hedgr::model::state::session_handle session,
        double seconds) noexcept {
        written_ = cursor{session, seconds};
    }

    /** @brief The time last frame's hovered chart of @p session had, if any. */
    [[nodiscard]] std::optional<double>
    read(hedgr::model::state::session_handle session) const noexcept {
        if (shown_ && shown_->session == session)
            return shown_->seconds;
        return std::nullopt;
    }

private:
    struct cursor final {
        hedgr::model::state::session_handle session;
        double seconds = 0.0;
    };

    std::optional<cursor> shown_;
    std::optional<cursor> written_;
};

}

#endif
