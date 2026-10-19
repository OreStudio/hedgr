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
#include "hedgr.model/domain/password_policy.hpp"
#include "ores.security/validation/password_validator.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

using hedgr::model::domain::assess_password;
using hedgr::model::domain::password_policy;
using ores::security::validation::password_validator;

namespace {

/** @brief The server's policy as the IAM read states it to Hedgr. */
password_policy server_policy() {
    const auto rules = password_validator::policy();
    return password_policy{
        .min_length = static_cast<int>(rules.min_length),
        .require_uppercase = rules.require_uppercase,
        .require_lowercase = rules.require_lowercase,
        .require_digit = rules.require_digit,
        .require_special = rules.require_special,
        .special_chars = rules.special_chars};
}

/**
 * @brief Every combination of the character classes the rules look at.
 *
 * Each subset is padded with spaces, which no rule counts, to lengths either
 * side of the server's minimum. The NUL and the byte above 0x7f are in the set
 * because they are where a reimplementation drifts from the server's C calls.
 */
std::vector<std::string> corpus() {
    constexpr std::array<char, 7> pieces{
        'A', 'a', '1', '!', '\0', static_cast<char>(0xe9), '~'};
    std::vector<std::string> passwords;
    for (unsigned mask = 0; mask < (1u << pieces.size()); ++mask) {
        std::string core;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if ((mask & (1u << i)) != 0)
                core.push_back(pieces[i]);
        }
        for (const std::size_t length : {std::size_t{0}, std::size_t{11},
                 std::size_t{12}, std::size_t{16}}) {
            auto password = core;
            if (password.size() < length)
                password.append(length - password.size(), ' ');
            passwords.push_back(std::move(password));
        }
    }
    return passwords;
}

}

TEST_CASE("the assessment agrees with the server's validator on its own policy",
    "[password]") {
    const auto policy = server_policy();
    for (const auto& password : corpus()) {
        INFO("password bytes: " << password.size());
        CHECK(assess_password(password, policy).valid
            == password_validator::validate(password).is_valid);
    }
}

TEST_CASE("a NUL counts as special, as it does on the server", "[password]") {
    const std::string password{"Abcdefghij1\0", 12};

    REQUIRE(password_validator::validate(password).is_valid);
    REQUIRE(assess_password(password, server_policy()).valid);
}
