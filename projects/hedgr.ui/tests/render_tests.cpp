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
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/browser.hpp"
#include "hedgr.ui/widget/panels.hpp"
#include <catch2/catch_all.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

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

/*
 * The lock gate, tested as a predicate rather than as pixels.
 *
 * Every renderer in this component calls into ImGui, and ImGui::NewFrame
 * without a live context, a font atlas and a rendering backend aborts rather
 * than returning an error. So the rule that decides whether the Instances
 * window is drawn at all lives in a function that touches no ImGui, and it is
 * called directly here. A predicate a test can call is worth more than a
 * comment saying the code checks something.
 */
namespace {

using hedgr::model::command::command_queue;
using hedgr::model::domain::store_lock;
using hedgr::model::state::account_table;
using hedgr::model::state::folder_table;
using hedgr::model::state::instance_table;
using hedgr::model::state::label_table;
using hedgr::model::state::labelling_table;
using hedgr::model::state::password_policy_state;
using hedgr::model::state::probe_table;
using hedgr::model::state::session_table;
using hedgr::model::state::window_table;
using hedgr::ui::render::render_input;
using hedgr::ui::render::shows_instances;

/** @brief Every table a render_input borrows, so it can safely borrow them. */
struct empty_state final {
    instance_table instances;
    account_table accounts;
    folder_table folders;
    label_table labels;
    labelling_table labellings;
    session_table sessions;
    probe_table probes;
    password_policy_state password_policy;
    window_table windows;
    command_queue commands;
};

/** @brief A render_input over empty tables, carrying @p lock. */
render_input input_with(empty_state& state, store_lock lock) {
    return render_input{
        .instances = state.instances,
        .accounts = state.accounts,
        .folders = state.folders,
        .labels = state.labels,
        .labellings = state.labellings,
        .sessions = state.sessions,
        .probes = state.probes,
        .password_policy = state.password_policy,
        .windows = state.windows,
        .commands = state.commands,
        .store_lock = lock,
    };
}

}

TEST_CASE("the instances window is drawn only while the store is unlocked",
    "[lock]") {
    empty_state state;

    REQUIRE(shows_instances(input_with(state, store_lock::unlocked)));

    /*
     * Absent is a first run and locked is a store at rest; neither is a
     * failure, and neither draws the catalogue. Failed draws it no more than
     * the other two do, because it is the same closed lock after a refusal.
     */
    REQUIRE_FALSE(shows_instances(input_with(state, store_lock::absent)));
    REQUIRE_FALSE(shows_instances(input_with(state, store_lock::locked)));
    REQUIRE_FALSE(shows_instances(input_with(state, store_lock::failed)));
}

TEST_CASE("the gate closes again when the store is locked once more", "[lock]") {
    empty_state state;

    /*
     * The same input object, moved through the states a run actually visits:
     * absent on a first launch, unlocked after the password was created, locked
     * again when the operator locked it. The gate has to follow, and it has to
     * open on the way back too, or a lock would be a one-way door.
     */
    auto input = input_with(state, store_lock::absent);
    REQUIRE_FALSE(shows_instances(input));

    input.store_lock = store_lock::unlocked;
    REQUIRE(shows_instances(input));

    input.store_lock = store_lock::locked;
    REQUIRE_FALSE(shows_instances(input));

    input.store_lock = store_lock::unlocked;
    REQUIRE(shows_instances(input));
}

TEST_CASE("every lock state has an icon", "[lock]") {
    for (std::size_t value = 0;
         value < hedgr::model::domain::store_lock_count; ++value) {
        const auto lock = static_cast<store_lock>(value);
        INFO("lock state " << value);
        const auto icon = hedgr::ui::theme::store_lock_icon(lock);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string{icon}.size() > 0);
    }
}

TEST_CASE("the lock states a person must tell apart have distinct icons",
    "[lock]") {
    /*
     * Unlocked, locked and absent have to be three different glyphs. Two of
     * them as a padlock would be a state an operator cannot read at a glance,
     * and absent is the one that wants an action rather than an unlock.
     */
    const auto unlocked = std::string{hedgr::ui::theme::store_lock_icon(
        store_lock::unlocked)};
    const auto locked =
        std::string{hedgr::ui::theme::store_lock_icon(store_lock::locked)};
    const auto absent =
        std::string{hedgr::ui::theme::store_lock_icon(store_lock::absent)};

    REQUIRE(unlocked != locked);
    REQUIRE(absent != locked);
    REQUIRE(absent != unlocked);
}

