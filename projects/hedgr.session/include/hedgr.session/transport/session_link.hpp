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
#ifndef HEDGR_SESSION_TRANSPORT_SESSION_LINK_HPP
#define HEDGR_SESSION_TRANSPORT_SESSION_LINK_HPP

#include "hedgr.session/export.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace hedgr::session::transport {

/**
 * @brief The calls a worker makes on one deployment connection.
 *
 * A worker's job is to turn answers into events, not to own a connection. This
 * is the seam that lets that mapping be tested against a fake: the real
 * implementation is `session_client_link`, which forwards to a
 * `session_client` and holds no logic.
 *
 * The surface starts with the calls the read workers make today and grows as
 * each worker is converted, so a fake only implements what its test needs.
 */
class HEDGR_SESSION_EXPORT session_link {
public:
    virtual ~session_link() = default;

    /** @brief Opens the connection. */
    [[nodiscard]] virtual result<void> connect() = 0;

    /** @brief Resumes the session's token on the connection. */
    [[nodiscard]] virtual result<void> resume(std::string_view token,
        std::string_view principal) = 0;

    /** @brief Closes the connection and drops any subscription. */
    virtual void disconnect() = 0;

    /** @brief Whether the connection is open. */
    [[nodiscard]] virtual bool is_connected() const noexcept = 0;

    /** @brief Whether the connection has a resumed session. */
    [[nodiscard]] virtual bool is_authenticated() const noexcept = 0;

    /**
     * @brief Asks the deployment whether it is there, and answers or fails.
     *
     * The liveness request, and the one read that needs no credential: it is
     * the deployment's bootstrap status, which a broker with no services
     * behind it cannot answer. A closed connection fails it outright and an
     * unanswered one fails it at the transport's timeout, so a caller learns
     * the link is gone from the failure rather than from a flag.
     */
    [[nodiscard]] virtual result<void> ping() const = 0;

    /**
     * @brief Signs in with a principal and a password over the connection.
     *
     * The token the answer carries is held by the implementation and presented
     * on every later authenticated call, so a worker never handles it beyond
     * the event it reports.
     */
    [[nodiscard]] virtual result<session_account> authenticate(
        std::string_view principal, std::string_view password) = 0;

    /**
     * @brief Scopes a just-signed-in session to one party.
     *
     * The login flow's single-use exchange, which only accepts the narrowly
     * scoped token a sign-in issued.
     */
    [[nodiscard]] virtual result<void> select_party(
        std::string_view party_id) = 0;

    /** @brief Moves a live session to another party. */
    [[nodiscard]] virtual result<void> switch_party(
        std::string_view party_id) = 0;

    /**
     * @brief Replaces the held token without the password.
     *
     * @return the lifetime of the new token in seconds, or why the instance
     * refused.
     */
    [[nodiscard]] virtual result<int> refresh() = 0;

    /**
     * @brief Signs out, dropping the token whatever the instance says.
     *
     * A sign-out is a state rather than an event: this process holds no
     * credential afterwards even when the request cannot be delivered.
     */
    [[nodiscard]] virtual result<void> logout() = 0;

    /** @brief What the instance last answered about this session. */
    [[nodiscard]] virtual const session_account& account() const = 0;

    /** @brief Subscribes to @p subject, replacing any previous one. */
    [[nodiscard]] virtual ores::nats::service::subscription subscribe(
        std::string_view subject,
        ores::nats::message_handler handler) const = 0;

    /** @brief Every configured CRM rate for a party, in one batch call. */
    [[nodiscard]] virtual result<std::vector<session_client::crm_rate>>
    crm_rates(std::string_view party_id, std::string_view crm_name,
        bool reciprocal) const = 0;

    /** @brief Every pair convention the deployment publishes. */
    [[nodiscard]] virtual result<std::vector<session_client::pair_convention>>
    currency_pair_conventions() const = 0;

    /** @brief The enabled CRM names a party has configured. */
    [[nodiscard]] virtual result<std::vector<std::string>> crm_names(
        std::string_view party_id) const = 0;

    /** @brief Every currency the tenant has an image for, by ISO code. */
    [[nodiscard]] virtual result<std::vector<session_client::currency_image>>
    currency_images() const = 0;

    /** @brief One image of the tenant, by its identifier. */
    [[nodiscard]] virtual result<session_client::image_content> image_by_id(
        std::string_view image_id) const = 0;

    /** @brief The deployment's tenant counts and roster. */
    [[nodiscard]] virtual result<deployment_read> deployment() const = 0;

    /** @brief At least @p count of a series' newest ticks. */
    [[nodiscard]] virtual result<market_series_read> market_series_tail(
        std::string_view series_id, std::size_t count) const = 0;

    /** @brief The series this login may plot. */
    [[nodiscard]] virtual result<std::vector<market_series_summary>>
    market_series() const = 0;

    /**
     * @brief The first load of one series: its newest ticks.
     *
     * @p timeout bounds the call, because a chart's read must not hold the
     * session's connection for the whole request timeout: a service that is
     * not answering is a chart that is not drawing either.
     */
    [[nodiscard]] virtual result<market_series_read> market_series_points(
        std::string_view series_id,
        const hedgr::model::domain::market_series_identity& identity,
        std::chrono::milliseconds timeout) const = 0;

