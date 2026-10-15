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
#include "hedgr.ui/render/windows.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/account.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/domain/window.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/browser.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/panels.hpp"
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
using hedgr::model::domain::session_status;
using hedgr::model::domain::store_lock;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;
using hedgr::ui::widget::browser_state;
using hedgr::ui::widget::tree_leaf;

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
        leaves.push_back(tree_leaf{row.id, row.folder_id,
            row.name.empty() ? row.principal : row.name, icon::account,
            std::format("{}\n{}", row.principal, row.name)});
    }
    return leaves;
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
    std::string_view leaf_id, bool duplicating) {
    window.browser.form = widget::form_kind::leaf;
    window.browser.form_leaf = duplicating ? std::string{} : std::string{leaf_id};
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
        { "Name", column_width::stretch, false },
        { "Principal", column_width::stretch, false },
        { "Tenant", column_width::fit, false },
        { "Labels", column_width::stretch, false },
        { "Last sign-in", column_width::fit, true },
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

    const auto summary = [&]() -> std::string {
        if (!sign_in.ever_attempted)
            return "never";
        switch (sign_in.status) {
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
    }();

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

}

namespace hedgr::ui::render {

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

    const bool have_selection = !state.browser.selected_leaf.empty();
    const auto selected = have_selection
        ? input.accounts.find(state.browser.selected_leaf)
        : std::optional<hedgr::model::state::account_handle>{};

    if (widget::icon_button("add-account", icon::add, "Add an account"))
        open_create_form(state, state.browser.selected_folder);

    ImGui::SameLine();
    if (widget::icon_button("edit-account", icon::edit,
            "Edit the selected account", have_selection)
        && selected) {
        state.browser.form = widget::form_kind::leaf;
        state.browser.form_leaf = state.browser.selected_leaf;
        state.browser.form_duplicating = false;
        state.draft_name.fill('\0');
    }

    ImGui::SameLine();
    if (widget::icon_button("duplicate-account", icon::duplicate,
            "Copy the selected account", have_selection)
        && selected) {
        state.browser.form = widget::form_kind::leaf;
        state.browser.form_leaf.clear();
        state.browser.form_duplicating = true;
        state.browser.duplicate_source = state.browser.selected_leaf;
        state.draft_name.fill('\0');
    }

    ImGui::SameLine();
    if (widget::icon_button("remove-account", icon::remove,
            "Remove the selected account", have_selection)) {
        state.browser.confirm_remove_leaf = true;
    }

    /*
     * The sign-in action is the one prominent control of this window, so it is
     * a labelled button rather than a bare glyph: it is why the window exists,
     * and an icon alone would make the most important thing here the least
     * legible.
     */
    ImGui::SameLine();
    if (!have_selection)
        ImGui::BeginDisabled();
    const bool sign_in =
        ImGui::Button(std::format("{} Sign in", icon::sign_in).c_str());
    if (!have_selection)
        ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s",
        "Sign in with the selected account and open a workspace");

    if (sign_in && selected) {
        const auto* row = input.accounts.get(*selected);

        /*
         * A stored credential is used when there is one and the store is open;
         * otherwise a password is asked for. Whether one is stored is not
         * something this side can see -- the sealed blob never leaves the store
         * -- so the closed-lock case is the one that can be decided here, and
         * the runtime's refusal is what raises the prompt in every other case.
         */
        if (input.store_lock != store_lock::unlocked) {
            state.browser.sign_in_open = true;
        } else {
            command request;
            request.kind = command_kind::connect_session;
            request.instance_id = row->instance_id;
            request.account_id = row->id;
            request.principal = row->principal;
            input.commands.push(std::move(request));

            /*
             * The workspace follows the session, so it is asked for now. The
             * runtime binds it to the session that the connect command creates,
             * and the frame loop draws it once the session is up rather than
             * when the command was pushed.
             */
            command open;
            open.kind = command_kind::open_window;
            open.instance_id = row->instance_id;
            open.account_id = row->id;
            open.new_window_kind = hedgr::model::domain::window_kind::workspace;
            input.commands.push(std::move(open));
        }
    }

    ImGui::Separator();

    widget::browser_model model;
    model.folders = forms::tree_folders(input, folder_tree::accounts);
    model.leaves = account_tree_leaves(input, instance_id);
    model.columns = account_columns();
    model.row_cells = &account_row_cells;
    model.row_labels = &account_row_labels;
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

    ImGui::PopID();
}

}
