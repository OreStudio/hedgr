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
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using hedgr::model::domain::assess_password;
using hedgr::model::domain::password_assessment;
using hedgr::model::domain::password_policy;
using hedgr::model::domain::password_rule;
using hedgr::model::domain::password_rules;

namespace {

/*
 * The policy the tests assess against is written out per test rather than
 * shared, because the point of most of them is which rules a policy states:
 * a helper that handed every test the same policy would hide the thing under
 * test inside the fixture.
 */

/** @brief The shortest policy a deployment could state: a length and nothing. */
password_policy minimal() {
    return password_policy{.min_length = 8};
}

/** @brief A policy that requires every rule, as the shipped validator does. */
password_policy demanding() {
    return password_policy{
        .min_length = 12,
        .require_uppercase = true,
        .require_lowercase = true,
        .require_digit = true,
        .require_special = true,
        .special_chars = "!@#$%^&*()_+-=[]{}|;:,.<>?"};
}

}

TEST_CASE("a password meeting a minimal policy is valid and met", "[password]") {
    const auto result = assess_password("abcdefgh", minimal());

    REQUIRE(result.valid);
    REQUIRE(result.strength == 3);
    REQUIRE(result.met == std::vector<password_rule>{password_rule::length});
}

TEST_CASE("an empty password is zero and meets nothing", "[password]") {
    /*
     * Zero is not a shade of weak: it is the state of a field nobody has typed
     * in, and a screen that drew it as level one would report a password the
     * person has not chosen.
     */
    const auto result = assess_password("", demanding());

    REQUIRE_FALSE(result.valid);
    REQUIRE(result.strength == 0);
    REQUIRE(result.met.empty());
}

TEST_CASE("a password failing only the length rule meets every other",
    "[password]") {
    const auto result = assess_password("aB3!x", demanding());

    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.meets(password_rule::length));
    REQUIRE(result.meets(password_rule::upper));
    REQUIRE(result.meets(password_rule::lower));
    REQUIRE(result.meets(password_rule::digit));
    REQUIRE(result.meets(password_rule::special));

    /* Four of five rules met is past half of them, so level two. */
    REQUIRE(result.strength == 2);
}

TEST_CASE("a password failing only the upper-case rule", "[password]") {
    const auto result = assess_password("abcdefgh123!", demanding());

    REQUIRE_FALSE(result.valid);
    REQUIRE(result.meets(password_rule::length));
    REQUIRE_FALSE(result.meets(password_rule::upper));
    REQUIRE(result.meets(password_rule::lower));
    REQUIRE(result.meets(password_rule::digit));
    REQUIRE(result.meets(password_rule::special));
    REQUIRE(result.strength == 2);
}

TEST_CASE("a password failing only the lower-case rule", "[password]") {
    const auto result = assess_password("ABCDEFGH123!", demanding());

    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.meets(password_rule::lower));
    REQUIRE(result.meets(password_rule::upper));
    REQUIRE(result.met.size() == 4);
}

TEST_CASE("a password failing only the digit rule", "[password]") {
    const auto result = assess_password("Abcdefghijk!", demanding());

    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.meets(password_rule::digit));
    REQUIRE(result.met.size() == 4);
}

TEST_CASE("a password failing only the special-character rule", "[password]") {
    const auto result = assess_password("Abcdefgh1234", demanding());

    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.meets(password_rule::special));
    REQUIRE(result.met.size() == 4);
}

TEST_CASE("a special-character rule with no characters cannot be met",
    "[password]") {
    /*
     * A deployment that states the rule and lists nothing behind it has stated
     * a rule no password satisfies. Ticking it because the flag was set would
     * tell a person they are fine when the deployment will refuse them.
     */
    const auto policy = password_policy{
        .min_length = 4,
        .require_special = true,
        .special_chars = ""};

    const auto result = assess_password("abcdef", policy);

    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.meets(password_rule::special));
}

TEST_CASE("a rule the policy does not require is absent and not unmet",
    "[password]") {
    const auto policy = password_policy{
        .min_length = 6,
        .require_lowercase = true};

    const auto rules = password_rules(policy);
    REQUIRE(rules == std::vector<password_rule>{password_rule::length,
                            password_rule::lower});

    /*
     * The absent rules are not merely unlisted: a password that carries no
     * digit at all is valid against a policy that never asked for one.
     */
    const auto result = assess_password("abcdef", policy);
    REQUIRE(result.valid);
    REQUIRE_FALSE(result.meets(password_rule::digit));
    REQUIRE_FALSE(result.meets(password_rule::upper));
    REQUIRE_FALSE(result.meets(password_rule::special));
}

TEST_CASE("the rule list follows the policy's own flags", "[password]") {
    const auto rules = password_rules(demanding());

    REQUIRE(rules == std::vector<password_rule>{password_rule::length,
                            password_rule::upper, password_rule::lower,
                            password_rule::digit, password_rule::special});
}

TEST_CASE("the rule list is length alone for a policy that asks no more",
    "[password]") {
    const auto rules = password_rules(minimal());
    REQUIRE(rules == std::vector<password_rule>{password_rule::length});
}

TEST_CASE("the strength scale at each boundary", "[password]") {
    /*
     * Four is the policy cleared by four characters, and three is the policy
     * cleared by anything less. The boundary is pinned on both sides because
     * off-by-one is the whole risk in a fixed scale.
     */
    const auto policy = password_policy{.min_length = 8};

    REQUIRE(assess_password("", policy).strength == 0);
    REQUIRE(assess_password("abcd", policy).strength == 1);
    REQUIRE(assess_password("abcd", policy).valid == false);
    REQUIRE(assess_password("abcdefgh", policy).strength == 3);
    REQUIRE(assess_password("abcdefghij", policy).strength == 3);
    REQUIRE(assess_password("abcdefghijk", policy).strength == 3);
    REQUIRE(assess_password("abcdefghijkl", policy).strength == 4);
    REQUIRE(assess_password("abcdefghijklmno", policy).strength == 4);
}

TEST_CASE("a policy that requires every rule scores four only with room",
    "[password]") {
    const auto policy = demanding();

    /* Twelve characters is the minimum, so this is three. */
    REQUIRE(assess_password("Abcdefghij1!", policy).strength == 3);

    /* Fifteen is the minimum with three to spare, so still three. */
    REQUIRE(assess_password("Abcdefghijkl1!", policy).strength == 3);

    /* Sixteen is twelve plus four, so this is four. */
    REQUIRE(assess_password("Abcdefghijklmn1!", policy).strength == 4);
}

TEST_CASE("half the rules met is level two and under that is level one",
    "[password]") {
    const auto policy = password_policy{.min_length = 10,
        .require_uppercase = true,
        .require_lowercase = true,
        .require_digit = true,
        .require_special = true,
        .special_chars = "!"};

    /*
     * Five rules in the list, so three met is over half and two is under it.
     * The length rule counts with the rest: it is one of the rules a person is
     * being asked to satisfy, not a precondition for the others.
     *
     * The last two are one character apart on purpose: the eleventh character
     * is the one that both clears the minimum length and satisfies the digit
     * rule, so it crosses from level two to level three on its own.
     */
    REQUIRE(assess_password("abcdefgh", policy).strength == 1);
    REQUIRE(assess_password("abcdefghij", policy).strength == 1);
    REQUIRE(assess_password("abcdefghij!", policy).strength == 2);
    REQUIRE(assess_password("Abcdefghij1!", policy).strength == 3);
}