    /** @brief One series' ticks over a span, bounded by @p timeout. */
    [[nodiscard]] virtual result<market_series_read> market_series_history(
        std::string_view series_id, std::int64_t from_ns,
        std::int64_t to_ns,
        const hedgr::model::domain::market_series_identity& identity,
        std::chrono::milliseconds timeout) const = 0;

    /** @brief Every market data provider and its kind. */
    [[nodiscard]] virtual result<
        std::vector<hedgr::model::domain::market_provider_row>>
    list_providers() const = 0;

    /** @brief The curves the deployment publishes for this session's party. */
    [[nodiscard]] virtual result<
        std::vector<hedgr::model::domain::published_curve>>
    published_curves(std::uint32_t limit) const = 0;

    /** @brief One published curve's most recent instants, newest first. */
    [[nodiscard]] virtual result<std::vector<curve_instant>>
    published_curve_sets(std::string_view series_id,
        std::uint32_t limit) const = 0;
};

/** @brief Forwards each call to a client it does not own. */
class HEDGR_SESSION_EXPORT session_client_link final : public session_link {
public:
    explicit session_client_link(session_client& client) noexcept
        : client_(client) {
    }

    [[nodiscard]] result<void> connect() override {
        return client_.connect();
    }

    [[nodiscard]] result<void> resume(std::string_view token,
        std::string_view principal) override {
        return client_.resume(token, principal);
    }

    void disconnect() override {
        client_.disconnect();
    }

    [[nodiscard]] bool is_connected() const noexcept override {
        return client_.is_connected();
    }

    [[nodiscard]] bool is_authenticated() const noexcept override {
        return client_.is_authenticated();
    }

    [[nodiscard]] result<void> ping() const override {
        if (const auto answer = client_.bootstrap_status(); !answer)
            return std::unexpected(answer.error());
        return {};
    }

    [[nodiscard]] result<session_account> authenticate(
        std::string_view principal, std::string_view password) override {
        return client_.authenticate(principal, password);
    }

    [[nodiscard]] result<void> select_party(
        std::string_view party_id) override {
        return client_.select_party(party_id);
    }

    [[nodiscard]] result<void> switch_party(
        std::string_view party_id) override {
        return client_.switch_party(party_id);
    }

    [[nodiscard]] result<int> refresh() override {
        return client_.refresh();
    }

    [[nodiscard]] result<void> logout() override {
        return client_.logout();
    }

    [[nodiscard]] const session_account& account() const override {
        return client_.account();
    }

    [[nodiscard]] ores::nats::service::subscription subscribe(
        std::string_view subject,
        ores::nats::message_handler handler) const override {
        return client_.subscribe(subject, std::move(handler));
    }

    [[nodiscard]] result<std::vector<session_client::crm_rate>> crm_rates(
        std::string_view party_id, std::string_view crm_name,
        bool reciprocal) const override {
        return client_.crm_rates(party_id, crm_name, reciprocal);
    }

    [[nodiscard]] result<std::vector<session_client::pair_convention>>
    currency_pair_conventions() const override {
        return client_.currency_pair_conventions();
    }

    [[nodiscard]] result<std::vector<std::string>> crm_names(
        std::string_view party_id) const override {
        return client_.crm_names(party_id);
    }

    [[nodiscard]] result<std::vector<session_client::currency_image>>
    currency_images() const override {
        return client_.currency_images();
    }

    [[nodiscard]] result<session_client::image_content> image_by_id(
        std::string_view image_id) const override {
        return client_.image_by_id(image_id);
    }

    [[nodiscard]] result<deployment_read> deployment() const override {
        return client_.deployment();
    }

    [[nodiscard]] result<market_series_read> market_series_tail(
        std::string_view series_id, std::size_t count) const override {
        return client_.market_series_tail(series_id, count);
    }

    [[nodiscard]] result<std::vector<market_series_summary>> market_series()
        const override {
        return client_.market_series();
    }

    [[nodiscard]] result<market_series_read> market_series_points(
        std::string_view series_id,
        const hedgr::model::domain::market_series_identity& identity,
        std::chrono::milliseconds timeout) const override {
        return client_.market_series_points(series_id, identity, timeout);
    }

    [[nodiscard]] result<market_series_read> market_series_history(
        std::string_view series_id, std::int64_t from_ns, std::int64_t to_ns,
        const hedgr::model::domain::market_series_identity& identity,
        std::chrono::milliseconds timeout) const override {
        return client_.market_series_history(series_id, from_ns, to_ns,
            identity, timeout);
    }

    [[nodiscard]] result<
        std::vector<hedgr::model::domain::market_provider_row>>
    list_providers() const override {
        return client_.list_providers();
    }

    [[nodiscard]] result<std::vector<hedgr::model::domain::published_curve>>
    published_curves(std::uint32_t limit) const override {
        return client_.published_curves(limit);
    }

    [[nodiscard]] result<std::vector<curve_instant>> published_curve_sets(
        std::string_view series_id,
        std::uint32_t limit) const override {
        return client_.published_curve_sets(series_id, limit);
    }

private:
    session_client& client_;
};

}

#endif
