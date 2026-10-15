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
#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/instance.hpp"
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.model/domain/probe.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/browser.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/panels.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace widget = hedgr::ui::widget;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;
using hedgr::model::command::command;
using hedgr::model::command::command_kind;
using hedgr::model::domain::folder_tree;
using hedgr::model::domain::instance;
using hedgr::model::domain::label;
using hedgr::model::domain::labelling_owner;
using hedgr::model::domain::probe_result;
using hedgr::model::domain::reachability;
using hedgr::model::domain::readiness;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;
using hedgr::ui::widget::browser_state;
using hedgr::ui::widget::tree_folder;
using hedgr::ui::widget::tree_leaf;

/**
 * @brief The wire formats the form offers.
 *
 * A closed list rather than free text, because a wire format is a fact about a
 * protocol: a typo in one is not a configuration, it is a connection that fails
 * in a way nobody can diagnose from the error.
 */
constexpr const char* wire_formats[]{ "msgpack", "json" };

/**
 * @brief Copies text into a fixed-size buffer, truncating it to fit.
 *
 * The buffers are what ImGui::InputText edits in place, so one helper states
 * the truncation once: a name longer than the buffer is cut, and the store
 * never sees more than a person could have typed into the field.
 */
void set_buffer(auto& buffer, std::string_view text) {
    std::snprintf(buffer.data(), buffer.size(), "%.*s",
        static_cast<int>(text.size()), text.data());
}

/** @brief A buffer's contents as a string. */
[[nodiscard]] std::string_view buffer_text(const auto& buffer) {
    return std::string_view{buffer.data()};
}

/** @brief Every live folder of one tree, as the browser wants them. */
[[nodiscard]] std::vector<tree_folder> tree_folders(const render_input& input,
    folder_tree tree) {
    std::vector<tree_folder> folders;
    const auto& layout = input.folders.layout();
    const auto rows = input.folders.rows();

    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        const auto& row = rows[index];
        if (row.tree != tree)
            continue;
        folders.push_back(tree_folder{row.id, row.parent_id, row.name,
            row.parent_id.empty()});
    }
    return folders;
}

/**
 * @brief What the last probe found about one instance, and whether one did.
 *
 * The flag is carried rather than inferred from the two enums, because a probe
 * whose answer never arrived and a probe nobody ran look identical in them and
 * are not the same thing to a reader: one is an outage, the other is a row the
 * operator has not waited for yet.
 */
struct instance_status final {
    probe_result result;
    bool observed = false;
    std::string detail;
};

/** @brief The status of one instance, read from the probe table by handle. */
[[nodiscard]] instance_status status_of(const render_input& input,
    std::string_view instance_id) {
    instance_status status;
    const auto handle = input.instances.find(instance_id);
    if (!handle)
        return status;

    const auto* probe = input.probes.get(*handle);
    if (probe == nullptr) {
        status.detail = "no probe has answered for this instance yet";
        return status;
    }
    status.result = *probe;
    status.observed = true;
    status.detail = probe->detail.empty()
        ? std::string{"the probe reported no detail"}
        : probe->detail;
    return status;
}

/**
 * @brief Every live instance, as the browser wants it.
 *
 * The glyph carries the state, so the probe is read here rather than in the
 * list's cells. An entry has one icon and it is in the tree: the state is the
 * icon's colour, and the sentence behind that colour is the icon's tooltip,
 * which is where a reader looks for the cause of an amber or a red one.
 */
[[nodiscard]] std::vector<tree_leaf> instance_tree_leaves(const render_input& input) {
    std::vector<tree_leaf> leaves;
    const auto& layout = input.instances.layout();
    const auto rows = input.instances.rows();

    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        const auto& row = rows[index];
        const auto status = status_of(input, row.id);

        tree_leaf leaf{
            .id = row.id,
            .parent_id = row.folder_id,
            .name = row.name,
            .icon = icon::instance,
            .search_text = std::format("{}\n{}", row.nats_url, row.subject_prefix),
        };
        leaf.icon_colour =
            theme::probe_state_colour(status.result, status.observed);

        /*
         * The state's own word goes first, because that is the fact the reader
         * is looking at, and the probe's detail follows it, because that is the
         * only place the cause of an amber or red row is written down.
         */
        leaf.icon_tooltip = std::format("{}\n{}",
            theme::probe_state_word(status.result, status.observed),
            status.detail);
        leaves.push_back(std::move(leaf));
    }
    return leaves;
}

