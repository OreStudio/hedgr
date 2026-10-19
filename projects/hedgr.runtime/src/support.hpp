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
#ifndef HEDGR_RUNTIME_SUPPORT_HPP
#define HEDGR_RUNTIME_SUPPORT_HPP

#include "hedgr.model/state/tables.hpp"
#include "hedgr.store/sqlite/error.hpp"
#include "ores.logging/boost_severity.hpp"
#include "ores.logging/make_logger.hpp"
#include <boost/log/sources/severity_feature.hpp>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

/*
 * Private to the component: the manager, the seeder and the services share
 * these, and none of them is part of the public interface.
 */
namespace hedgr::runtime::support {

namespace model_state = hedgr::model::state;
namespace sqlite = hedgr::store::sqlite;

/**
 * @brief The logger the manager, the seeder and the services share.
 *
 * A warning that nothing reads is not a warning, so a checkout the scan could
 * not use is reported the way the rest of OreStudio reports one: through the
 * process log, which the application sets up before it starts anything.
 */
[[nodiscard]] inline auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.manager.instance_manager");
    return instance;
}

/**
 * @brief The key a running worker is registered under.
 *
 * The id separator is the one the session table uses internally to join a pair
 * as well, so a key here can never collide with either half of another pair:
 * "a" + "bc" and "ab" + "c" have to be two workers, not one.
 */
[[nodiscard]] inline std::string running_key(std::string_view instance_id,
    std::string_view account_id) {
    std::string result;
    result.reserve(instance_id.size() + account_id.size() + 1);
    result.append(instance_id);
    result.push_back('\x1f');
    result.append(account_id);
    return result;
}

/** @brief Every live window row, holes skipped, for a whole-table walk. */
[[nodiscard]] inline std::vector<model_state::window_handle>
live_windows(const model_state::window_table& windows) {
    std::vector<model_state::window_handle> result;
    const auto& layout = windows.layout();
    for (std::uint32_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;
        result.push_back(model_state::window_handle{index, layout.generation[index]});
    }
    return result;
}

/** @brief The live slot index of every row of a table, holes skipped. */
[[nodiscard]] inline std::vector<std::uint32_t>
live_indices(const model_state::slab& layout) {
    std::vector<std::uint32_t> result;
    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        result.push_back(index);
    }
    return result;
}

/** @brief A store failure as one sentence naming the operation that failed. */
[[nodiscard]] inline std::string store_failure(std::string_view what,
    const sqlite::store_error& error) {
    const auto message = std::format("{}: {} ({})", what, error.message,
        sqlite::to_string(error.code));
    BOOST_LOG_SEV(lg(), ores::logging::error) << "Store failure: " << message;
    return message;
}

/**
 * @brief The monotonic clock in nanoseconds, which is what deadlines use.
 */
[[nodiscard]] inline std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}

#endif