TEST_CASE("a failed lock is drawn as a failure and a working one is not",
    "[lock]") {
    /*
     * Only @c failed says something went wrong, so only @c failed may use the
     * colour this interface uses for failure. If locked or absent shared it,
     * the ordinary condition of a store at rest would read as a fault.
     */
    const auto failed =
        hedgr::ui::theme::store_lock_colour(store_lock::failed);
    const auto unlocked =
        hedgr::ui::theme::store_lock_colour(store_lock::unlocked);
    const auto locked = hedgr::ui::theme::store_lock_colour(store_lock::locked);

    REQUIRE(failed != unlocked);
    REQUIRE(failed != locked);
}

/*
 * ---------------------------------------------------------------- browser
 *
 * The browser's decisions -- what the filter keeps, what a selection lists,
 * which affordances a row has, and which drops are refused -- are free
 * functions over plain values, and they are tested here rather than through a
 * frame. The drawing itself is not tested: ImGui::NewFrame without a live
 * context, a font atlas and a backend aborts rather than returning an error, so
 * there is no way to run a renderer in this binary and no reason to want one.
 * What is worth a test is everything a renderer decides before it draws.
 */

/*
 * The widget layer's decisions, tested as values rather than as pixels.
 *
 * Nothing here draws a pill, a row or a frame. Painting goes through
 * ImGui::GetWindowDrawList and ImGui::CalcTextSize, and both need a live
 * context, a font atlas and a backend; ImGui::NewFrame aborts rather than
 * returning an error without them, so there is no way to run a renderer in
 * this binary and no reason to want one. What is worth a test is everything the
 * drawing decides before it paints -- which text colour a fill earns, how many
 * pills fit in a width, what a model label turns into -- and each of those is a
 * free function over plain values, callable from here.
 */

using hedgr::ui::widget::affordances_for;
using hedgr::ui::widget::browser_state;
using hedgr::ui::widget::can_drop_onto;
using hedgr::ui::widget::filter_matches;
using hedgr::ui::widget::filter_matches_any;
using hedgr::ui::widget::node_kind;

TEST_CASE("the filter matches anywhere in a name, ignoring case", "[browser]") {
    REQUIRE(filter_matches("nats://localhost:20405", "LOCALHOST"));
    REQUIRE(filter_matches("production", "duct"));
    REQUIRE(filter_matches("production", ""));
    REQUIRE_FALSE(filter_matches("production", "staging"));
    REQUIRE(filter_matches_any({"name", "principal"}, "PRIN"));
    REQUIRE_FALSE(filter_matches_any({"name", "principal"}, "tenant"));
}

TEST_CASE("root offers no rename, move or delete, and offers creation",
    "[browser]") {
    /*
     * The acceptance rule, stated as a test rather than as a comment: root is
     * what the tree is anchored by, so the three affordances that would move or
     * relabel it do not exist for it. It is where things are created, which is
     * the one thing root is for.
     */
    const auto root = affordances_for(node_kind::folder, true);
    REQUIRE_FALSE(root.rename);
    REQUIRE_FALSE(root.move);
    REQUIRE_FALSE(root.remove);
    REQUIRE(root.create_inside);

    /* An ordinary folder may do all four. */
    const auto folder = affordances_for(node_kind::folder, false);
    REQUIRE(folder.rename);
    REQUIRE(folder.move);
    REQUIRE(folder.remove);
    REQUIRE(folder.create_inside);

    /* A leaf holds nothing, so it is the one row that cannot contain. */
    const auto leaf = affordances_for(node_kind::leaf, false);
    REQUIRE(leaf.rename);
    REQUIRE(leaf.move);
    REQUIRE(leaf.remove);
    REQUIRE_FALSE(leaf.create_inside);
}

TEST_CASE("a folder cannot be dropped into itself or its own subtree",
    "[browser]") {
    const std::vector<std::string> subtree{ "a", "b", "c" };

    REQUIRE_FALSE(can_drop_onto("a", "a", subtree));
    REQUIRE_FALSE(can_drop_onto("a", "b", subtree));
    REQUIRE_FALSE(can_drop_onto("a", "c", subtree));

    /* Somewhere outside the subtree, and the root, are both fine. */
    REQUIRE(can_drop_onto("a", "d", subtree));
    REQUIRE(can_drop_onto("a", "root", subtree));
    REQUIRE(can_drop_onto("a", "root", {}));

    /* Nothing is dropped onto nothing. */
    REQUIRE_FALSE(can_drop_onto("a", "", {}));
}