/** @brief Whether the folder table would accept a move of @p id under @p to. */
[[nodiscard]] bool folder_move_allowed(const render_input& input,
    std::string_view id, std::string_view to) {
    const auto moving = input.folders.find(id);
    const auto target = input.folders.find(to);
    if (!moving || !target)
        return false;

    /* A root is what its tree is anchored by, so it is never moved. */
    if (input.folders.get(*moving)->parent_id.empty())
        return false;

    /*
     * The table refuses a cycle when it is asked to, but asking here is what
     * keeps the refusal ahead of the attempt: the drop is declined and nothing
     * is written, rather than written and then rolled back.
     */
    for (const auto& above : input.folders.ancestors_of(to)) {
        if (above == id)
            return false;
    }
    return input.folders.get(*moving)->parent_id != to;
}

/** @brief The labels attached to an owner, resolved to rows. */
[[nodiscard]] std::vector<const label*> labels_of(const render_input& input,
    labelling_owner owner, std::string_view owner_id) {
    std::vector<const label*> result;
    for (const auto& id : input.labellings.labels_of(owner, owner_id)) {
        if (const auto handle = input.labels.find(id))
            result.push_back(input.labels.get(*handle));
    }
    return result;
}

/** @brief Label names joined for a form field. */
[[nodiscard]] std::string joined_labels(const render_input& input,
    labelling_owner owner, std::string_view owner_id) {
    std::vector<std::string> names;
    for (const auto* row : labels_of(input, owner, owner_id))
        names.push_back(row->name);
    return widget::join_labels(names);
}

/**
 * @brief The key a label name refers to, or the one a new label is stored under.
 *
 * A label is created the first time it is used, so a name the store does not
 * know is not refused: an operator who types "production" means to have a label
 * called production, and telling them to go and create one first would be the
 * application explaining its own schema. The key is derived from the name,
 * because that is what the runtime stores a new label under, and the save
 * command is queued ahead of the attach that names it.
 */
[[nodiscard]] std::string label_key_for(const render_input& input,
    std::string_view name) {
    if (const auto handle = input.labels.find_by_name(name))
        return input.labels.get(*handle)->id;
    return std::format("label:{}", name);
}

/** @brief Attaches and detaches labels until an owner's set matches @p typed. */
void apply_labels(const render_input& input, labelling_owner owner,
    std::string_view owner_id, std::string_view typed) {
    const auto wanted = widget::split_labels(typed);
    const auto current = labels_of(input, owner, owner_id);

    const auto attached = [&](std::string_view name) {
        return std::any_of(current.begin(), current.end(),
            [&](const label* row) { return row->name == name; });
    };

    for (const auto& name : wanted) {
        if (attached(name))
            continue;

        if (!input.labels.find_by_name(name)) {
            command make;
            make.kind = command_kind::save_label;
            make.label_row.id = label_key_for(input, name);
            make.label_row.name = name;
            make.label_row.colour = hedgr::model::domain::label_colour::grey;
            make.label_row.severity = hedgr::model::domain::label_severity::none;
            input.commands.push(std::move(make));
        }

        command attach;
        attach.kind = command_kind::attach_label;
        attach.labelling_row.owner = owner;
        attach.labelling_row.owner_id = std::string{owner_id};
        attach.labelling_row.label_id = label_key_for(input, name);
        input.commands.push(std::move(attach));
    }

    for (const auto* row : current) {
        if (std::find(wanted.begin(), wanted.end(), row->name) != wanted.end())
            continue;
        command detach;
        detach.kind = command_kind::detach_label;
        detach.labelling_row.owner = owner;
        detach.labelling_row.owner_id = std::string{owner_id};
        detach.labelling_row.label_id = row->id;
        input.commands.push(std::move(detach));
    }
}

/** @brief Loads the form's buffers from an instance row, or leaves them blank. */
void load_draft(window_ui_state& state, const instance* row) {
    state.draft_name.fill('\0');
    state.draft_host.fill('\0');
    state.draft_port.fill('\0');
    state.draft_subject.fill('\0');
    state.draft_wire_format.fill('\0');
    state.draft_tls_ca.fill('\0');
    state.draft_tls_cert.fill('\0');
    state.draft_tls_key.fill('\0');
    set_buffer(state.draft_wire_format, "msgpack");

    if (row == nullptr)
        return;

    const auto address = widget::split_host_port(row->nats_url);
    set_buffer(state.draft_name, row->name);
    set_buffer(state.draft_host, address.host);
    set_buffer(state.draft_port, address.port);
    set_buffer(state.draft_subject, row->subject_prefix);
    set_buffer(state.draft_wire_format, row->wire_format);
    set_buffer(state.draft_tls_ca, row->tls_ca);
    set_buffer(state.draft_tls_cert, row->tls_cert);
    set_buffer(state.draft_tls_key, row->tls_key);
}

