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
#include "hedgr.ui/render/panels.hpp"
#include "hedgr.ui/render/windows.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/browser.hpp"
#include "hedgr.ui/widget/cards.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/panels.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "ores.logging/make_logger.hpp"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "window_forms.hpp"

namespace {

namespace widget = hedgr::ui::widget;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;
namespace forms = hedgr::ui::render::forms;
using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::account;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::labelling_owner;
using hedgr::model::domain::probe_result;
using hedgr::model::domain::session_status;
using hedgr::model::domain::store_lock;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/** @brief This window's logger. It names a decision, never a password. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.ui.render.accounts_window");
    return instance;
}
using hedgr::ui::widget::browser_state;
using hedgr::ui::widget::tree_leaf;

/*
 * The row a capture asked to see selected, held until the frame that can show
 * it. File-scope rather than a parameter because the environment is read once,
 * before any window is drawn, and empty in an ordinary run.
 */
std::string pending_capture_selected_account;

/** @brief Every live account of one instance, as the browser wants it. */
[[nodiscard]] std::vector<tree_leaf> account_tree_leaves(const render_input& input,
    std::string_view instance_id) {
    std::vector<tree_leaf> leaves;
    const auto& layout = input.accounts.layout();
    const auto rows = input.accounts.rows();

    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        const auto& row = rows[index];
        if (row.instance_id != instance_id)
            continue;
        leaves.push_back(tree_leaf{
            .id = row.id,
            .parent_id = row.folder_id,
            .name = row.name.empty() ? row.principal : row.name,
            .icon = icon::account,
            .search_text = std::format("{}\n{}", row.principal, row.name),
        });
    }
    return leaves;
}

/**
 * @brief The one word a session's status is read by.
 *
 * The list and the pane both write it, so the two cannot come to use two words
 * for one state.
 */
[[nodiscard]] const char* session_word(session_status value) noexcept {
    switch (value) {
    case session_status::authenticated:
        return "authenticated";
    case session_status::connecting:
        return "connecting";
    case session_status::failed:
        return "failed";
    case session_status::disconnected:
        return "disconnected";
    }
    return "unknown";
}

/** @brief Loads the account form's buffers from a row, or leaves them blank. */
void load_draft(window_ui_state& state, const account* row) {
    state.draft_name.fill('\0');
    state.draft_subject.fill('\0');
    state.browser.form_labels.fill('\0');
    if (row == nullptr)
        return;

    forms::set_buffer(state.draft_name,
        row->name.empty() ? row->principal : row->name);
    forms::set_buffer(state.draft_subject, row->principal);
}

/** @brief Pushes a save for one account and the labels it should carry. */
void save_account(const render_input& input, window_ui_state& state,
    std::string_view instance_id) {
    const std::string_view editing{state.browser.form_leaf};
    const auto existing = input.accounts.find(editing);

    account row;
    if (existing)
        row = *input.accounts.get(*existing);

    std::string name{forms::buffer_text(state.draft_name)};
    if (state.browser.form_duplicating)
        name.append(" copy");

    /*
     * A new account's key is derived from its instance and its name, which
     * together are what a person means when they name an account. A serial
     * would be unique too and would make the same account a different row every
     * time it was created in a test.
     */
    row.id = existing && !state.browser.form_duplicating
        ? row.id
        : std::format("acct:{}:{}", instance_id, name);
    row.instance_id = std::string{instance_id};
    row.name = name;
    row.principal = forms::buffer_text(state.draft_subject);
    row.folder_id = state.browser.form_folder;

    command request;
    request.kind = command_kind::save_account;
    request.account_id = row.id;
    request.account_row = std::move(row);

    const std::string id = request.account_id;
    input.commands.push(std::move(request));
    forms::apply_labels(input, labelling_owner::account, id,
        forms::buffer_text(state.browser.form_labels));
}

