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
#ifndef HEDGR_RUNTIME_TESTS_FAKE_LINK_HPP
#define HEDGR_RUNTIME_TESTS_FAKE_LINK_HPP

#include "hedgr.session/transport/session_link.hpp"
#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace hedgr::testing {

/**
 * @brief A session link that answers from prepared values.
 *
 * Every call has a value and an optional failure, and the calls a worker is
 * expected to make are counted, so a test says what the deployment answered
 * without a broker.
 */
struct fake_session_link final : session::transport::session_link {
    using client = session::transport::session_client;

    std::vector<client::crm_rate> rates;
    std::vector<client::pair_convention> conventions;
    std::vector<std::string> names;
    std::vector<client::currency_image> images;
    client::image_content image;
    session::transport::deployment_read deployment_read;
    std::map<std::string, std::vector<session::transport::market_point>> tails;
    std::vector<session::transport::market_series_summary> catalogue;
    session::transport::market_series_read points_read;
    std::vector<hedgr::model::domain::market_provider_row> providers;

    std::optional<session::transport::session_error> connect_error;
    std::optional<session::transport::session_error> resume_error;
    std::optional<session::transport::session_error> rates_error;
    std::optional<session::transport::session_error> names_error;
    std::optional<session::transport::session_error> images_error;
    std::optional<session::transport::session_error> catalogue_error;
    std::optional<session::transport::session_error> points_error;
    std::optional<session::transport::session_error> providers_error;
    std::optional<session::transport::session_error> image_error;
    std::optional<session::transport::session_error> deployment_error;
    std::optional<session::transport::session_error> tail_error;

    mutable int connects = 0;
    mutable int resumes = 0;
    mutable int disconnects = 0;
    mutable int crm_calls = 0;
    mutable int names_calls = 0;
    mutable int image_calls = 0;
    mutable int tail_calls = 0;
    mutable std::atomic<int> pings{0};
    bool connected = false;
    bool authenticated = false;

    /**
     * @brief Whether a liveness request answers.
     *
     * Atomic because the actor asks on its own thread while the test flips it,
     * which is how an outage and its recovery are scripted.
     */
    std::atomic<bool> ping_answers{true};

    /** @brief How long a liveness request takes, to stand in for a slow one. */
    std::chrono::milliseconds ping_delay{};

    [[nodiscard]] session::transport::result<void> connect() override {
        ++connects;
        if (connect_error)
            return std::unexpected(*connect_error);
        connected = true;
        return {};
    }

    [[nodiscard]] session::transport::result<void> resume(std::string_view,
        std::string_view) override {
        ++resumes;
        if (resume_error)
            return std::unexpected(*resume_error);
        authenticated = true;
        return {};
    }

    void disconnect() override {
        ++disconnects;
        connected = false;
        authenticated = false;
    }

    [[nodiscard]] bool is_connected() const noexcept override {
        return connected;
    }

    [[nodiscard]] bool is_authenticated() const noexcept override {
        return authenticated;
    }

    [[nodiscard]] session::transport::result<void> ping() const override {
        ++pings;
        if (ping_delay.count() > 0)
            std::this_thread::sleep_for(ping_delay);
        if (!ping_answers)
            return std::unexpected(session::transport::session_error{
                .code = session::transport::session_errc::timeout,
                .message = "the liveness request timed out"});
        return {};
    }

    [[nodiscard]] ores::nats::service::subscription subscribe(
        std::string_view,
        ores::nats::message_handler) const override {
        throw std::logic_error("the fake link cannot subscribe");
    }

    [[nodiscard]] session::transport::result<std::vector<client::crm_rate>>
    crm_rates(std::string_view, std::string_view, bool) const override {
        ++crm_calls;
        if (rates_error)
            return std::unexpected(*rates_error);
        return rates;
    }

    [[nodiscard]] session::transport::result<
        std::vector<client::pair_convention>>
    currency_pair_conventions() const override {
        return conventions;
    }

    [[nodiscard]] session::transport::result<std::vector<std::string>>
    crm_names(std::string_view) const override {
        ++names_calls;
        if (names_error)
            return std::unexpected(*names_error);
        return names;
    }

    [[nodiscard]] session::transport::result<std::vector<client::currency_image>>
    currency_images() const override {
        if (images_error)
            return std::unexpected(*images_error);
        return images;
    }

    [[nodiscard]] session::transport::result<client::image_content> image_by_id(
        std::string_view) const override {
        ++image_calls;
        if (image_error)
            return std::unexpected(*image_error);
        return image;
    }

    [[nodiscard]] session::transport::result<
        session::transport::deployment_read>
    deployment() const override {
        if (deployment_error)
            return std::unexpected(*deployment_error);
        return deployment_read;
    }

    [[nodiscard]] session::transport::result<
        session::transport::market_series_read>
    market_series_tail(std::string_view series_id,
        std::size_t) const override {
        ++tail_calls;
        if (tail_error)
            return std::unexpected(*tail_error);
        session::transport::market_series_read read;
        if (const auto found = tails.find(std::string{series_id});
            found != tails.end())
            read.points = found->second;
        return read;
    }

    [[nodiscard]] session::transport::result<
        std::vector<session::transport::market_series_summary>>
    market_series() const override {
        if (catalogue_error)
            return std::unexpected(*catalogue_error);
        return catalogue;
    }

    [[nodiscard]] session::transport::result<
        session::transport::market_series_read>
    market_series_points(std::string_view,
        const hedgr::model::domain::market_series_identity&) const override {
        if (points_error)
            return std::unexpected(*points_error);
        return points_read;
    }

    [[nodiscard]] session::transport::result<
        session::transport::market_series_read>
    market_series_history(std::string_view, std::int64_t, std::int64_t,
        const hedgr::model::domain::market_series_identity&) const override {
        if (points_error)
            return std::unexpected(*points_error);
        return points_read;
    }

    [[nodiscard]] session::transport::result<
        std::vector<hedgr::model::domain::market_provider_row>>
    list_providers() const override {
        if (providers_error)
            return std::unexpected(*providers_error);
        return providers;
    }
};

}

#endif