/** @brief Pushes a save for one instance and the labels it should carry. */
void save_instance(const render_input& input, window_ui_state& state) {
    const std::string_view editing{state.browser.form_leaf};
    const auto existing = input.instances.find(editing);

    const bool duplicating = state.browser.form_duplicating;

    instance row;
    if (existing)
        row = *input.instances.get(*existing);

    /*
     * A new instance's key is derived from its name, which the store holds
     * unique. A serial would be unique too and would also mean the same
     * instance had a different key every time it was created in a test.
     */
    std::string name{buffer_text(state.draft_name)};
    if (duplicating)
        name.append(" copy");

    row.id = existing && !duplicating ? row.id : std::format("inst:{}", name);
    row.name = name;
    row.subject_prefix = buffer_text(state.draft_subject);
    row.wire_format = buffer_text(state.draft_wire_format);
    row.tls_ca = buffer_text(state.draft_tls_ca);
    row.tls_cert = buffer_text(state.draft_tls_cert);
    row.tls_key = buffer_text(state.draft_tls_key);
    row.folder_id = state.browser.form_folder;

    /*
     * The scheme is carried through untouched from the stored row: it is not
     * this form's business which transports exist, and rewriting "tls://" as
     * "nats://" would silently change which deployment the row points at.
     */
    const auto scheme = existing
        ? widget::split_host_port(input.instances.get(*existing)->nats_url).scheme
        : std::string{"nats://"};
    row.nats_url = widget::join_host_port(widget::host_port{scheme,
        std::string{buffer_text(state.draft_host)},
        std::string{buffer_text(state.draft_port)}});

    command request;
    request.kind = command_kind::save_instance;
    request.instance_id = row.id;
    request.instance_row = std::move(row);

    const std::string id = request.instance_id;
    input.commands.push(std::move(request));
    apply_labels(input, labelling_owner::instance, id,
        buffer_text(state.browser.form_labels));
}

/** @brief The instance form: one dialog for adding and for editing. */
void draw_form(const render_input& input, window_ui_state& state) {
    const bool editing = !state.browser.form_leaf.empty();

    /*
     * Fixed, non-resizable, non-collapsible and with no scrolling, like every
     * other dialog here. A modal whose layout can move invites a size that
     * hides its own controls, and one that scrolls hides its own validation.
     */
    const ImVec2 size(460.0f, 560.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    const char* title = editing ? "Edit instance###hedgr-instance-form"
                                : "New instance###hedgr-instance-form";
    ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, flags))
        return;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##instance-name", "name", state.draft_name.data(),
        state.draft_name.size());

    /*
     * The address is edited as a host and a port rather than as one string,
     * because that is how an operator knows it: a person is told "the broker on
     * manta, port 20405", not a URL.
     */
    const float half =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    ImGui::SetNextItemWidth(half);
    ImGui::InputTextWithHint("##instance-host", "host", state.draft_host.data(),
        state.draft_host.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##instance-port", "port", state.draft_port.data(),
        state.draft_port.size());

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##instance-subject", "subject prefix",
        state.draft_subject.data(), state.draft_subject.size());

    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string_view chosen = buffer_text(state.draft_wire_format);
    if (ImGui::BeginCombo("##instance-wire-format",
            chosen.empty() ? "msgpack" : state.draft_wire_format.data())) {
        for (const char* format : wire_formats) {
            if (ImGui::Selectable(format, chosen == format))
                set_buffer(state.draft_wire_format, format);
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", "Wire format");

    /*
     * The labels this instance already carries, as the same pills the list
     * draws, above the field that edits them. The field shows names because a
     * name is what a person types; the pills show the colour and the severity
     * those names will have, so an edit is made against what the row looks like
     * rather than against a list of words.
     */
    const auto attached =
        labels_of(input, labelling_owner::instance, state.browser.form_leaf);
    if (!attached.empty()) {
        ImGui::TextDisabled("Attached:");
        ImGui::SameLine();
        static_cast<void>(widget::label_pill_row(widget::pills_for(attached), 0.0f));
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##instance-labels", "labels, comma separated",
        state.browser.form_labels.data(), state.browser.form_labels.size());
    ImGui::SetItemTooltip("%s",
        "Labels attached to this instance. A name that does not exist yet is "
        "created.");

    /*
     * The instance's own TLS material. It is per instance because a client
     * certificate says who is calling rather than what is called, and one Hedgr
     * process holds instances on several deployments, each of which may want a
     * different caller. A path and not the certificate: the private key stays a
     * file with file permissions and never enters the store.
     *
     * An empty field is not an error and is not "no TLS". It means this
     * instance says nothing and the machine's environment answers instead,
     * which is what keeps a row that predates these fields working.
     */
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##instance-tls-ca", "CA certificate (optional)",
        state.draft_tls_ca.data(), state.draft_tls_ca.size());
    ImGui::SetItemTooltip("%s",
        "The certificate authority this deployment is trusted through. Empty "
        "means the environment's CA, and an empty CA is what turns TLS off.");

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##instance-tls-cert", "client certificate (optional)",
        state.draft_tls_cert.data(), state.draft_tls_cert.size());
    ImGui::SetItemTooltip("%s", "The certificate this client presents. Empty means "
        "the environment's certificate.");

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##instance-tls-key", "client key (optional)",
        state.draft_tls_key.data(), state.draft_tls_key.size());
    ImGui::SetItemTooltip("%s", "The private key for that certificate. Empty means "
        "the environment's key.");

    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped(
        "A relative path is resolved against the folder holding the store, so a "
        "store copied to another host with its keys beside it keeps working. An "
        "empty field falls back to the environment.");
    ImGui::PopStyleColor();

    const bool complete = !buffer_text(state.draft_name).empty()
        && !buffer_text(state.draft_host).empty()
        && !buffer_text(state.draft_subject).empty();

    if (!complete)
        ImGui::BeginDisabled();
    const bool saved = ImGui::Button(std::format("{} Save", icon::save).c_str());
    if (!complete)
        ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", "Save the instance");

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
        save_instance(input, state);
        state.browser.form = widget::form_kind::none;
        state.browser.form_leaf.clear();
        state.browser.form_folder.clear();
        state.browser.form_duplicating = false;
        state.browser.duplicate_source.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

