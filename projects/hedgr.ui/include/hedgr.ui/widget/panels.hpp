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
#ifndef HEDGR_UI_WIDGET_PANELS_HPP
#define HEDGR_UI_WIDGET_PANELS_HPP

#include "hedgr.model/state/tables.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.ui/export.hpp"
#include "hedgr.ui/widget/browser.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::ui::widget {

/**
 * @brief What the Instances window's dashboard counts.
 *
 * Read from the probe table and the instance table and nothing else, so a
 * number here is a fact rather than a rendering. Kept as a value with named
 * fields rather than as four returns or a tuple, because the panel draws them
 * in an order and a caller that mixed them up would draw a plausible lie.
 */
struct dashboard_counts final {
    /** @brief How many instances this process knows. */
    std::size_t total = 0;

    /** @brief How many have answered that they are set up. */
    std::size_t ready = 0;

    /** @brief How many have answered that they still need setting up. */
    std::size_t bootstrapping = 0;

    /** @brief How many no connection could be opened to. */
    std::size_t unreachable = 0;

    friend bool operator==(const dashboard_counts&, const dashboard_counts&) =
        default;
};

/**
 * @brief Counts the instances by what the last probe found.
 *
 * @c total counts every row, including instances no probe has answered for.
 * The three status counts partition the probed ones and deliberately overlap
 * in no direction: an instance is ready, bootstrapping or unreachable according
 * to its own two answers, and the counts that do not add up to @c total are the
 * instances nobody has asked about yet.
 *
 * "Unknown is not bootstrapping" is the rule from the glossary and it is why
 * this reads the readiness column rather than inferring from reachability: a
 * deployment whose IAM is down has said nothing, and counting it as
 * bootstrapping would turn an outage into a work item.
 */
[[nodiscard]] HEDGR_UI_EXPORT dashboard_counts count_instances(
    const hedgr::model::state::instance_table& instances,
    const hedgr::model::state::probe_table& probes);

/**
 * @brief What a session is, in the terms a Workspace shows it.
 *
 * Session facts and not permissions: the permission source the Workspace's
 * panels will eventually be driven by does not exist yet, so everything here
 * is something Hedgr already knows.
 */
struct session_facts final {
    /** @brief Whether a session row was found at all. */
    bool found = false;

    /** @brief The instance the session points at. */
    std::string instance_id;

    /** @brief The account the session presents. */
    std::string account_id;

    /** @brief The account's display name, falling back to its principal. */
    std::string account_name;

    /** @brief The principal IAM accepted. */
    std::string principal;

    /** @brief The tenant IAM resolved, empty when it named none. */
    std::string tenant;

    /** @brief The party IAM resolved, empty when it named none. */
    std::string party;

    /** @brief When the session was established, on the monotonic clock. */
    std::int64_t opened_at_ns = 0;

    /** @brief The last round trip's latency in microseconds. */
    std::int64_t latency_us = 0;

    /** @brief Where the session is in its lifecycle. */
    hedgr::model::domain::session_status status =
        hedgr::model::domain::session_status::disconnected;

    /** @brief Why the last attempt failed, or empty. */
    std::string last_error;
};

/**
 * @brief Gathers one session's facts, resolving the account through its table.
 *
 * A free function rather than a member of the window table, because it reads
 * two tables and belongs to neither, and because the Workspace's panel is the
 * only caller.
 */
[[nodiscard]] HEDGR_UI_EXPORT session_facts collect_session_facts(
    const hedgr::model::state::session_table& sessions,
    const hedgr::model::state::account_table& accounts,
    hedgr::model::state::session_handle handle);

/**
 * @brief What an account's last sign-in came to.
 *
 * Read from the session table, because a sign-in is a fact about a session and
 * not a column on an account: an account's row is configuration that does not
 * change while Hedgr runs, and the outcome of its last attempt changes every
 * time somebody tries. An account with no session row at all has never been
 * signed in from this process.
 */
struct sign_in_result final {
    /** @brief Whether the account has a session row. */
    bool ever_attempted = false;

    /** @brief Where that session is in its lifecycle. */
    hedgr::model::domain::session_status status =
        hedgr::model::domain::session_status::disconnected;

    /** @brief The tenant it last reached, empty when it has reached none. */
    std::string tenant;

    /** @brief Why the last attempt failed, or empty. */
    std::string last_error;
};

/**
 * @brief Looks up an account's last sign-in.
 *
 * A missing session row is @c ever_attempted false, which is not the same as a
 * failed attempt: an account nobody has tried is not an account that did not
 * work, and the list says so in words rather than drawing the grey of a
 * disconnected session.
 */
[[nodiscard]] HEDGR_UI_EXPORT sign_in_result last_sign_in(
    const hedgr::model::state::session_table& sessions,
    std::string_view instance_id, std::string_view account_id);

/** @brief A URL split into the pieces a form edits. */
struct host_port final {
    /**
     * @brief The scheme, including its separator, for example "nats://".
     *
     * Carried rather than assumed, because it is not this component's business
     * which transports exist and a form that rewrote "tls://" as "nats://"
     * would silently change which deployment it points at.
     */
    std::string scheme;

    /** @brief The host. */
    std::string host;

    /** @brief The port, as typed. Empty when the address names none. */
    std::string port;
};

/**
 * @brief Splits @p url into a scheme, a host and a port.
 *
 * An address with no port is split into a host and an empty port rather than
 * guessed at, because the form shows what there is: a default injected here
 * would look like configuration the operator had entered.
 */
[[nodiscard]] HEDGR_UI_EXPORT host_port split_host_port(std::string_view url);

/** @brief Puts @p scheme, @p host and @p port back into one address. */
[[nodiscard]] HEDGR_UI_EXPORT std::string join_host_port(
    const host_port& parts);

/**
 * @brief Splits comma-separated label names, trimming the spaces around them.
 *
 * Names and not ids: a person attaches a label by typing the word they gave it.
 * An empty name between two commas is dropped rather than treated as a label
 * with no name, which would be a row nobody could ever have meant to attach.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::vector<std::string>
split_labels(std::string_view text);

/** @brief Joins label names into the comma-separated form the form shows. */
[[nodiscard]] HEDGR_UI_EXPORT std::string
join_labels(const std::vector<std::string>& names);

}

#endif