/** @brief The account form: one dialog for adding and for editing. */
void draw_form(const render_input& input, window_ui_state& state) {
    const bool editing = !state.browser.form_leaf.empty()
        && !state.browser.form_duplicating;

    const ImVec2 size(460.0f, 240.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    const char* title = editing ? "Edit account###hedgr-account-form"
                                : "New account###hedgr-account-form";
    ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, flags))
        return;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##account-name", "name", state.draft_name.data(),
        state.draft_name.size());

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##account-principal", "principal",
        state.draft_subject.data(), state.draft_subject.size());

    /*
     * The labels this account already carries, as the same pills the list
     * draws, above the field that edits them: the names are what a person
     * types, and the pills are what those names look like once attached.
     */
    const auto attached =
        forms::labels_of(input, labelling_owner::account, state.browser.form_leaf);
    if (!attached.empty()) {
        ImGui::TextDisabled("Attached:");
        ImGui::SameLine();
        static_cast<void>(widget::label_pill_row(widget::pills_for(attached), 0.0f));
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##account-labels", "labels, comma separated",
        state.browser.form_labels.data(), state.browser.form_labels.size());
    ImGui::SetItemTooltip("%s",
        "Labels attached to this account. A name that does not exist yet is "
        "created.");

    /*
     * The name is display text and the principal is what IAM is given. They are
     * two fields because they are two facts: two accounts on one instance may
     * share a name and may not share a principal.
     */
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("The name is what you call this account; the principal "
                       "is what IAM sees.");
    ImGui::PopStyleColor();

    const bool complete = !forms::buffer_text(state.draft_name).empty()
        && !forms::buffer_text(state.draft_subject).empty();

    if (!complete)
        ImGui::BeginDisabled();
    const bool saved = ImGui::Button(std::format("{} Save", icon::save).c_str());
    if (!complete)
        ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", "Save the account");

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Cancel", icon::cancel).c_str())) {
        state.browser.form = widget::form_kind::none;
        state.browser.form_leaf.clear();
        state.browser.form_folder.clear();
        state.browser.form_duplicating = false;
        state.browser.duplicate_source.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Abandon this edit");

    if (saved && complete) {
        const auto handle = editing
            ? input.accounts.find(state.browser.form_leaf)
            : std::optional<hedgr::model::state::account_handle>{};
        const auto instance_id = handle
            ? input.accounts.get(*handle)->instance_id
            : state.browser.form_owner;
        save_account(input, state, instance_id);

        state.browser.form = widget::form_kind::none;
        state.browser.form_leaf.clear();
        state.browser.form_folder.clear();
        state.browser.form_duplicating = false;
        state.browser.duplicate_source.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

/** @brief Opens the form for a new account inside @p parent. */
void open_create_form(window_ui_state& window, std::string_view parent) {
    window.browser.form = widget::form_kind::leaf;
    window.browser.form_leaf.clear();
    window.browser.form_folder = std::string{parent};
    window.browser.form_duplicating = false;
    window.browser.duplicate_source.clear();
    load_draft(window, nullptr);
}

/** @brief Opens the form on an account, or on a blank copy of one. */
void open_edit_form(const render_input& input, window_ui_state& window,
    std::string leaf_id, bool duplicating) {
    /*
     * The id is taken by value rather than as a view, because one caller names
     * the row with the very buffer this assigns to: a view into
     * @c window.browser.form_leaf would dangle the moment the assignment below
     * released that string, and the lookup would then read freed memory and
     * report that the row does not exist.
     */
    window.browser.form = widget::form_kind::leaf;
    window.browser.form_leaf = duplicating ? std::string{} : leaf_id;
    window.browser.form_duplicating = duplicating;

    const auto handle = input.accounts.find(leaf_id);
    load_draft(window, handle ? input.accounts.get(*handle) : nullptr);

    if (handle) {
        const auto* row = input.accounts.get(*handle);
        window.browser.form_folder = row->folder_id;
        window.browser.form_owner = row->instance_id;
        if (duplicating)
            forms::set_buffer(window.draft_name,
                std::format("{} copy", row->name));
    }
    forms::set_buffer(window.browser.form_labels,
        forms::joined_labels(input, labelling_owner::account, leaf_id));
}

/**
 * @brief The sign-in prompt: a password, and the eye toggle that reveals it.
 *
 * Raised when the account has no credential that can be used without one. The
 * password leaves the window state only in the command it is submitted with,
 * and the buffer is wiped as soon as it is.
 */
void draw_sign_in_form(const render_input& input, window_ui_state& window) {
    browser_state& browser = window.browser;
    if (!browser.sign_in_open)
        return;

    const ImVec2 size(440.0f, 180.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    const char* title = "Sign in###hedgr-sign-in";
    ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, flags))
        return;

    const auto handle = input.accounts.find(browser.selected_leaf);
    const auto* row = handle ? input.accounts.get(*handle) : nullptr;

    if (input.store_lock != store_lock::unlocked) {
        ImGui::TextWrapped(
            "The store is %s, so a remembered password cannot be read. Type one "
            "for this sign-in.",
            std::string{hedgr::model::domain::to_string(input.store_lock)}
                .c_str());
    } else if (row != nullptr && row->has_credential) {
        ImGui::TextWrapped(
            "A password is remembered for %s. Type a new one to replace it.",
            row->principal.c_str());
    } else {
        ImGui::TextWrapped(
            "No password is remembered for %s. Type one for this sign-in.",
            row != nullptr ? row->principal.c_str() : "this account");
    }

    const float eye = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(widget::width_before(eye));
    ImGui::InputTextWithHint("##sign-in-password", "password",
        browser.credential.data(), browser.credential.size(),
        browser.password_visible ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (widget::icon_button("reveal",
            browser.password_visible ? icon::eye_off : icon::eye,
            browser.password_visible ? "Hide password" : "Show password"))
        browser.password_visible = !browser.password_visible;

    if (input.store_lock != store_lock::unlocked)
        ImGui::BeginDisabled();
    ImGui::Checkbox("Remember this password", &browser.remember);
    ImGui::SetItemTooltip("%s",
        input.store_lock == store_lock::unlocked
            ? "Seal the password under the master password, so the next sign-in "
              "does not ask for it"
            : "The store is closed, so there is no master password to seal it "
              "under");
    if (input.store_lock != store_lock::unlocked)
        ImGui::EndDisabled();

    const bool complete = browser.credential[0] != '\0' && row != nullptr;

    if (!complete)
        ImGui::BeginDisabled();
    const bool signed_in =
        ImGui::Button(std::format("{} Sign in", icon::sign_in).c_str());
    if (!complete)
        ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", "Sign in and open a workspace");

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Cancel", icon::cancel).c_str())) {
        /*
         * The typed password is wiped on the way out: a cancelled prompt that
         * kept it would leave a secret in this window's state for the rest of
         * the run.
         */
        std::fill(browser.credential.begin(), browser.credential.end(), '\0');
        browser.password_visible = false;
        browser.sign_in_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Do not sign in");

    if (signed_in && complete) {
        command request;
        request.kind = command_kind::connect_session;
        request.instance_id = row->instance_id;
        request.account_id = row->id;
        request.principal = row->principal;
        request.password = browser.credential.data();
        request.remember = browser.remember;
        input.commands.push(std::move(request));

        std::fill(browser.credential.begin(), browser.credential.end(), '\0');
        browser.password_visible = false;
        browser.sign_in_open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

/** @brief The confirmation for removing one account. */
void draw_remove_form(const render_input& input, window_ui_state& window) {
    browser_state& browser = window.browser;
    if (!browser.confirm_remove_leaf)
        return;

    const ImVec2 size(440.0f, 160.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    const char* title = "Remove account###hedgr-account-remove";
    ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, flags))
        return;

    const auto handle = input.accounts.find(browser.selected_leaf);
    const auto* row = handle ? input.accounts.get(*handle) : nullptr;

    ImGui::TextWrapped("Remove %s? Its session, if any, is closed with it.",
        row != nullptr ? (row->name.empty() ? row->principal.c_str()
                                            : row->name.c_str())
                       : "this account");

    if (ImGui::Button(std::format("{} Remove", icon::remove).c_str())) {
        command request;
        request.kind = command_kind::remove_account;
        request.account_id = browser.selected_leaf;
        request.instance_id = row != nullptr ? row->instance_id : browser.instance;
        input.commands.push(std::move(request));

        browser.confirm_remove_leaf = false;
        browser.selected_leaf.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Remove it");

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Cancel", icon::cancel).c_str())) {
        browser.confirm_remove_leaf = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Keep it");

    ImGui::EndPopup();
}

/** @brief The account list's five columns. */
[[nodiscard]] std::vector<hedgr::ui::widget::list_column> account_columns() {
    using hedgr::ui::widget::column_width;
    return {
        { "Name", column_width::stretch },
        { "Principal", column_width::stretch },
        { "Tenant", column_width::fit },
        { "Labels", column_width::stretch },
        { "Last sign-in", column_width::fit },
    };
}

/** @brief The index of the one column whose cells are label pills. */
constexpr std::size_t account_label_column = 3;

/** @brief Fills one account row's cells. */
void account_row_cells(const tree_leaf& leaf, const render_input& input,
    widget::row_cells& cells) {
    const auto handle = input.accounts.find(leaf.id);
    const auto* row = handle ? input.accounts.get(*handle) : nullptr;
    const std::string instance_id = row != nullptr ? row->instance_id
                                                   : std::string{};

    cells.texts.push_back(leaf.name);
    cells.colours.push_back(0);
    cells.tooltips.emplace_back(row != nullptr ? row->name : std::string{});

    cells.texts.emplace_back(row != nullptr ? row->principal : std::string{});
    cells.colours.push_back(0);
    cells.tooltips.push_back(cells.texts.back());

    /*
     * The tenant column holds what the last sign-in reached, not a field of the
     * account. An account is configuration; which tenant IAM puts it in is IAM's
     * answer at sign-in and nothing here can know it before then. The tooltip
     * says which of the two an empty cell means, because "not known yet" and
     * "none" look identical in a table.
     */
    const auto sign_in = widget::last_sign_in(input.sessions, instance_id, leaf.id);
    cells.texts.push_back(
        sign_in.ever_attempted && !sign_in.tenant.empty() ? sign_in.tenant : "-");
    cells.colours.push_back(0);
    cells.tooltips.emplace_back(sign_in.tenant.empty()
            ? std::string{"IAM has not named a tenant for this account yet"}
            : std::format("Tenant {} as of the last sign-in", sign_in.tenant));

    const auto summary = sign_in.ever_attempted ? session_word(sign_in.status)
                                                : "never";

    cells.texts.push_back(summary);
    cells.colours.push_back(sign_in.ever_attempted
            ? theme::status_colour(sign_in.status)
            : theme::reachability_colour(
                  hedgr::model::domain::reachability::unknown));
    cells.tooltips.emplace_back(sign_in.last_error.empty()
            ? std::format("Last sign-in: {}", summary)
            : std::format("Last sign-in failed: {}", sign_in.last_error));
}

/**
 * @brief Fills one account row's label column with pills.
 *
 * Separate from @c account_row_cells because the labels column is drawn rather
 * than written: a pill carries the label's colour as its fill and its severity
 * in its tooltip, and the list cell has room for neither as text.
 */
void account_row_labels(const tree_leaf& leaf, const render_input& input,
    std::vector<std::vector<widget::label_pill>>& cells) {
    cells.resize(account_columns().size());
    cells[account_label_column] = widget::pills_for(
        forms::labels_of(input, labelling_owner::account, leaf.id));
}

/**
 * @brief The instance an account signs in to, and what the last probe found.
 *
 * The flag is carried rather than inferred, for the reason the Instances
 * window carries it: a probe whose answer never arrived and a probe nobody ran
 * look identical in the two enums and are not the same thing to a reader. One
 * is an outage, the other is a deployment nobody has waited on yet.
 */
struct account_instance_state final {
    std::string name;
    probe_result result;
    bool observed = false;
};

/** @brief The state of the instance an account belongs to. */
[[nodiscard]] account_instance_state instance_state_of(const render_input& input,
    std::string_view instance_id) {
    account_instance_state state;
    const auto handle = input.instances.find(instance_id);
    if (!handle)
        return state;

    state.name = input.instances.get(*handle)->name;
    const auto* probe = input.probes.get(*handle);
    if (probe == nullptr)
        return state;

    state.result = *probe;
    state.observed = true;
    return state;
}

/**
 * @brief Draws one account's details, in the browser's right hand pane.
 *
 * The account is the leaf and the pane reports on the selected one, which is
 * what the operator asked for: cards for the current account rather than a
 * table of every account. The pane is the browser's own right hand slot, the
 * same slot the Instances window shows an instance in, so the two windows are
 * one program rather than two. The kind of card, the grouping and the full
 * width rule for a long value all come from the shared card widget.
 *
 * The instance's readiness and reachability lead the report after the account's
 * own fields, because they are what decide whether signing in can work at all,
 * and they are read from the same probe result and the same theme helpers the
 * Instances window uses so the two cannot disagree about one deployment.
 */
void account_details(const tree_leaf& leaf, const render_input& input,
    window_ui_state& window) {
    static_cast<void>(window);
    const auto handle = input.accounts.find(leaf.id);
    if (!handle) {
        ImGui::TextDisabled("This account is no longer in the store.");
        return;
    }

    const auto& row = *input.accounts.get(*handle);
    const auto instance = instance_state_of(input, row.instance_id);
    const std::string& display = row.name.empty() ? row.principal : row.name;

    /*
     * The head names the account and the state of the instance it belongs to,
     * in the state's own colour and its own word. The labels sit beside them as
     * the same pills the list draws, so a label is one colour on every surface.
     */
    ImGui::TextUnformatted(display.c_str());
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::ColorConvertU32ToFloat4(
            theme::probe_state_colour(instance.result, instance.observed)));
    ImGui::TextUnformatted(
        theme::probe_state_word(instance.result, instance.observed));
    ImGui::PopStyleColor();

    const auto attached =
        forms::labels_of(input, labelling_owner::account, row.id);
    if (!attached.empty()) {
        ImGui::SameLine();
        static_cast<void>(
            widget::label_pill_row(widget::pills_for(attached), 0.0f));
    }

    const float pane_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const int columns = widget::card_columns(pane_width);
    const float width = widget::card_width(pane_width, columns);
    const float height = widget::card_height();

    std::vector<widget::topic_spec> topics;

    widget::topic_spec identity;
    identity.title = "Account";
    identity.cards.push_back(widget::card_spec{.id = "account-name",
        .glyph = icon::account, .title = "Name", .value = display,
        .caption = "the name this account is read by"});
    if (!row.principal.empty()) {
        identity.cards.push_back(widget::card_spec{.id = "account-principal",
            .title = "Principal", .value = row.principal,
            .caption = "what IAM sees when it signs in"});
    }
    topics.push_back(std::move(identity));

    widget::topic_spec deployment;
    deployment.title = "Instance";
    if (!instance.name.empty()) {
        deployment.cards.push_back(widget::card_spec{.id = "instance-name",
            .title = "Instance", .value = instance.name,
            .caption = "the deployment this login belongs to"});
    }
    deployment.cards.push_back(widget::card_spec{.id = "probe-state",
        .glyph = icon::probe, .title = "Probe state",
        .value = theme::probe_state_word(instance.result, instance.observed),
        .caption = "the last probe's answer",
        .colour = theme::probe_state_colour(instance.result, instance.observed)});
    deployment.cards.push_back(widget::card_spec{.id = "reachability",
        .glyph = icon::connect, .title = "Reachability",
        .value = std::string{
            hedgr::model::domain::to_string(instance.result.reach)},
        .caption = "whether the broker answered"});
    deployment.cards.push_back(widget::card_spec{.id = "readiness",
        .glyph = icon::ready, .title = "Readiness",
        .value = std::string{
            hedgr::model::domain::to_string(instance.result.ready)},
        .caption = "whether IAM is behind it"});
    topics.push_back(std::move(deployment));

    const auto sign_in =
        widget::last_sign_in(input.sessions, row.instance_id, row.id);
    widget::topic_spec session;
    session.title = "Session";
    session.cards.push_back(widget::card_spec{.id = "last-sign-in",
        .title = "Last sign-in",
        .value = sign_in.ever_attempted ? session_word(sign_in.status) : "never",
        .caption = sign_in.ever_attempted ? "the last sign-in's answer"
                                          : "no sign-in has been attempted",
        .colour = sign_in.ever_attempted
            ? theme::status_colour(sign_in.status)
            : theme::reachability_colour(
                  hedgr::model::domain::reachability::unknown)});
    if (!sign_in.tenant.empty()) {
        session.cards.push_back(widget::card_spec{.id = "tenant",
            .title = "Tenant", .value = sign_in.tenant,
            .caption = "what IAM named at the last sign-in"});
    }
    if (!sign_in.last_error.empty()) {
        session.cards.push_back(widget::card_spec{.id = "sign-in-detail",
            .title = "Detail", .value = sign_in.last_error,
            .caption = "what the last sign-in reported",
            .colour = sign_in.status == session_status::failed
                ? theme::failure_colour
                : 0,
            .full_width = true,
            .wrap = true});
    }
    topics.push_back(std::move(session));

    widget::draw_topics(topics, pane_width, width, height, columns);
}

}

namespace hedgr::ui::render {

void select_account_for_capture(std::string_view account_id) {
    pending_capture_selected_account = std::string{account_id};
}

/**
 * @brief Draws the Accounts window's own action, in the browser's toolbar.
 *
 * The sign-in is the one control the browser cannot draw for it, and it is the
 * window's reason for existing. It is a rail entry with the others now, an
 * icon and a word, because the operator asked for the worded strip here as
 * well as on the Instances window. The word is "Sign in", which is the
 * action's own name and the same one its tooltip carries.
 */
void accounts_window_actions(const render_input& input,
    window_ui_state& state) {
    using hedgr::model::domain::store_lock;

    /*
     * A rule between the browser's own actions and this window's, so the strip
     * reads as two groups rather than as a column of unrelated buttons.
     * Separator() asks the current layout which way it runs, and the strip is a
     * vertical stack, so it draws a horizontal rule across the strip.
     */
    ImGui::Separator();

    const bool have_selection = !state.browser.selected_leaf.empty();
    const auto selected = have_selection
        ? input.accounts.find(state.browser.selected_leaf)
        : std::optional<hedgr::model::state::account_handle>{};

    const bool sign_in = widget::rail_button("sign-in", icon::sign_in,
        "Sign in", "Sign in with the selected account and open the command "
                   "centre or a workspace",
        have_selection);
    const bool set_password = widget::rail_button("set-password",
        icon::lock_open, "Password",
        "Set or change the stored password for the selected account",
        have_selection);

    if (set_password && selected) {
        /*
         * The prompt opens whatever the account has stored, because this is
         * the path for replacing a password rather than for using one. The
         * submit signs in with it, which is also the only way to find out
         * whether the new password is right.
         */
        std::fill(state.browser.credential.begin(),
            state.browser.credential.end(), '\0');
        state.browser.password_visible = false;
        state.browser.remember = true;
        state.browser.sign_in_open = true;
        return;
    }

    if (!sign_in || !selected)
        return;

    const auto* row = input.accounts.get(*selected);

    /*
     * A stored credential is used when there is one and the store is open;
     * otherwise a password is asked for. Both halves are known before the
     * attempt: the lock says whether the store can be read, and the account
     * says whether there is anything to read. Asking the runtime instead would
     * mean the click did nothing until the refusal came back, which reads as a
     * button that is broken rather than as a password that is missing.
     */
    if (hedgr::ui::render::sign_in_needs_password(input.store_lock,
            row->has_credential)) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Asking for a password for '" << row->principal << "' on '"
            << row->instance_id
            << "' (no stored credential or the store is closed)";
        state.browser.sign_in_open = true;
        return;
    }

    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Signing in '" << row->principal << "' on '" << row->instance_id
        << "' with the stored credential";

    command request;
    request.kind = command_kind::connect_session;
    request.instance_id = row->instance_id;
    request.account_id = row->id;
    request.principal = row->principal;
    input.commands.push(std::move(request));

    /*
     * The landing window is not asked for here. Whether this account's session
     * lands on a workspace or on the command centre is a fact about the tenant
     * IAM resolves, and that is known only once the sign-in has been answered.
     * The runtime opens the right one when it accepts the session, which also
     * means a refused sign-in leaves no window behind.
     */
}

void render_accounts_window(hedgr::model::state::window_handle handle,
    const render_input& input, window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(lock);
    /*
     * The window's own state says which instance it shows. A window whose state
     * is not the accounts shape, or which names an instance that has gone, is
     * drawn as an empty browser rather than as nothing: a window that renders
     * blank explains itself to nobody.
     */
    const auto& shape = input.windows.state(handle);
    const auto* accounts = std::get_if<hedgr::model::domain::accounts_state>(&shape);
    const std::string instance_id =
        accounts != nullptr ? accounts->instance_id : std::string{};
    state.browser.instance = instance_id;
    state.browser.form_owner = instance_id;

    /*
     * A capture's selection is the scratch a click writes and nothing more, so
     * the pane that follows is the one an operator would have reached by hand.
     */
    if (!pending_capture_selected_account.empty()) {
        state.browser.selected_leaf = pending_capture_selected_account;
        state.browser.selected_folder.clear();
        pending_capture_selected_account.clear();
    }

    ImGui::PushID("accounts");

    const auto root = input.folders.root(folder_tree::accounts, instance_id);
    const std::string root_id = root ? input.folders.get(*root)->id : std::string{};

    /*
     * A browser with nothing selected defaults to its root, so the "add" action
     * has somewhere to put what it creates and the list shows the whole tree
     * rather than nothing at all.
     */
    if (state.browser.selected_folder.empty() && !root_id.empty())
        state.browser.selected_folder = root_id;

    /*
     * This window's actions used to be drawn here, in a strip of their own
     * above the browser's, which is what made the window look like it had two
     * toolbars. Adding, editing, duplicating and removing an account are the
     * browser's own add, edit, duplicate and remove -- an account is a leaf of
     * the accounts tree -- so what is left of this window's strip is the one
     * action the browser has no opinion about, the sign-in. It is drawn by
     * @c accounts_window_actions in the browser's single strip.
     */
    widget::browser_model model;
    model.window_actions = &accounts_window_actions;
    model.folders = forms::tree_folders(input, folder_tree::accounts, instance_id);
    model.leaves = account_tree_leaves(input, instance_id);
    model.columns = account_columns();
    model.row_cells = &account_row_cells;
    model.row_labels = &account_row_labels;
    /*
     * The right hand pane shows one account in full rather than every account
     * as a row, which is the operator's "cards just for the current account".
     * The columns and cells above are still declared, because they are the
     * pane's fallback and the browser draws the details whenever this is set.
     */
    model.draw_details = &account_details;
    model.create_leaf = [](window_ui_state& window, std::string_view parent) {
        open_create_form(window, parent);
    };
    model.edit_leaf = [](window_ui_state& window, std::string_view leaf_id) {
        window.browser.form = widget::form_kind::leaf;
        window.browser.form_leaf = std::string{leaf_id};
        window.browser.form_duplicating = false;
        window.draft_name.fill('\0');
    };
    model.duplicate_leaf = [](window_ui_state& window, std::string_view leaf_id) {
        window.browser.form = widget::form_kind::leaf;
        window.browser.form_leaf.clear();
        window.browser.form_duplicating = true;
        window.browser.duplicate_source = std::string{leaf_id};
        window.draft_name.fill('\0');
    };
    model.leaf_form = &draw_form;
    model.sign_in_form = &draw_sign_in_form;
    model.remove_form = &draw_remove_form;
    model.can_reparent = [](const render_input& in, std::string_view id,
                             widget::node_kind, std::string_view to) {
        return forms::folder_move_allowed(in, id, to);
    };
    model.can_write = true;
    model.can_duplicate = true;
    model.can_refresh = false;

    widget::render_browser(model, state, input);

    /*
     * A form the callbacks above opened but could not fill -- a function
     * pointer carries no state -- is filled here, once, from the tables this
     * function holds. Only an empty draft is filled, so a form in progress is
     * never overwritten by the row it came from.
     */
    if (state.browser.form == widget::form_kind::leaf
        && forms::buffer_text(state.draft_name).empty()) {
        if (!state.browser.form_leaf.empty())
            open_edit_form(input, state, state.browser.form_leaf, false);
        else if (!state.browser.duplicate_source.empty())
            open_edit_form(input, state, state.browser.duplicate_source, true);
    }

    /*
     * The foot reports the state of the instance this window belongs to, in the
     * instance's own colour and its own word, with the same disc ahead of the
     * line the Instances window draws. Signing in is only possible when the
     * broker answered and IAM is behind it, so the state belongs on this window
     * and not only on the one that owns the probes. It is drawn last, so the
     * browser above has already taken the height it needs.
     */
    const auto instance_state = instance_state_of(input, instance_id);
    widget::foot_state foot;
    if (!instance_id.empty()) {
        foot.word =
            theme::probe_state_word(instance_state.result, instance_state.observed);
        foot.colour = theme::probe_state_colour(
            instance_state.result, instance_state.observed);
    }
    widget::draw_status_foot(status, input.now_ns, foot);

    ImGui::PopID();
}

}