/*
 * The host's callbacks are free functions rather than capturing lambdas: a
 * function pointer carries no state across the boundary, and none is needed,
 * because what a form collects lives in the window state the callback already
 * receives.
 */

/** @brief Opens the form for a new instance inside @p parent. */
void open_create_form(window_ui_state& window, std::string_view parent) {
    window.browser.form = widget::form_kind::leaf;
    window.browser.form_leaf.clear();
    window.browser.form_folder = std::string{parent};
    window.browser.form_duplicating = false;
    window.browser.form_labels.fill('\0');
    load_draft(window, nullptr);
}

/** @brief Opens the form on an instance, or on a blank copy of one. */
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

    const auto handle = input.instances.find(leaf_id);
    load_draft(window, handle ? input.instances.get(*handle) : nullptr);
    if (handle)
        window.browser.form_folder = input.instances.get(*handle)->folder_id;

    if (duplicating && handle) {
        const std::string copy =
            std::format("{} copy", input.instances.get(*handle)->name);
        set_buffer(window.draft_name, copy);
    }
    set_buffer(window.browser.form_labels,
        joined_labels(input, labelling_owner::instance, leaf_id));
}

/** @brief The removal confirmation, naming what a folder removal takes. */
void draw_remove_form(const render_input& input, window_ui_state& window) {
    browser_state& browser = window.browser;
    const ImVec2 size(460.0f, 190.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    const char* title = "Remove###hedgr-browser-remove";
    ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, flags))
        return;

    if (browser.confirm_remove_folder) {
        /*
         * A folder removal names how many instances come out of it, because the
         * count is what makes the click deliberate: "remove this folder" sounds
         * free, and "three instances move to the root" does not.
         */
        const auto doomed = input.folders.descendants_of(browser.selected_folder);
        std::size_t count = 0;
        const auto& layout = input.instances.layout();
        const auto rows = input.instances.rows();
        for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
            if (!layout.is_alive(index))
                continue;
            const auto& row = rows[index];
            if (row.folder_id == browser.selected_folder
                || std::find(doomed.begin(), doomed.end(), row.folder_id)
                    != doomed.end())
                ++count;
        }
        ImGui::TextWrapped(
            "Remove this folder and its subfolders. %zu instance(s) inside are "
            "moved to the root, not removed.",
            count);
    } else {
        const auto handle = input.instances.find(browser.selected_leaf);
        ImGui::TextWrapped("Remove %s?",
            handle ? input.instances.get(*handle)->name.c_str() : "this instance");
    }

    if (ImGui::Button(std::format("{} Remove", icon::remove).c_str())) {
        command request;
        if (browser.confirm_remove_folder) {
            request.kind = command_kind::remove_folder;
            request.folder_row.id = browser.selected_folder;
        } else {
            request.kind = command_kind::remove_instance;
            request.instance_id = browser.selected_leaf;
        }
        input.commands.push(std::move(request));

        browser.confirm_remove_folder = false;
        browser.confirm_remove_leaf = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Remove it");

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Cancel", icon::cancel).c_str())) {
        browser.confirm_remove_folder = false;
        browser.confirm_remove_leaf = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Keep it");

    ImGui::EndPopup();
}

