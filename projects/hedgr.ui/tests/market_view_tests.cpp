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
#include "hedgr.ui/render/market_view.hpp"
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <string>

using hedgr::ui::render::chart_span_index;
using hedgr::ui::render::chart_span_ns;
using hedgr::ui::render::load_market_view;
using hedgr::ui::render::market_view_state;
using hedgr::ui::render::parse_market_view;
using hedgr::ui::render::save_market_view;
using hedgr::ui::render::serialize_market_view;

TEST_CASE("a chart preference names a span rather than a position",
    "[market-view]") {
    CHECK(chart_span_ns(0) == 60'000'000'000);
    CHECK(chart_span_ns(3) == 3'600'000'000'000);
    CHECK(chart_span_ns(5) == 0);
    CHECK(chart_span_index(3'600'000'000'000) == 3);
    CHECK(chart_span_index(0) == 5);

    /* An index outside the list falls back to the five minute default. */
    CHECK(chart_span_ns(99) == 300'000'000'000);
    CHECK(chart_span_index(1234) == 1);
}

TEST_CASE("a chart period reads in minutes, not in term months",
    "[market-view]") {
    using hedgr::ui::render::chart_span_labels;

    /* In OreStudio's term grammar "1M" is one month; the combo means a minute. */
    CHECK(std::string{chart_span_labels[0]} == "1m");
    CHECK(std::string{chart_span_labels[3]} == "1h");

    market_view_state state;
    state.chart_span = 0;
    CHECK(serialize_market_view(state).starts_with("period=1m\n"));
}

TEST_CASE("a period saved before the relabel still reads", "[market-view]") {
    market_view_state state;
    parse_market_view("period=15M\n", state);
    CHECK(state.chart_span == 2);
    parse_market_view("period=1H\n", state);
    CHECK(state.chart_span == 3);
    parse_market_view("period=All\n", state);
    CHECK(state.chart_span == 5);
}

TEST_CASE("market view preferences survive the text form", "[market-view]") {
    market_view_state state;
    state.chart_span = 3;
    state.chart_style = 0;
    state.chart_ema = 1;
    state.term_history = 8;

    const auto text = serialize_market_view(state);
    market_view_state read;
    parse_market_view(text, read);

    CHECK(read.chart_span == 3);
    CHECK(read.chart_style == 0);
    CHECK(read.chart_ema == 1);
    CHECK(read.term_history == 8);
}

TEST_CASE("a damaged preference line leaves the other choices alone",
    "[market-view]") {
    market_view_state state;
    state.chart_span = 2;
    state.chart_style = 1;
    state.chart_ema = 0;
    state.term_history = 5;

    parse_market_view("period=not-a-span\nstyle=candles\nema=maybe\n"
        "history=99\nunknown=value\n",
        state);

    CHECK(state.chart_span == 2);
    CHECK(state.chart_style == 0);
    CHECK(state.chart_ema == 0);
    CHECK(state.term_history == 10);
}

TEST_CASE("the preferences file is written and read back", "[market-view]") {
    const auto path = std::filesystem::temp_directory_path()
        / "hedgr-market-view-test.ini";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    market_view_state saved;
    saved.chart_span = 4;
    saved.chart_style = 0;
    saved.chart_ema = 1;
    saved.term_history = 2;
    REQUIRE(save_market_view(path, saved));

    market_view_state read;
    REQUIRE(load_market_view(path, read));
    CHECK(read.chart_span == 4);
    CHECK(read.chart_style == 0);
    CHECK(read.chart_ema == 1);
    CHECK(read.term_history == 2);

    std::filesystem::remove(path, ignored);
}