TEST_CASE("a pill's text colour is chosen from its fill's luminance",
    "[theme]") {
    using hedgr::ui::theme::text_colour_on;

    /*
     * The rule that makes a label pill readable: the fill is the label's own
     * colour, so the text on it cannot be a constant. This is a pure function
     * of a colour and needs no window, which is why it is the half of the
     * pill's drawing that is tested here.
     */
    const auto dark_text = text_colour_on(IM_COL32(255, 255, 255, 255));
    const auto light_text = text_colour_on(IM_COL32(0, 0, 0, 255));

    REQUIRE(dark_text != light_text);

    /* Luminance, not a plain mean: the red channel carries the least weight. */
    REQUIRE(text_colour_on(IM_COL32(255, 255, 255, 255)) == dark_text);
    REQUIRE(text_colour_on(IM_COL32(128, 128, 128, 255)) == dark_text);
    REQUIRE(text_colour_on(IM_COL32(0, 0, 0, 255)) == light_text);
    REQUIRE(text_colour_on(IM_COL32(0, 0, 255, 255)) == light_text);

    /*
     * And the choice is the right way round for every colour the theme actually
     * paints a label with. The split is not the one the names suggest: all of
     * the palette is pale enough for dark text except red, green included --
     * a saturated green is the entry nearest the line, which is why the rule
     * is stated over luminance rather than picked per entry: a change to the
     * palette cannot leave one label unreadable.
     */
    using hedgr::model::domain::label_colour;

    const auto on = [&](label_colour entry) {
        return text_colour_on(hedgr::ui::theme::label_colour(entry));
    };

    for (const auto entry : { label_colour::amber, label_colour::grey,
             label_colour::blue, label_colour::purple, label_colour::green }) {
        INFO("pale label colour " << static_cast<std::size_t>(entry));
        REQUIRE(on(entry) == dark_text);
    }

    INFO("dark label colour " << static_cast<std::size_t>(label_colour::red));
    REQUIRE(on(label_colour::red) == light_text);
}

TEST_CASE("a collapsible panel opens the first time it is drawn", "[widget]") {
    using hedgr::ui::widget::panel_default_open;
    using hedgr::ui::widget::panel_kind;

    /*
     * The rule the two collapsible headers apply through ImGui's
     * SetNextItemOpen. It is a pure function of a panel, so it is checked here
     * rather than in a frame: what a header does with it is ImGui's, and what
     * this decides is whether the operator has to click to see a summary.
     */
    REQUIRE(panel_default_open(panel_kind::dashboard));
    REQUIRE(panel_default_open(panel_kind::session_facts));

    /*
     * And the window state starts the same way, so a header drawn before
     * anything sets it is not briefly shut. This is a guard on the initialiser
     * rather than on the drawing: it is the value the two windows hand ImGui on
     * their first frame.
     */
    const window_ui_state state;
    REQUIRE(state.panel_open);
}

TEST_CASE("a row of pills keeps only what fits whole", "[widget]") {
    using hedgr::ui::widget::pill_fits;
    using hedgr::ui::widget::pills_that_fit;

    /*
     * The layout rule, as arithmetic over widths: the drawing needs a window
     * and a font atlas, but the decision of how many pills are drawn does not.
     * A pill is drawn whole or not at all, because the one outcome a row of
     * labels must never have is two pills overlapping.
     */
    const std::vector<float> widths{ 40.0f, 40.0f, 40.0f };
    constexpr float gap = 6.0f;

    /* Three at 40 with two 6-wide gaps come to 132. */
    REQUIRE(pills_that_fit(widths, gap, 132.0f) == 3);
    REQUIRE(pills_that_fit(widths, gap, 131.0f) == 2);
    REQUIRE(pills_that_fit(widths, gap, 86.0f) == 2);
    REQUIRE(pills_that_fit(widths, gap, 85.0f) == 1);
    REQUIRE(pills_that_fit(widths, gap, 40.0f) == 1);
    REQUIRE(pills_that_fit(widths, gap, 39.0f) == 0);
    REQUIRE(pills_that_fit(widths, gap, 0.0f) == 0);

    /* The gap belongs after a pill, so the first one needs none. */
    REQUIRE(pill_fits(40.0f, 0.0f, 40.0f));
    REQUIRE_FALSE(pill_fits(40.0f, gap, 40.0f));
    REQUIRE(pill_fits(40.0f, gap, 46.0f));
}