/** @brief The dashboard: four numbers, in the window they belong to. */
void draw_dashboard(const render_input& input, window_ui_state& state) {
    const auto counts = widget::count_instances(input.instances, input.probes);

    /*
     * The panel opens the first time it is drawn. A CollapsingHeader carries no
     * default of its own and overwrites the bool it is handed from its own
     * storage, so an initialiser in the window state cannot open it: this call
     * is the only thing that can. Appearing rather than Always, so the operator
     * can still shut it and have it stay shut.
     */
    ImGui::SetNextItemOpen(
        widget::panel_default_open(widget::panel_kind::dashboard),
        ImGuiCond_Appearing);
    if (!ImGui::CollapsingHeader(
            std::format("{} Dashboard", icon::dashboard).c_str(),
            &state.panel_open))
        return;

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;

    if (!ImGui::BeginTable("##instances-dashboard", 4, flags))
        return;

    const auto cell = [](const char* heading, std::size_t value, ImU32 colour) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", heading);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colour));
        ImGui::Text("%zu", value);
        ImGui::PopStyleColor();
    };

    ImGui::TableNextRow();
    cell("Instances", counts.total, ImGui::GetColorU32(ImGuiCol_Text));
    cell("Ready", counts.ready, theme::readiness_colour(readiness::ready));
    cell("Bootstrapping", counts.bootstrapping,
        theme::readiness_colour(readiness::bootstrapping));
    cell("Unreachable", counts.unreachable,
        theme::reachability_colour(reachability::unreachable));

    ImGui::EndTable();
}

/** @brief The four columns of the instance list. */
[[nodiscard]] std::vector<hedgr::ui::widget::list_column> instance_columns() {
    using hedgr::ui::widget::column_width;
    return {
        /*
         * The first column is the row's icon, and it is the row's whole state:
         * the icon is drawn in the colour the last probe earned, so there is no
         * second column for a reachability and no third for a readiness. Read
         * the tooltip for the reason behind the colour.
         */
        { "Name", column_width::stretch },
        /*
         * Host and Subject are stretched rather than fit, because a fitted
         * column sizes itself to its widest cell and a long address would then
         * squeeze the name beside it. Stretched columns share the room that is
         * left, which is the honest answer to "how wide is a host".
         */
        { "Host", column_width::stretch },
        { "Subject", column_width::stretch },
        { "Labels", column_width::stretch },
    };
}

/**
 * @brief Draws the Instances window's own actions, in the browser's toolbar.
 *
 * What is left of this window's strip once the actions the browser already
 * offers -- add, edit, duplicate, remove -- are subtracted. Adding an instance
 * is the browser's own add, which creates a leaf in the selected folder, and
 * editing and removing one are the browser's edit and remove, so neither is
 * repeated here.
 */
void instance_window_actions(const render_input& input, window_ui_state& state) {
    /*
     * A vertical rule between the browser's own actions and this window's, so
     * the one row reads as two groups rather than as six unrelated buttons.
     */
    ImGui::SameLine();
    /*
     * Separator(), not SeparatorText(""). ImGui's Separator() asks the current
     * layout which way it runs and draws a vertical rule inside a horizontal
     * strip, which is the whole point here. SeparatorText always draws a
     * horizontal rule with a label, so inside this strip it ended the row and
     * pushed the rest of the buttons onto a second one, which read as two
     * toolbars.
     */
    ImGui::Separator();
    /*
     * The separator advanced the line, so the first host action has to
     * rejoin the row or it lands on a second one.
     */
    ImGui::SameLine();

    /*
     * Probe-all belongs here rather than in the application's toolbar: it acts
     * on the instances this window lists, and a control that acts on one
     * window's rows belongs with them.
     */
    if (widget::icon_button("probe-all", icon::probe, "Probe every instance now")) {
        command request;
        request.kind = command_kind::probe_all;
        input.commands.push(std::move(request));
    }

    ImGui::SameLine();
    if (widget::icon_button("open-accounts", icon::account,
            "Open the accounts of the selected instance")) {
        /*
         * The accounts window is found or made by the runtime, which owns the
         * window table: the interface names the instance it wants and the
         * runtime answers with the one window showing it. A second request
         * focuses the window that exists rather than making another.
         */
        if (input.instances.find(state.browser.selected_leaf)) {
            command request;
            request.kind = command_kind::open_window;
            request.instance_id = state.browser.selected_leaf;
            request.new_window_kind = hedgr::model::domain::window_kind::accounts;
            input.commands.push(std::move(request));
        }
    }
}

