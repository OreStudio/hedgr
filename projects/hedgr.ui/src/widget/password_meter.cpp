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
#include "hedgr.ui/widget/password_meter.hpp"

#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace hedgr::ui::widget {

namespace {

namespace domain = hedgr::model::domain;

/** @brief The number of segments the bar is drawn in. */
constexpr int strength_segments = 4;

constexpr std::array<std::string_view, 5> strength_texts{
    "empty", "weak", "fair", "meets the policy", "strong"};

/**
 * @brief The sentence for one rule, in the deployment's own terms.
 *
 * The minimum length is read from the policy rather than written into the
 * sentence, so a deployment that changes it changes what this says. A rule
 * worded with a constant would be a copy of the rules, which is exactly what
 * this is not allowed to be.
 *
 * The special set is deliberately not in the sentence. It is long, a dialog
 * this wide cannot carry it on one line, and a wrapped rule list reads as a
 * different list; the set travels on the rule's tooltip instead, where it is
 * available in full and costs the layout nothing.
 */
[[nodiscard]] std::string rule_text(domain::password_rule rule,
    const domain::password_policy& policy) {
    switch (rule) {
    case domain::password_rule::length:
        return std::format("At least {} characters", policy.min_length);
    case domain::password_rule::upper:
        return "An upper-case letter (A-Z)";
    case domain::password_rule::lower:
        return "A lower-case letter (a-z)";
    case domain::password_rule::digit:
        return "A digit (0-9)";
    case domain::password_rule::special:
        return "A special character";
    }
    return "An unknown rule";
}

/**
 * @brief The four segments of the bar, filled up to @p strength.
 *
 * The label's width is measured first and taken out of the row, because a bar
 * that claimed the whole width would push the word that says what the bar
 * means off the edge -- which is worse than no word, since a person is then
 * reading a colour and guessing at it.
 */
void draw_strength_bar(int strength, float label_width) {
    const auto tone = hedgr::ui::theme::strength_colour(strength);
    const auto filled = ImGui::ColorConvertU32ToFloat4(tone);
    const auto empty = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);

    const float gap = ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
    const float total = ImGui::GetContentRegionAvail().x - label_width - gap;
    const float each = (total - gap * static_cast<float>(strength_segments - 1))
        / static_cast<float>(strength_segments);
    const float height = ImGui::GetTextLineHeight() * 0.5f;

    for (int segment = 0; segment < strength_segments; ++segment) {
        if (segment != 0)
            ImGui::SameLine(0.0f, gap);

        const ImVec2 corner = ImGui::GetCursorScreenPos();
        /*
         * The comparison is against the segment's one-based position, so level
         * three fills three of the four segments and level four fills all of
         * them: the bar reads as a scale a person can count rather than as a
         * progress bar that is always nearly full.
         */
        const bool on = strength >= segment + 1;
        ImGui::GetWindowDrawList()->AddRectFilled(corner,
            ImVec2(corner.x + each, corner.y + height),
            ImGui::ColorConvertFloat4ToU32(on ? filled : empty));
        ImGui::Dummy(ImVec2(each, height));
    }
}

/** @brief One rule, ticked or not, on one line. */
void draw_rule(domain::password_rule rule,
    const domain::password_policy& policy,
    const domain::password_assessment& assessment) {
    const bool met = assessment.meets(rule);

    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                           hedgr::ui::theme::strength_colour(
                               met ? 3 : 0)),
        "%s", met ? "*" : "o");
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    /*
     * An unmet rule is dimmed rather than coloured as an error: a rule nobody
     * has typed enough characters for yet is not a mistake, and a screen that
     * shouted at every keystroke would be unreadable on the way to a good
     * password.
     */
    if (!met)
        ImGui::TextDisabled("%s", rule_text(rule, policy).c_str());
    else
        ImGui::TextUnformatted(rule_text(rule, policy).c_str());

    /*
     * The set the rule accepts, in full, on the rule itself. A person who
     * cannot tell whether the character they just typed counts looks here, and
     * the answer is the deployment's own list rather than a summary of it.
     */
    if (rule == domain::password_rule::special && !policy.special_chars.empty())
        ImGui::SetItemTooltip("Any of: %s", policy.special_chars.c_str());
}

}

std::string_view strength_text(int strength) noexcept {
    const auto index = static_cast<std::size_t>(std::clamp(strength, 0, 4));
    return strength_texts[index];
}

int met_rule_count(const domain::password_assessment& assessment,
    const domain::password_policy& policy) {
    const auto rules = domain::password_rules(policy);
    return static_cast<int>(std::count_if(rules.begin(), rules.end(),
        [&assessment](domain::password_rule rule) {
            return assessment.meets(rule);
        }));
}

void draw_password_meter(
    const hedgr::model::state::password_policy_state& state,
    std::string_view password) {
    if (!state.is_loaded()) {
        /*
         * Nothing is assessed, and the screen says which nothing it is. A
         * meter drawn from a policy nobody supplied would be Hedgr's own
         * guess, and a guess that disagrees with the deployment is worse than
         * no meter: the person would be told they are fine by the thing that
         * is about to refuse them.
         */
        if (state.state() == domain::policy_state::loading)
            ImGui::TextDisabled("Checking the rules this deployment requires...");
        else
            ImGui::TextDisabled(
                "Strength cannot be assessed: this deployment did not state "
                "its password rules. The deployment still checks the "
                "password.");
        if (!state.detail().empty())
            ImGui::SetItemTooltip("%s", state.detail().c_str());
        return;
    }

    const auto& policy = state.policy();
    const auto assessment = domain::assess_password(password, policy);

    /*
     * The label's width is measured off the longest level's own word, so the
     * row does not resize as a person types from "empty" to "strong". A row
     * that moved under the cursor would be a row being read while it changes.
     */
    float label_width = 0.0f;
    for (int level = 0; level <= 4; ++level) {
        const auto size = ImGui::CalcTextSize(
            std::string{strength_text(level)}.c_str());
        label_width = std::max(label_width, size.x);
    }

    draw_strength_bar(assessment.strength, label_width);
    /*
     * The gap the bar reserved for the label, repeated here so the two agree:
     * the bar's segments and this word are placed from the same measurement,
     * and the label cannot drift away from the scale it names.
     */
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                           hedgr::ui::theme::strength_colour(
                               assessment.strength)),
        "%s", strength_text(assessment.strength).data());

    ImGui::TextDisabled("Rules this deployment requires:");
    for (const auto rule : domain::password_rules(policy))
        draw_rule(rule, policy, assessment);
}

}
