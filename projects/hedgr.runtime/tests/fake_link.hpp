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
#include <optional>
#include <string>
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

    std::optional<session::transport::session_error> connect_error;
    std::optional<session::transport::session_error> resume_error;
    std::optional<session::transport::session_error> rates_error;
    std::optional<session::transport::session_error> names_error;
    std::optional<session::transport::session_error> images_error;
    std::optional<session::transport::session_error> image_error;

    mutable int connects = 0;
    mutable int resumes = 0;
    mutable int crm_calls = 0;
    mutable int names_calls = 0;
    mutable int image_calls = 0;

    [[nodiscard]] session::transport::result<void> connect() override {
        ++connects;
        if (connect_error)
            return std::unexpected(*connect_error);
        return {};
    }

    [[nodiscard]] session::transport::result<void> resume(std::string_view,
        std::string_view) override {
        ++resumes;
        if (resume_error)
            return std::unexpected(*resume_error);
        return {};
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
};

}

#endif