/** @brief The index of the one column whose cells are label pills. */
constexpr std::size_t instance_label_column = 3;

/**
 * @brief Fills one instance row's cells.
 *
 * While the store is closed every cell that came out of the store is left
 * empty, because those are the fields the lock withholds. The icon and its
 * colour are not blanked: a probe measures the broker rather than the row, so
 * the state of a deployment is not a secret, and a measurement taken minutes
 * ago does not become one when the lock closes. The cells are blanked here, at
 * the source, rather than covered by something drawn over the table afterwards:
 * an overlay is a rectangle that has to agree with a layout it does not own,
 * and text nobody can read is still text that was drawn.
 */
void instance_row_cells(const tree_leaf& leaf, const render_input& input,
    widget::row_cells& cells) {
    const bool readable = shows_instances(input);

    const auto handle = input.instances.find(leaf.id);
    const auto* row = handle ? input.instances.get(*handle) : nullptr;

    /*
     * Every column is text, including the first. The entry's state is on its
     * glyph in the tree and nowhere else: the same icon drawn again in the list
     * was a second copy of one fact, and two copies of one thing read as two
     * facts.
     */
    cells.texts.push_back(readable ? leaf.name : std::string{});
    cells.tooltips.emplace_back(readable && row != nullptr ? row->nats_url
                                                           : std::string{});

    const auto address = readable && row != nullptr
        ? widget::split_host_port(row->nats_url)
        : widget::host_port{};
    cells.texts.push_back(address.host);
    cells.tooltips.push_back(address.host);

    cells.texts.emplace_back(readable && row != nullptr ? row->subject_prefix
                                                        : std::string{});
    cells.tooltips.push_back(cells.texts.back());
}

/**
 * @brief Fills one instance row's label column with pills.
 *
 * Separate from @c instance_row_cells because the labels column is the one
 * column whose cells are drawn rather than written: a label is a name with a
 * colour and a severity, and the colour list that used to travel in a tooltip
 * is what the pill's own fill now shows.
 */
void instance_row_labels(const tree_leaf& leaf, const render_input& input,
    std::vector<std::vector<widget::label_pill>>& cells) {
    cells.resize(instance_columns().size());
    /*
     * A label is the operator's own vocabulary for their estates -- the words
     * "production" and "critical" read the deployment from across the room --
     * so the pills go with the rest of the row while the store is closed.
     */
    if (!shows_instances(input))
        return;
    cells[instance_label_column] = widget::pills_for(
        labels_of(input, labelling_owner::instance, leaf.id));
}

}

namespace hedgr::ui::render {

namespace {

/**
 * @brief A folder a capture asked to see selected, on the next draw.
 *
 * A file-scope variable rather than a parameter, because it is read on the
 * frame after it is written and the renderer's signature is the frame's. It is
 * empty in every ordinary run, and it is consumed the first time the window
 * draws.
 */
std::string pending_capture_folder;

/** @brief Whether a capture asked for the dashboard to be open. */
bool capture_dashboard_open = false;

/**
 * @brief The instance row a capture asked to see the form for, on the next draw.
 *
 * Empty in every ordinary run, and cleared the first time the window draws, so
 * it can open a form once and never again.
 */
std::string pending_capture_edit_instance;

}

void select_folder_for_capture(std::string_view folder_id) {
    pending_capture_folder = std::string{folder_id};
}

void open_dashboard_for_capture() {
    capture_dashboard_open = true;
}

void open_instance_form_for_capture(std::string_view instance_id) {
    pending_capture_edit_instance = std::string{instance_id};
}

namespace {

/**
 * @brief The status line's own metrics.
 *
 * Two text rows and a little air, measured from the current font rather than
 * fixed, so a theme that installs a larger face does not clip the message.
 *
 * Two and not one because the messages are sentences and the window can be
 * made narrow: a single row would either clip the end of the sentence or be
 * elided, and a fact that is worth announcing is worth announcing in full.
 * The second row costs nothing when the sentence fits on one.
 */
constexpr int status_row_lines = 2;
constexpr float status_row_extra = 8.0f;

/**
 * @brief The rows an error gets, which is one more than a remark gets.
 *
 * The runtime's failures are whole sentences that name the record and the
 * reason -- "cannot remove folder instance-root: it is the root of the
 * instances tree" -- and the longest of them, the store's schema refusal,
 * names a path and two version numbers on top of that. Two rows elide it on a
 * narrow window, and an error that is elided is an error that will be read as
 * whatever fits. The extra row is reserved only while an error is on the bar,
 * so a window showing a remark is exactly the height it always was.
 */
constexpr int status_error_row_lines = 3;

}

/**
 * @brief Draws the window's status line: the last thing that happened.
 *
 * The bar itself decides what is on it and whether its time is up; this only
 * gives it the frame's clock and the moment to age itself. Reading the clock
 * here rather than keeping one is what makes the fade frame-rate independent
 * without a timer anywhere in the interface.
 *
 * An error is drawn differently from a remark, and it is drawn differently in
 * two ways at once because the difference has to survive a glance at a line
 * that may be at the far side of the screen: a glyph in front of it and the
 * palette's red rather than the dim grey every other message uses. A remark
 * keeps the disabled text colour, because it is a thing that happened; red on
 * a fade would read as a second alarm going off.
 *
 * The red is the palette's own, named once in the theme rather than borrowed
 * from the lock's helper, which would tie a message about a refused save to a
 * fact about the lock.
 */
void draw_status_line(widget::status_bar& status, std::int64_t now_ns) {
    status.advance(now_ns);

    const bool error =
        status.severity() == widget::status_severity::error;

    /*
     * The fade is a style alpha rather than a colour of its own, so the line
     * keeps the interface's disabled text colour on the way out. A message
     * that changed colour as it went would read as a second message. An error
     * never fades, so it keeps its red for as long as it is on the bar.
     */
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, status.alpha(now_ns));

