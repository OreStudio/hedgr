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
#include "hedgr.ui/render/time_text.hpp"
#include "ores.platform/time/relative_time_formatter.hpp"
#include <chrono>

namespace hedgr::ui::render {

std::string age_text(std::int64_t age_ns) {
    using clock = std::chrono::system_clock;
    const clock::time_point then{};
    const auto now = then + std::chrono::duration_cast<clock::duration>(
        std::chrono::nanoseconds{age_ns});
    return ores::platform::time::relative_time_formatter{}.format(then, now);
}

}
