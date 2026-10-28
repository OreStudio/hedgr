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
#include <memory>
#include <string_view>
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

private:
    session_client& client_;
};

}

#endif
