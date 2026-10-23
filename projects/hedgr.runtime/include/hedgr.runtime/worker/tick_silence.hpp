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
#ifndef HEDGR_RUNTIME_WORKER_TICK_SILENCE_HPP
#define HEDGR_RUNTIME_WORKER_TICK_SILENCE_HPP

#include "hedgr.session/transport/session_error.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace hedgr::runtime::worker {

/**
 * @brief How long one tick read waits for an answer.
 *
 * A request waits out its timeout on a pool thread, and a chart opens a read
 * per series, so the default thirty seconds would hold every thread for half a
 * minute each when the service does not answer. A service that is up answers a
 * tick read in well under this.
 */
inline constexpr std::chrono::milliseconds tick_read_timeout{8000};

/**
 * @brief How long an instance that did not answer is not asked again.
 *
 * After one read finds the tick call unanswered, the reads queued behind it
 * would each wait the timeout out in turn. They fail at once instead, and the
 * next read after this window asks again, so a service that comes back is
 * picked up without a restart of Hedgr.
 */
inline constexpr std::int64_t tick_silence_ns = 20'000'000'000;

/** @brief Whether a failure means the service gave no answer at all. */
[[nodiscard]] inline bool is_unanswered(
    hedgr::session::transport::session_errc code) noexcept {
    using hedgr::session::transport::session_errc;
    return code == session_errc::timeout
        || code == session_errc::services_unavailable;
}

/**
 * @brief The instances whose tick call went unanswered, and until when.
 *
 * Shared by every read job of the process, because each job opens a session of
 * its own and so no session can remember what the last one found.
 */
class tick_silence final {
public:
    /** @brief Whether @p instance is inside its silence window at @p now_ns. */
    [[nodiscard]] bool silent(const std::string_view instance,
        const std::int64_t now_ns) const {
        const std::lock_guard lock(mutex_);
        const auto found = until_.find(std::string{instance});
        return found != until_.end() && now_ns < found->second;
    }

    /** @brief Starts or clears @p instance's window after a read ended. */
    void note(const std::string_view instance, const bool unanswered,
        const std::int64_t now_ns) {
        const std::lock_guard lock(mutex_);
        if (unanswered)
            until_[std::string{instance}] = now_ns + tick_silence_ns;
        else
            until_.erase(std::string{instance});
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::int64_t> until_;
};

}

#endif
