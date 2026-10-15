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
#ifndef HEDGR_MODEL_DOMAIN_PASSWORD_POLICY_HPP
#define HEDGR_MODEL_DOMAIN_PASSWORD_POLICY_HPP

#include "hedgr.model/export.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hedgr::model::domain {

/**
 * @brief One rule a deployment's password policy states.
 *
 * The list is short and closed because it is not Hedgr's: these are the rules
 * the deployment's own validator has, named so a screen can tick them off. A
 * deployment that grows a rule grows it here and in the IAM read behind it.
 */
enum class password_rule : std::uint8_t {
    /** @brief The password is at least the policy's minimum length. */
    length = 0,
    /** @brief The password carries an upper-case letter. */
    upper = 1,
    /** @brief The password carries a lower-case letter. */
    lower = 2,
    /** @brief The password carries a digit. */
    digit = 3,
    /** @brief The password carries a character from the policy's special set. */
    special = 4,
};

/** @brief The number of rules, for indexing a lookup table. */
inline constexpr std::size_t password_rule_count = 5;

/** @brief The canonical text of a rule, for a label and for a test. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(password_rule value) noexcept;

/**
 * @brief The rules a deployment says a new password must satisfy.
 *
 * A record of what one deployment answered, never a rule set Hedgr holds. The
 * difference matters: a rule the deployment changes changes what the create
 * prompt assesses, and a rule the deployment does not state is not shown and
 * not required. A client that hardcoded the rules would drift from the
 * validator that enforces them, and would confidently tick a box the server
 * was about to refuse.
 */
struct password_policy final {
    /** @brief The shortest password the deployment accepts. */
    int min_length = 0;

    /** @brief Whether an upper-case letter is required. */
    bool require_uppercase = false;

    /** @brief Whether a lower-case letter is required. */
    bool require_lowercase = false;

    /** @brief Whether a digit is required. */
    bool require_digit = false;

    /** @brief Whether a character from @c special_chars is required. */
    bool require_special = false;

    /**
     * @brief The characters that satisfy the special-character rule.
     *
     * The deployment's own set, not a set of Hedgr's: a policy that lists
     * different characters changes what the rule ticks on. Empty means the
     * rule cannot be met at all, which is why the assessment checks it rather
     * than trusting the flag alone.
     */
    std::string special_chars;
};

/**
 * @brief The rules a policy declares, in the order a screen lists them.
 *
 * A rule the policy does not require is absent rather than listed as optional.
 * A screen that listed every rule a deployment could have would describe
 * deployments other than the one being signed in to.
 */
[[nodiscard]] HEDGR_MODEL_EXPORT std::vector<password_rule>
password_rules(const password_policy& policy);

/**
 * @brief What the policy says about one password.
 *
 * A pure answer about the password typed so far. It is not a permission: the
 * deployment still decides, and the point of this is to say so while a person
 * types rather than after they submit.
 */
struct password_assessment final {
    /** @brief The rules of this policy's list that the password satisfies. */
    std::vector<password_rule> met;

    /** @brief Whether every rule the policy requires is satisfied. */
    bool valid = false;

    /**
     * @brief How strong the password is, from 0 to 4.
     *
     * Zero is an empty password. One and two are a password that breaks the
     * policy, split by whether it satisfies at least half the rules. Three is
     * a password that satisfies it, and four is one that clears the minimum
     * length by four characters as well.
     */
    int strength = 0;

    /**
     * @brief Whether @p rule is one the password satisfies.
     *
     * A search of @c met rather than a parallel array of flags, so the set a
     * screen ticks off and the set the validity check reads are one thing.
     */
    [[nodiscard]] HEDGR_MODEL_EXPORT bool meets(password_rule rule) const;
};

/**
 * @brief Assesses @p password against @p policy.
 *
 * The rules are the policy's, so this answers differently for two deployments
 * and answers nothing at all for a deployment that stated no policy. It is a
 * pure function of its two arguments on purpose: it is the one part of the
 * create prompt that can be tested without a screen, a connection or a store.
 */
[[nodiscard]] HEDGR_MODEL_EXPORT password_assessment
assess_password(std::string_view password, const password_policy& policy);

/**
 * @brief Where the policy read stands.
 *
 * Told apart because the screen has to say different things. @c loading is
 * "not yet"; @c unavailable is "the deployment did not answer", and the second
 * must not be drawn as though the policy were empty, because an empty policy
 * would be a claim and this is an absence of one.
 */
enum class policy_state : std::uint8_t {
    /** @brief The read has been asked for and has not answered. */
    loading = 0,
    /** @brief The deployment answered, and @c policy holds what it said. */
    loaded = 1,
    /** @brief The deployment did not answer, so nothing is assessed. */
    unavailable = 2,
};

/** @brief The number of fetch states, for indexing a lookup table. */
inline constexpr std::size_t policy_state_count = 3;

/** @brief The canonical text of a fetch state, for a tooltip and a test. */
[[nodiscard]] HEDGR_MODEL_EXPORT std::string_view
to_string(policy_state value) noexcept;

}

#endif
