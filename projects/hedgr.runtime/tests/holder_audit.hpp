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
#ifndef HEDGR_RUNTIME_TESTS_HOLDER_AUDIT_HPP
#define HEDGR_RUNTIME_TESTS_HOLDER_AUDIT_HPP

#include "hedgr.model/state/handle.hpp"
#include "hedgr.runtime/manager/instance_manager.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace hedgr::testing {

/**
 * @brief Checks that the series table's watchers are exactly the open windows.
 *
 * Every valid handle the window table holds is one watcher of its row, and no
 * row has a watcher no window accounts for. A reference count leaks silently,
 * so a lifecycle test calls this last and a leaked or doubled holder fails it.
 */
inline void audit_holders(const runtime::manager::instance_manager& manager) {
    const auto& windows = manager.windows();
    const auto& series = manager.market();

    std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> held;
    std::size_t valid = 0;
    const auto& layout = windows.layout();
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        const model::state::window_handle window{
            index, layout.generation[index]};
        for (const auto handle : windows.series(window)) {
            if (!series.contains(handle))
                continue;
            ++held[{handle.index, handle.generation}];
            ++valid;
        }
    }

    for (const auto& [key, count] : held) {
        INFO("series slot " << key.first << " generation " << key.second);
        CHECK(series.watchers(model::state::series_handle{key.first,
                  key.second}) == count);
    }
    CHECK(series.watcher_total() == valid);
}

}

#endif
