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
#include <algorithm>
#include <array>
#include <cctype>

namespace hedgr::model::domain {

namespace {

using text_table = std::array<std::string_view, password_rule_count>;

constexpr text_table rule_texts{"length", "upper", "lower", "digit", "special"};

constexpr std::array<std::string_view, policy_state_count> state_texts{
    "loading", "loaded", "unavailable"};

/** @brief Whether @p password carries a character @p wanted accepts. */
template <typename Predicate>
[[nodiscard]] bool carries(std::string_view password, Predicate wanted) {
    return std::any_of(password.begin(), password.end(),
        [wanted](char character) {
            return wanted(static_cast<unsigned char>(character)) != 0;
        });
}

/**
 * @brief Whether @p password carries one of @p accepted's characters.
 *
 * A search of the policy's own set rather than a test against a character
 * class, because the set is the deployment's to choose. A set it left empty
 * cannot be satisfied by anything, which is why the caller checks that too.
 */
[[nodiscard]] bool carries_any_of(std::string_view password,
    std::string_view accepted) {
    return std::any_of(password.begin(), password.end(),
        [accepted](char character) {
            return accepted.find(character) != std::string_view::npos;
        });
}

}

std::string_view to_string(password_rule value) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < rule_texts.size() ? rule_texts[index] : "unknown";
}

std::string_view to_string(policy_state value) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < state_texts.size() ? state_texts[index] : "unknown";
}

std::vector<password_rule> password_rules(const password_policy& policy) {
    /*
     * Length first and always, because every policy has one. The rest follow in
     * the order a person reads them, and each appears only when the deployment
     * asks for it.
     */
    std::vector<password_rule> rules{password_rule::length};
    if (policy.require_uppercase)
        rules.push_back(password_rule::upper);
    if (policy.require_lowercase)
        rules.push_back(password_rule::lower);
    if (policy.require_digit)
        rules.push_back(password_rule::digit);
    if (policy.require_special)
        rules.push_back(password_rule::special);
    return rules;
}

bool password_assessment::meets(password_rule rule) const {
    return std::find(met.begin(), met.end(), rule) != met.end();
}

password_assessment assess_password(std::string_view password,
    const password_policy& policy) {
    password_assessment result;

    /*
     * The length rule is compared as the signed width the policy states it in,
     * so a policy that somehow carried a negative minimum is met by every
     * password rather than by none of them: the comparison is the deployment's
     * own and this must not invert it.
     */
    const auto length = static_cast<std::int64_t>(password.size());
    if (length >= static_cast<std::int64_t>(policy.min_length))
        result.met.push_back(password_rule::length);

    if (policy.require_uppercase
        && carries(password, [](int character) { return std::isupper(character); }))
        result.met.push_back(password_rule::upper);
    if (policy.require_lowercase
        && carries(password, [](int character) { return std::islower(character); }))
        result.met.push_back(password_rule::lower);
    if (policy.require_digit
        && carries(password, [](int character) { return std::isdigit(character); }))
        result.met.push_back(password_rule::digit);
    if (policy.require_special && !policy.special_chars.empty()
        && carries_any_of(password, policy.special_chars))
        result.met.push_back(password_rule::special);

    const auto rules = password_rules(policy);
    result.valid = std::all_of(rules.begin(), rules.end(),
        [&result](password_rule rule) { return result.meets(rule); });

    if (password.empty()) {
        result.strength = 0;
    } else if (result.valid) {
        /*
         * Meeting the policy is three. The fourth is the policy cleared with
         * room to spare, which is the only thing above "acceptable" this can
         * claim without guessing at entropy.
         */
        const auto beyond = static_cast<std::int64_t>(policy.min_length) + 4;
        result.strength = length >= beyond ? 4 : 3;
    } else {
        /*
         * Half the rules, rounded up, is the line between a password that is
         * merely short of the policy and one that has barely started. The
         * count is over every rule met, including any the policy does not
         * require, which cannot happen: a rule the policy does not state is
         * not assessed and never enters the set.
         */
        const auto half = static_cast<std::size_t>((rules.size() + 1) / 2);
        result.strength = result.met.size() >= half ? 2 : 1;
    }

    return result;
}

}
