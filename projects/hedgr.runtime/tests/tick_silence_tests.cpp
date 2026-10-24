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
#include "hedgr.runtime/worker/tick_silence.hpp"
#include <catch2/catch_test_macros.hpp>

namespace {

using hedgr::runtime::worker::is_unanswered;
using hedgr::runtime::worker::tick_silence;
using hedgr::runtime::worker::tick_silence_ns;
using hedgr::runtime::worker::tick_read_timeout;
using hedgr::session::transport::session_errc;

}

TEST_CASE("an instance that did not answer is not asked again for a while",
    "[tick_silence]") {
    tick_silence silence;
    CHECK_FALSE(silence.silent("a", 100));
    silence.note("a", true, 100);
    CHECK(silence.silent("a", 100));
    CHECK(silence.silent("a", 100 + tick_silence_ns - 1));
    CHECK_FALSE(silence.silent("a", 100 + tick_silence_ns));
}

TEST_CASE("silence is per instance and an answer clears it", "[tick_silence]") {
    tick_silence silence;
    silence.note("a", true, 0);
    CHECK_FALSE(silence.silent("b", 1));
    silence.note("a", false, 5);
    CHECK_FALSE(silence.silent("a", 6));
}

TEST_CASE("only a missing answer counts as silence, not a refusal",
    "[tick_silence]") {
    CHECK(is_unanswered(session_errc::timeout));
    CHECK(is_unanswered(session_errc::services_unavailable));
    CHECK_FALSE(is_unanswered(session_errc::range_too_large));
    CHECK_FALSE(is_unanswered(session_errc::party_required));
    CHECK_FALSE(is_unanswered(session_errc::protocol_error));
}

TEST_CASE("a tick read gives up well before the default request timeout",
    "[tick_silence]") {
    CHECK(tick_read_timeout.count() <= 10000);
    CHECK(tick_silence_ns > 0);
}