    /*
     * An error is white behind its red glyph rather than the disabled grey a
     * remark gets. Disabled grey is what an interface uses for something it is
     * not offering, and a failure the operator has to act on is the opposite
     * of that.
     */
    if (error) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(hedgr::ui::theme::failure_colour));
        ImGui::TextUnformatted(hedgr::ui::icon::error);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        /*
         * Wrapped from where the glyph left off rather than from the line's
         * start, because these sentences are long: the store's own schema
         * refusal runs to three lines in this bar, and a wrap position that
         * ignored the glyph would run the last one off the window's edge.
         */
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x
            + ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        /*
         * A remark is wrapped from the line's start, which is where it is
         * drawn: it has no glyph in front of it.
         */
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x
            + ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    ImGui::TextWrapped("%s", status.message().c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::PopStyleVar();
}

/**
 * @brief How tall the window's foot is, whether or not anything is on it.
 *
 * Counted from the font rather than fixed, so a theme that installs a larger
 * face gets a foot that still fits its own text.
 */
[[nodiscard]] float status_foot_height(const widget::status_bar& status) {
    const int rows = status.severity() == widget::status_severity::error
        ? status_error_row_lines
        : status_row_lines;
    return ImGui::GetTextLineHeight() * static_cast<float>(rows)
        + status_row_extra;
}

/**
 * @brief Measures the foot of the window and draws the line into it.
 *
 * Both halves are needed and the split is the point. Reserving the space first
 * is what stops the table above from changing height as messages come and go:
 * a table that grew by two rows whenever something was announced would move
 * the row under the pointer at the worst possible moment. Drawing into a child
 * of its own is what pins the line to the window's foot instead of leaving it
 * wherever the browser happened to stop, which is what put it in the middle of
 * the window when it was drawn straight after the table.
 *
 * The reservation is always made, so nothing below knows whether there is a
 * message; the line is drawn only when there is one.
 */