TEST_CASE("a label is turned into a pill in the theme's own colour",
    "[widget]") {
    using hedgr::model::domain::label;
    using hedgr::model::domain::label_colour;
    using hedgr::model::domain::label_severity;
    using hedgr::ui::widget::pills_for;

    label amber;
    amber.id = "label:production";
    amber.name = "production";
    amber.colour = label_colour::amber;
    amber.severity = label_severity::danger;

    const std::vector<const label*> rows{ &amber };
    const auto pills = pills_for(rows);

    REQUIRE(pills.size() == 1);
    REQUIRE(pills[0].text == "production");
    REQUIRE(pills[0].id == "label:production");
    REQUIRE(pills[0].severity == label_severity::danger);
    REQUIRE(pills[0].background
        == static_cast<std::uint32_t>(hedgr::ui::theme::label_colour(amber.colour)));

    /* A missing row is skipped rather than drawn as a pill with no name. */
    const std::vector<const label*> with_hole{ nullptr, &amber };
    REQUIRE(pills_for(with_hole).size() == 1);
    REQUIRE(pills_for({}).empty());
}

TEST_CASE("the dashboard counts what the probes found", "[dashboard]") {
    empty_state state;

    const auto add = [&](std::string id) {
        hedgr::model::domain::instance row;
        row.id = id;
        row.name = id;
        return state.instances.upsert(row);
    };

    /* An instance nobody has probed is counted as total and nothing else. */
    const auto pending = add("pending");
    const auto unprobed =
        hedgr::ui::widget::count_instances(state.instances, state.probes);
    REQUIRE(unprobed.total == 1);
    REQUIRE(unprobed.ready == 0);
    REQUIRE(unprobed.bootstrapping == 0);
    REQUIRE(unprobed.unreachable == 0);

    hedgr::model::domain::probe_result ready;
    ready.reach = hedgr::model::domain::reachability::reachable;
    ready.ready = hedgr::model::domain::readiness::ready;
    state.probes.set(pending, ready);

    hedgr::model::domain::probe_result booting;
    booting.reach = hedgr::model::domain::reachability::reachable;
    booting.ready = hedgr::model::domain::readiness::bootstrapping;
    state.probes.set(add("booting"), booting);

    /*
     * A deployment whose IAM did not answer is unreachable and neither ready
     * nor bootstrapping: unknown is not bootstrapping, because a deployment
     * that said nothing has not said it is setting itself up.
     */
    hedgr::model::domain::probe_result down;
    down.reach = hedgr::model::domain::reachability::unreachable;
    down.ready = hedgr::model::domain::readiness::unknown;
    state.probes.set(add("down"), down);

    const auto counts =
        hedgr::ui::widget::count_instances(state.instances, state.probes);
    REQUIRE(counts.total == 3);
    REQUIRE(counts.ready == 1);
    REQUIRE(counts.bootstrapping == 1);
    REQUIRE(counts.unreachable == 1);

    /*
     * A removed instance takes its probe row with it, so a result cannot be
     * counted against the row that takes the slot afterwards.
     */
    const auto doomed = state.instances.find("down");
    REQUIRE(doomed.has_value());
    state.instances.remove(*doomed);
    state.probes.forget(*doomed);

    const auto after = hedgr::ui::widget::count_instances(state.instances, state.probes);
    REQUIRE(after.total == 2);
    REQUIRE(after.unreachable == 0);
}

TEST_CASE("an account that was never signed in differs from a failed one",
    "[dashboard]") {
    using hedgr::model::domain::session_status;
    empty_state state;

    const auto never = hedgr::ui::widget::last_sign_in(state.sessions, "i", "a");
    REQUIRE_FALSE(never.ever_attempted);

    const auto handle = state.sessions.insert("i", "a");
    state.sessions.set_status(handle, session_status::failed);
    state.sessions.set_last_error(handle, "IAM refused the password");

    const auto failed = hedgr::ui::widget::last_sign_in(state.sessions, "i", "a");
    REQUIRE(failed.ever_attempted);
    REQUIRE(failed.status == session_status::failed);
    REQUIRE(failed.last_error == "IAM refused the password");
}

TEST_CASE("an address is split into a host and a port without guessing",
    "[dashboard]") {
    using hedgr::ui::widget::host_port;
    using hedgr::ui::widget::join_host_port;
    using hedgr::ui::widget::split_host_port;

    const auto plain = split_host_port("nats://manta:20405");
    REQUIRE(plain.scheme == "nats://");
    REQUIRE(plain.host == "manta");
    REQUIRE(plain.port == "20405");
    REQUIRE(join_host_port(plain) == "nats://manta:20405");

    /* No port is no port, not a default filled in by the splitter. */
    const auto bare = split_host_port("nats://manta");
    REQUIRE(bare.host == "manta");
    REQUIRE(bare.port.empty());
    REQUIRE(join_host_port(bare) == "nats://manta");

    /* A scheme nobody here knows is carried through rather than rewritten. */
    const auto secure = split_host_port("tls://manta:4222");
    REQUIRE(secure.scheme == "tls://");
    REQUIRE(join_host_port(secure) == "tls://manta:4222");

    /* A host with no scheme at all still yields a host and a port. */
    const auto scheme_less = split_host_port("manta:4222");
    REQUIRE(scheme_less.scheme.empty());
    REQUIRE(scheme_less.host == "manta");
    REQUIRE(scheme_less.port == "4222");

    /* An IPv6 literal keeps its colons and still names its port. */
    const auto v6 = split_host_port("nats://[::1]:4222");
    REQUIRE(v6.host == "[::1]");
    REQUIRE(v6.port == "4222");

    const host_port empty;
    REQUIRE(split_host_port("").host.empty());
    REQUIRE(join_host_port(empty).empty());
}

