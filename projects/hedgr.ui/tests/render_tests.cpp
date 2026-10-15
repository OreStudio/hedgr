#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/tier.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

/*
 * There is deliberately no rendering test here. Every renderer in this
 * component calls into ImGui, and ImGui::NewFrame without a live context, a
 * font atlas and a rendering backend aborts rather than returning an error.
 * Standing one up would need a GL context, which this test binary has no way
 * to create and no reason to: the renderers are thin bindings over the model's
 * tables, and the parts of them that carry any logic -- which renderer answers
 * for a kind, and what colour and icon a status maps to -- are exactly the
 * parts tested below, none of which touch ImGui.
 */

using hedgr::model::domain::session_status;
using hedgr::model::domain::tier;
using hedgr::model::domain::window_kind;
using hedgr::ui::render::renderer_for;
using hedgr::ui::render::window_ui_state;

TEST_CASE("every window kind has a renderer", "[renderer]") {
    for (std::size_t value = 0; value < hedgr::model::domain::window_kind_count;
         ++value) {
        const auto kind = static_cast<window_kind>(value);
        INFO("window kind " << value);
        REQUIRE(renderer_for(kind) != nullptr);
    }
}

TEST_CASE("the three tiers have three distinct colours", "[theme]") {
    const auto development = hedgr::ui::theme::tier_colour(tier::development);
    const auto staging = hedgr::ui::theme::tier_colour(tier::staging);
    const auto production = hedgr::ui::theme::tier_colour(tier::production);

    REQUIRE(development != staging);
    REQUIRE(staging != production);
    REQUIRE(development != production);
}

TEST_CASE("statuses that a person must tell apart have distinct colours",
    "[theme]") {
    const auto authenticated = hedgr::ui::theme::status_colour(
        session_status::authenticated);
    const auto failed =
        hedgr::ui::theme::status_colour(session_status::failed);
    const auto disconnected =
        hedgr::ui::theme::status_colour(session_status::disconnected);

    REQUIRE(authenticated != failed);
    REQUIRE(failed != disconnected);
    REQUIRE(authenticated != disconnected);
}

TEST_CASE("every tier has an icon", "[theme]") {
    for (std::size_t value = 0; value < hedgr::model::domain::tier_count;
         ++value) {
        const auto icon = hedgr::ui::theme::tier_icon(static_cast<tier>(value));
        INFO("tier " << value);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string{icon}.size() > 0);
    }
}

TEST_CASE("every session status has an icon", "[theme]") {
    for (std::size_t value = 0;
         value < hedgr::model::domain::session_status_count; ++value) {
        const auto icon =
            hedgr::ui::theme::status_icon(static_cast<session_status>(value));
        INFO("status " << value);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string{icon}.size() > 0);
    }
}

TEST_CASE("every window kind has an icon", "[theme]") {
    for (std::size_t value = 0; value < hedgr::model::domain::window_kind_count;
         ++value) {
        const auto icon =
            hedgr::ui::theme::window_kind_icon(static_cast<window_kind>(value));
        INFO("window kind " << value);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string{icon}.size() > 0);
    }
}

TEST_CASE("a window's scratch state starts empty and masked", "[state]") {
    const window_ui_state state;

    REQUIRE(state.filter[0] == '\0');
    REQUIRE(state.draft_password[0] == '\0');
    REQUIRE_FALSE(state.password_visible);
    REQUIRE(state.selected_row == -1);
    REQUIRE(state.pending_remove_id.empty());
    REQUIRE(state.draft_principal.empty());
    REQUIRE_FALSE(state.show_detail);
}