void draw_status_foot(widget::status_bar& status, std::int64_t now_ns) {
    const float height = status_foot_height(status);

    /*
     * The reservation: an empty child of exactly the foot's height, which
     * advances the cursor past the foot and leaves the line below it with the
     * window's whole width.
     */
    ImGui::BeginChild("##hedgr-status-space", ImVec2(0.0f, height),
        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::EndChild();

    const float bottom = ImGui::GetWindowHeight() - height
        - ImGui::GetStyle().WindowPadding.y;
    ImGui::SetCursorPosY(bottom);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
        ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::Separator();
    ImGui::PopStyleVar();

    ImGui::BeginChild("##hedgr-status-line", ImVec2(0.0f, height),
        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

    /*
     * The status line, and nothing ahead of it. The lock control used to sit
     * here, at the left of the foot, and it moved into the window's one
     * toolbar: the foot is where the runtime's messages are read, and a
     * control that changes the state of the whole window does not belong in
     * the middle of a sentence about a probe.
     */
    if (!status.empty())
        draw_status_line(status, now_ns);

    ImGui::EndChild();
}

void render_instances_window(hedgr::model::state::window_handle handle,
    const render_input& input, window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    /*
     * The Instances window is root: it ignores whether the window table thinks
     * it is open, because closing it would leave an interface with no way to
     * open anything again.
     */
    static_cast<void>(handle);

    ImGui::PushID("instances");

    if (!pending_capture_folder.empty()) {
        state.browser.selected_folder = pending_capture_folder;
        state.browser.selected_leaf.clear();
        pending_capture_folder.clear();
    }
    if (capture_dashboard_open) {
        state.panel_open = true;
        capture_dashboard_open = false;
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    if (!pending_capture_edit_instance.empty()) {
        /*
         * The same scratch a click on the row's edit icon writes, and no more:
         * the fill below then loads the row into the fields, which is what
         * makes the captured form the one an operator would see.
         */
        state.browser.form = widget::form_kind::leaf;
        state.browser.form_leaf = pending_capture_edit_instance;
        state.browser.form_duplicating = false;
        state.draft_name.fill('\0');
        pending_capture_edit_instance.clear();
    }

    /*
     * This window's own actions are drawn in the browser's toolbar rather than
     * in a strip of their own, which is what made one window look like two:
     * the browser drew a row of controls and the window drew a second row above
     * it. There is one strip, beside the tree, and @c instance_window_actions
     * below is this window's share of it.
     */

    /*
     * A closed lock does not take the window away. The surface stays, the tree
     * and the list are drawn where they always are, and what is behind them is
     * veiled: an operator who was told the application had gone -- or who was
     * shown an empty box, or a spinner -- would be looking at a different
     * program from the one they are about to use.
     *
     * The veil is drawn over the table rather than instead of it for the same
     * reason. The rows are dimmed out of legibility, and the shapes underneath
     * are what says the catalogue is there and waiting.
     */
    widget::browser_model model;
    model.folders = tree_folders(input, folder_tree::instances);
    model.leaves = instance_tree_leaves(input);
    model.columns = instance_columns();
    model.window_actions = &instance_window_actions;
    model.lock = &lock;
    model.row_cells = &instance_row_cells;
    model.row_labels = &instance_row_labels;
    model.create_leaf = [](window_ui_state& window, std::string_view parent) {
        open_create_form(window, parent);
    };
    model.edit_leaf = [](window_ui_state& window, std::string_view leaf_id) {
        /*
         * The row is read on the next frame, not here, because a function
         * pointer carries no state across the boundary. This callback asks for
         * the form and names the row; the renderer fills the fields below, and
         * only when the draft is still empty, so a form in progress is never
         * overwritten by its own row.
         */
        window.browser.form = widget::form_kind::leaf;
        window.browser.form_leaf = std::string{leaf_id};
        window.browser.form_duplicating = false;
        window.draft_name.fill('\0');
    };
    model.duplicate_leaf = [](window_ui_state& window, std::string_view leaf_id) {
        window.browser.form = widget::form_kind::leaf;
        window.browser.form_leaf.clear();
        window.browser.form_folder =
            window.browser.selected_folder;
        window.browser.form_duplicating = true;
        window.draft_name.fill('\0');
        /*
         * The row the copy is made from is remembered for the fill that follows
         * on the next frame. The form's folder is the row's own when the copy
         * is requested from the list rather than from the tree.
         */
        window.browser.duplicate_source = std::string{leaf_id};
    };
    model.leaf_form = &draw_form;
    model.remove_form = &draw_remove_form;
    model.can_reparent = [](const render_input& in, std::string_view id,
                             widget::node_kind, std::string_view to) {
        return folder_move_allowed(in, id, to);
    };
    model.can_write = true;
    model.can_refresh = true;

    widget::render_browser(model, state, input);


    /*
     * A form the callbacks above opened but did not fill is filled here, once
     * per frame, from the tables the renderer holds. Doing it after the browser
     * means the fill lands before the form is drawn on the next frame, which is
     * what makes the two-step look like one.
     */
    if (state.browser.form == widget::form_kind::leaf
        && buffer_text(state.draft_name).empty()) {
        if (!state.browser.form_leaf.empty())
            open_edit_form(input, state, state.browser.form_leaf, false);
        else if (!state.browser.duplicate_source.empty())
            open_edit_form(input, state, state.browser.duplicate_source, true);
    }

    /*
     * The status bar, last, so that its reservation is made after everything
     * else has taken the height it needs and its own foot is measured back
     * from the window's edge. It carries the lock control, which is the only
     * way the password form is opened, so it is deliberately drawn in every
     * state rather than only while the store is open.
     */
    draw_status_foot(status, input.now_ns);

    ImGui::PopID();
}

}