TEST_CASE("label names are split on commas and trimmed", "[dashboard]") {
    using hedgr::ui::widget::join_labels;
    using hedgr::ui::widget::split_labels;

    const auto names = split_labels(" production , staging,ops ");
    REQUIRE(names == std::vector<std::string>{ "production", "staging", "ops" });

    /* An empty word between two commas is nobody's label. */
    REQUIRE(split_labels("a,,b").size() == 2);
    REQUIRE(split_labels("").empty());
    REQUIRE(split_labels("  ").empty());

    REQUIRE(join_labels(names) == "production, staging, ops");
    REQUIRE(join_labels({}).empty());
}

TEST_CASE("a catalogue window is culled while the store is locked", "[lock]") {
    empty_state state;

    /*
     * The window kinds that read the store are the ones the lock closes. The
     * ones whose contents arrive over a session are not, because a session is
     * not what the master password protects.
     */
    const auto shows = [&](store_lock lock, window_kind kind) {
        return hedgr::ui::render::shows_window(
            kind, input_with(state, lock), std::string_view{"w-1"});
    };

    REQUIRE(shows(store_lock::unlocked, window_kind::accounts));
    REQUIRE(shows(store_lock::unlocked, window_kind::workspace));
    REQUIRE(shows(store_lock::unlocked, window_kind::instance_manager));

    REQUIRE_FALSE(shows(store_lock::locked, window_kind::accounts));
    REQUIRE_FALSE(shows(store_lock::locked, window_kind::workspace));
    REQUIRE_FALSE(shows(store_lock::locked, window_kind::instance_manager));

    REQUIRE(shows(store_lock::locked, window_kind::notice));
    REQUIRE(shows(store_lock::locked, window_kind::chart));
    REQUIRE(shows(store_lock::locked, window_kind::grid));
}

TEST_CASE("the new window kinds have renderers and titles", "[renderer]") {
    using hedgr::model::domain::accounts_state;
    using hedgr::model::domain::workspace_state;
    using hedgr::model::domain::window_state;

    REQUIRE(renderer_for(window_kind::accounts) != nullptr);
    REQUIRE(renderer_for(window_kind::workspace) != nullptr);

    empty_state state;
    const auto input = input_with(state, store_lock::unlocked);

    /*
     * A window whose subject has gone still has a title, and it is the kind's
     * own name rather than an empty string: a restored window over a deleted
     * instance must still say what it is.
     */
    const window_state orphan = accounts_state{ "inst-gone" };
    REQUIRE(hedgr::ui::render::window_title(window_kind::accounts, orphan, input)
        == "Accounts: inst-gone");

    const window_state session = workspace_state{ "acct-gone" };
    REQUIRE(hedgr::ui::render::window_title(window_kind::workspace, session, input)
        == "acct-gone");
}

TEST_CASE("every window kind has an icon and a distinct spelling", "[renderer]") {
    /*
     * The kind table is the one place a kind is registered, and the compiler
     * checks the renderer half of it. The icon and the persisted spelling are
     * the two halves it cannot check, so they are checked here: a kind with no
     * icon or two kinds with one spelling are both a kind that cannot be
     * restored or drawn.
     */
    std::unordered_set<std::string> spellings;
    for (std::size_t value = 0; value < hedgr::model::domain::window_kind_count;
         ++value) {
        const auto kind = static_cast<window_kind>(value);

        const auto icon = hedgr::ui::theme::window_kind_icon(kind);
        REQUIRE(icon != nullptr);
        REQUIRE(std::string{icon}.size() > 0);

        const auto spelling =
            std::string{hedgr::model::domain::to_string(kind)};
        REQUIRE(spelling != "unknown");
        REQUIRE(spellings.insert(spelling).second);

        /* And it survives a round trip through the stored form. */
        const auto parsed = hedgr::model::domain::window_kind_from_string(spelling);
        REQUIRE(parsed.has_value());
        REQUIRE(*parsed == kind);
    }
}
