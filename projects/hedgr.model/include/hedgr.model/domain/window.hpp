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
#ifndef HEDGR_MODEL_DOMAIN_WINDOW_HPP
#define HEDGR_MODEL_DOMAIN_WINDOW_HPP

#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/export.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>
#include <string_view>
#include <variant>

namespace hedgr::model::domain {

/**
 * @brief The discriminator that selects a window's renderer and state shape.
 *
 * Adding a kind adds a variant alternative, a renderer table entry and a menu
 * entry. It changes nothing else, so the enum is the only place the set of
 * kinds is enumerated.
 */
enum class window_kind : std::uint8_t {
    /** @brief A timeline of one numeric series. */
    chart = 0,
    /** @brief A paged table of one resource. */
    grid = 1,
    /** @brief A message with an optional low-level detail line. */
    notice = 2,
    /** @brief The Instances window itself: the root surface. */
    instance_manager = 3,
    /** @brief The accounts of one instance, browsed as a tree and a list. */
    accounts = 4,
    /** @brief The live session for one account on one instance. */
    workspace = 5,
    /**
     * @brief The deployment's own screens, for a super administrator.
     *
     * One window per session, opened when IAM resolves the session to the
     * system tenant. It hosts a menu bar and a tab strip rather than showing
     * one thing, because it is the surface an administrator works from rather
     * than a read about one record.
     */
    command_centre = 6,
    /** @brief The market series a session may plot, as a chooser list. */
    series_browser = 7,
    /** @brief One interest-rate curve as a term structure. */
    term_structure = 8,
    /** @brief A cross-rates matrix for one party. */
    crm_matrix = 9,
};

/** @brief The number of window kinds, for indexing the renderer table. */
inline constexpr std::size_t window_kind_count = 10;

/** @brief The canonical text of a kind, as stored in the local database. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(window_kind value) noexcept;

/** @brief Parses a window kind from its canonical text. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::expected<window_kind, std::string>
window_kind_from_string(std::string_view text);

/** @brief How a chart draws its points. */
enum class chart_style : std::uint8_t {
    candles = 0,
    line = 1,
};

/** @brief How one chart is read: its period, its style and its overlay. */
struct chart_view final {
    /** @brief The span the chart plots, in nanoseconds, or zero for all. */
    std::int64_t span_ns = std::int64_t{5} * 60 * 1'000'000'000;

    /** @brief Candles or a line. */
    chart_style style = chart_style::line;

    /** @brief Whether the moving average is drawn over the points. */
    bool ema = false;

    friend bool operator==(const chart_view&, const chart_view&) = default;
};

/** @brief State of a chart window. */
struct chart_state final {
    /** @brief Human label of the series, shown in the title. */
    std::string label;

    /** @brief Key of the market series this window plots. */
    std::string series_id;

    /** @brief Ring capacity of the plotted series. */
    std::uint32_t max_points = 600;

    /**
     * @brief The chart's own view, once it has one.
     *
     * Empty for a chart opened a moment ago or saved before charts kept their
     * own view. Such a chart draws with the interface's remembered defaults
     * and saves them as its own on its first frame, so a later change to the
     * defaults by another chart does not move it.
     */
    std::optional<chart_view> view;
};

/** @brief State of a term-structure window. */
struct term_structure_state final {
    /** @brief The curve's name, shown in the title. */
    std::string label;

    /** @brief The curve identity: the series URI without its tenor. */
    std::string curve_key;

    /** @brief Member series keys, in tenor order. */
    std::vector<std::string> member_ids;

    /** @brief Member tenors, index for index with member_ids. Empty for a
     *  composite curve, whose terms are read from its observations. */
    std::vector<std::string> member_tenors;
};

/** @brief State of a cross-rates matrix window. */
struct crm_matrix_state final {
    /** @brief Party whose CRM rates are shown. */
    std::string party_id;
    /** @brief CRM name, or empty for every enabled CRM. */
    std::string crm_name;
    /** @brief Whether to request reciprocal backfill. */
    bool reciprocal = false;
};

/** @brief State of a grid window. */
struct grid_state final {
    /** @brief Resource name, for example "trades". */
    std::string resource;

    /** @brief Tenant the page is read for. Empty means the session default. */
    std::string tenant_id;

    /** @brief Rows per page. */
    std::uint32_t page_size = 200;
};

/** @brief State of a notice window. */
struct notice_state final {
    /** @brief Short headline. */
    std::string title;

    /** @brief The user-facing message. */
    std::string text;

    /** @brief The low-level error, shown dimmed and second. */
    std::string detail;
};

/**
 * @brief State of an accounts window.
 *
 * The only fact an accounts window carries is which instance's accounts it
 * shows, and it has to be persisted: the window wall is restored on the next
 * launch and there is nobody to ask which instance a restored window was for.
 * Everything else the window shows is read from the live tables.
 */
struct accounts_state final {
    /** @brief Key of the instance whose accounts are browsed. */
    std::string instance_id;
};

/** @brief State of a workspace window. */
struct workspace_state final {
    /**
     * @brief Key of the account the session presents.
     *
     * The instance comes from the window's session binding rather than from
     * here: a workspace is always bound to a session, and storing the instance
     * twice would be two places for the pair to disagree.
     */
    std::string account_id;
};

/** @brief State of the market series chooser. */
struct series_browser_state final {};

/**
 * @brief State of a command centre window.
 *
 * The only fact it carries is which deployment it is about, and it is stored
 * for the same reason the accounts window stores its instance: the title and
 * the session-loss card have to name the deployment when there is no live
 * session to ask. The tabs and the selection are the window's scratch, not
 * state the runtime owns, so they live in the interface beside every other
 * window's scratch.
 */
struct command_centre_state final {
    /** @brief Key of the instance whose deployment is shown. */
    std::string instance_id;
};

/** @brief The kind-specific state of a window. */
using window_state = std::variant<chart_state, grid_state, notice_state,
    accounts_state, workspace_state, command_centre_state,
    series_browser_state, term_structure_state, crm_matrix_state>;

/**
 * @brief The persisted description of a window.
 *
 * This is the cold shape: it says what the window is, not what it is doing.
 * The live per-frame columns live in the window table, so a renderer reads one
 * contiguous array per attribute rather than chasing a description object.
 *
 * A null @c session means the window is not bound to a session. Only
 * window_kind::instance_manager uses that, and it is the reason the field is
 * optional rather than a session_key with empty strings.
 */
struct window_spec final {
    /** @brief Stable id, used as the ImGui id and never shown. */
    std::string id;

    /** @brief What the window renders. */
    window_kind kind = window_kind::notice;

    /** @brief The session the window belongs to, if any. */
    std::optional<session_key> session;

    /** @brief How often the window's data is refreshed. */
    std::chrono::milliseconds cadence{1000};

    /** @brief The kind-specific state. */
    window_state state = notice_state{};
};

}

#endif
