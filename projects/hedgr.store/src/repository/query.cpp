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
#include "hedgr.store/repository/query.hpp"
#include "hedgr.store/sqlite/database.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace hedgr::store::repository {

std::string timestamp_now() {
    const auto now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

sqlite::store_error query_failed(const rfl::Error& error) {
    /*
     * A statement that will not prepare and a statement that will not step are
     * the same kind of sentence to sqlgen, and the caller of a repository cares
     * about neither: both are the store refusing the operation. The
     * classification that matters -- a constraint rather than a fault -- is
     * made once, in the sqlite facet, and is the only one a caller switches on.
     */
    return sqlite::from_sqlgen(error, sqlite::store_errc::step_failed);
}

}
