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
#include "hedgr.ui/widget/cards.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "hedgr.ui/widget/panels.hpp"
#include "hedgr.ui/widget/status_foot.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
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
using hedgr::model::domain::window_kind;
using hedgr::ui::render::instance_icon_texture;
using hedgr::ui::render::instance_edit_state;
using hedgr::ui::render::render_input;
using hedgr::ui::render::shortcut_toggle_command;
using hedgr::ui::render::shortcuts_bar_holds;
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

/*
 * What a capture asked the window to show, held until the frame that can show
 * it. File-scope rather than a parameter because the environment is read once,
 * before any window is drawn, and the selection and the drafts it names belong
 * to the frames that follow. Every one is empty in an ordinary run.
 */

/** @brief The folder a capture asked to see selected. */
std::string pending_capture_folder;

/** @brief The row a capture asked to see being edited. */
std::string pending_capture_edit_instance;

/** @brief The row a capture asked to see selected, without editing it. */
std::string pending_capture_selected_instance;

/** @brief The field a capture asked a changed value to be written into. */
std::string pending_capture_field;

/** @brief The value that capture's field takes, as typing it would leave it. */
std::string pending_capture_value;

/** @brief Whether a capture asked to see the Instances window closed. */
bool pending_capture_close_instances = false;

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
 *
 * The detail is the probe's own sentence and is left exactly as it was
 * measured, empty included. A reader of the details pane is owed the cause and
 * not a rewording of it, and a sentence this code composed would be a claim
 * the probe never made.
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
    if (probe == nullptr)
        return status;

    status.result = *probe;
    status.observed = true;
    status.detail = probe->detail;
    return status;
}

/**
 * @brief Every live instance, as the browser wants it.
 *
 * The glyph carries the state, so the probe is read here rather than in the
 * list's cells. An entry has one icon and it is in the tree: the state is the
 * icon's colour, and the tooltip is the state's own word, which is what a
 * reader who has not selected the row has to go on.
 *
 * The probe's sentence is not in the tooltip. It is in the details pane, which
 * has the width to hold it whole, and a tooltip that repeated it would be a
 * second copy of one fact to keep in step.
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
         * One word and no more. It is the answer to "what does this colour
         * mean", which is the whole question a hover can ask before a row is
         * selected; the cause behind the colour waits in the pane.
         */
        leaf.icon_tooltip = theme::probe_state_word(status.result, status.observed);
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

/** @brief Blanks the create form's drafts and starts the wire format default. */
void clear_create_draft(window_ui_state& state) {
    state.draft_name.fill('\0');
    state.draft_host.fill('\0');
    state.draft_port.fill('\0');
    state.draft_subject.fill('\0');
    state.draft_wire_format.fill('\0');
    state.draft_tls_ca.fill('\0');
    state.draft_tls_cert.fill('\0');
    state.draft_tls_key.fill('\0');
    /*
     * The format the client speaks unless the operator says otherwise. It is a
     * starting value and not a stored one, so it is written after the blanks
     * rather than filled for.
     */
    set_buffer(state.draft_wire_format, "msgpack");
}

/**
 * @brief Pushes a save for a new instance and the labels it should carry.
 *
 * Only creation. An existing row is edited in place in the details pane, and
 * that path writes the fields the pane holds rather than these, so the two
 * cannot disagree about which draft a save is reading.
 */
void save_new_instance(const render_input& input, window_ui_state& state) {
    instance row;

    /*
     * A new instance's key is derived from its name, which the store holds
     * unique. A serial would be unique too and would also mean the same
     * instance had a different key every time it was created in a test.
     */
    const std::string name{buffer_text(state.draft_name)};
    row.id = std::format("inst:{}", name);
    row.name = name;
    row.subject_prefix = buffer_text(state.draft_subject);
    row.wire_format = buffer_text(state.draft_wire_format);
    row.tls_ca = buffer_text(state.draft_tls_ca);
    row.tls_cert = buffer_text(state.draft_tls_cert);
    row.tls_key = buffer_text(state.draft_tls_key);
    row.folder_id = state.browser.form_folder;

    /*
     * A new row has no stored scheme to carry through, so it starts on the
     * transport the client is built for. An existing row's scheme never passes
     * through here, which is what keeps an edit from changing it.
     */
    row.nats_url = widget::join_host_port(widget::host_port{
        std::string{"nats://"}, std::string{buffer_text(state.draft_host)},
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

/** @brief Whether the pane is editing the row with @p id. */
[[nodiscard]] bool editing_row(const window_ui_state& state,
    std::string_view id) {
    return state.instance_edit.active && state.instance_edit.target == id;
}

/**
 * @brief Reads the edited row into the pane's drafts.
 *
 * The row is read from the store rather than remembered when the edit began, so
 * a reset always returns to what is held now and not to what was held then.
 *
 * @return Whether the store still holds the row, which is what tells the caller
 *         to clear the pending fill and stop waiting for one.
 */
[[nodiscard]] bool fill_edit_drafts(const render_input& input,
    window_ui_state& state) {
    instance_edit_state& edit = state.instance_edit;
    const auto handle = input.instances.find(edit.target);
    if (!handle)
        return false;

    const auto& row = *input.instances.get(*handle);
    const auto address = widget::split_host_port(row.nats_url);
    set_buffer(edit.name, row.name);
    set_buffer(edit.host, address.host);
    set_buffer(edit.port, address.port);
    set_buffer(edit.subject, row.subject_prefix);
    set_buffer(edit.wire_format, row.wire_format);
    set_buffer(edit.tls_ca, row.tls_ca);
    set_buffer(edit.tls_cert, row.tls_cert);
    set_buffer(edit.tls_key, row.tls_key);
    set_buffer(edit.labels,
        joined_labels(input, labelling_owner::instance, row.id));
    return true;
}

/**
 * @brief Puts the pane into edit mode on @p id.
 *
 * The drafts are not filled here. The toolbar callback that reaches this is
 * handed no tables, so the fill is left pending for the frame the pane draws,
 * where the row can be read.
 */
void begin_instance_edit(window_ui_state& state, std::string id) {
    instance_edit_state& edit = state.instance_edit;
    edit.active = true;
    edit.pending_fill = true;
    edit.confirm_discard = false;
    edit.pending_leaf.clear();
    edit.pending_folder.clear();
    edit.target = std::move(id);
}

/** @brief Leaves edit mode and forgets which row it was on. */
void end_instance_edit(window_ui_state& state) {
    instance_edit_state& edit = state.instance_edit;
    edit.active = false;
    edit.pending_fill = false;
    edit.confirm_discard = false;
    edit.target.clear();
    edit.pending_leaf.clear();
    edit.pending_folder.clear();
}

/**
 * @brief Whether the pane holds anything the store does not.
 *
 * Asked of the row the store holds now rather than of the row that was there
 * when the edit began, so a field put back to its stored value stops being
 * marked and leaving no longer warns about a change that no longer exists.
 */
[[nodiscard]] bool edit_has_changes(const render_input& input,
    const window_ui_state& state) {
    const instance_edit_state& edit = state.instance_edit;
    const auto handle = input.instances.find(edit.target);
    if (!handle)
        return false;

    const auto& row = *input.instances.get(*handle);
    const auto address = widget::split_host_port(row.nats_url);
    return buffer_text(edit.name) != row.name
        || buffer_text(edit.host) != address.host
        || buffer_text(edit.port) != address.port
        || buffer_text(edit.subject) != row.subject_prefix
        || buffer_text(edit.wire_format) != row.wire_format
        || buffer_text(edit.tls_ca) != row.tls_ca
        || buffer_text(edit.tls_cert) != row.tls_cert
        || buffer_text(edit.tls_key) != row.tls_key
        || buffer_text(edit.labels)
            != joined_labels(input, labelling_owner::instance, row.id);
}

/**
 * @brief Pushes a save for the row the pane is editing.
 *
 * The row that is written is the one the store already holds, with only the
 * pane's fields moved onto it, so the picture, the folder and the transport
 * scheme survive an edit the pane never showed. Edit mode ends on the click,
 * because the store applies the command on a later frame and a pane that stayed
 * editable would mark every field changed while it waited for the row to catch
 * up.
 */
void save_edit(const render_input& input, window_ui_state& state) {
    instance_edit_state& edit = state.instance_edit;
    const auto handle = input.instances.find(edit.target);
    if (!handle) {
        end_instance_edit(state);
        return;
    }

    instance row = *input.instances.get(*handle);
    row.name = buffer_text(edit.name);
    row.subject_prefix = buffer_text(edit.subject);
    row.wire_format = buffer_text(edit.wire_format);
    row.tls_ca = buffer_text(edit.tls_ca);
    row.tls_cert = buffer_text(edit.tls_cert);
    row.tls_key = buffer_text(edit.tls_key);

    /*
     * The scheme is carried through untouched from the stored row. It is not
     * this pane's business which transports exist, and rewriting "tls://" as
     * "nats://" would silently change which deployment the row points at.
     */
    const auto scheme = widget::split_host_port(row.nats_url).scheme;
    row.nats_url = widget::join_host_port(widget::host_port{scheme,
        std::string{buffer_text(edit.host)},
        std::string{buffer_text(edit.port)}});

    command request;
    request.kind = command_kind::save_instance;
    request.instance_id = row.id;
    request.instance_row = std::move(row);

    const std::string id = request.instance_id;
    input.commands.push(std::move(request));
    apply_labels(input, labelling_owner::instance, id,
        buffer_text(edit.labels));

    end_instance_edit(state);
}

/**
 * @brief Writes a capture's own value into the edit draft it names.
 *
 * A capture has no keyboard, so the frame that has to show a changed field is
 * reached by this rather than by typing. An unknown field name writes nothing
 * at all, which is the safe answer for a hook that exists only to describe a
 * frame a person could otherwise reach by hand.
 */
void apply_capture_field(window_ui_state& state) {
    if (pending_capture_field.empty())
        return;

    instance_edit_state& edit = state.instance_edit;
    const std::string_view value{pending_capture_value};
    if (pending_capture_field == "name")
        set_buffer(edit.name, value);
    else if (pending_capture_field == "host")
        set_buffer(edit.host, value);
    else if (pending_capture_field == "port")
        set_buffer(edit.port, value);
    else if (pending_capture_field == "subject")
        set_buffer(edit.subject, value);
    else if (pending_capture_field == "wire_format")
        set_buffer(edit.wire_format, value);
    else if (pending_capture_field == "tls_ca")
        set_buffer(edit.tls_ca, value);
    else if (pending_capture_field == "tls_cert")
        set_buffer(edit.tls_cert, value);
    else if (pending_capture_field == "tls_key")
        set_buffer(edit.tls_key, value);
    else if (pending_capture_field == "labels")
        set_buffer(edit.labels, value);

    pending_capture_field.clear();
    pending_capture_value.clear();
}

/**
 * @brief Saves one instance with a new picture key and nothing else changed.
 *
 * The row that is written is the one the store already holds, so the picture is
 * the only field the command can move. The save path is the same one the form
 * uses, which is what keeps this second way of writing an instance subject to
 * the store's own rules rather than to a rule remembered here.
 *
 * An empty key is a real choice and clears the picture. It is not the same as a
 * key whose asset is missing, and the pane tells the two apart.
 */
void save_picture(const render_input& input, const instance& row,
    std::string key) {
    command request;
    request.kind = command_kind::save_instance;
    request.instance_id = row.id;
    request.instance_row = row;
    request.instance_row.icon_key = std::move(key);
    input.commands.push(std::move(request));
}

/**
 * @brief The create form: the one dialog this window still owns.
 *
 * Creation keeps a form because there is no row to edit yet. Changing an
 * existing row happens in the details pane instead, so this dialog is never an
 * edit and never has a row behind it.
 */
void draw_create_form(const render_input& input, window_ui_state& state) {
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

    const char* title = "New instance###hedgr-instance-form";
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
    ImGui::SetItemTooltip("%s", "Create the instance");

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Cancel", icon::cancel).c_str())) {
        state.browser.form = widget::form_kind::none;
        state.browser.form_folder.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Abandon this new instance");

    if (saved && complete) {
        save_new_instance(input, state);
        state.browser.form = widget::form_kind::none;
        state.browser.form_folder.clear();
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
    window.browser.form_folder = std::string{parent};
    window.browser.form_labels.fill('\0');
    clear_create_draft(window);
}

/**
 * @brief The warning shown when leaving would lose an unsaved edit.
 *
 * The selection has already been put back on the edited row by the caller, so
 * the pane behind the dialog still shows exactly what the warning is about.
 * Discarding follows the click that was held back; keeping the edit leaves the
 * selection where the caller put it.
 *
 * The two choices are named in words rather than drawn as icons. Every other
 * confirmation in this interface names its choices in words, and a dialog whose
 * whole purpose is to stop an operator losing work is the last place to make
 * them hover to find out which button is which.
 */
void draw_discard_warning(window_ui_state& state) {
    const ImVec2 size(460.0f, 170.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    const char* title = "Unsaved changes###hedgr-instance-discard";
    ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, flags))
        return;

    ImGui::TextWrapped(
        "This instance has changes that have not been saved. Leaving now loses "
        "them.");

    if (ImGui::Button(std::format("{} Discard", icon::remove).c_str())) {
        /*
         * The click that was held back is applied here, and the edit forgotten.
         * Copying the pending selection out first is what keeps the clear in
         * @c end_instance_edit from taking the destination with it.
         */
        const std::string leaf = state.instance_edit.pending_leaf;
        const std::string folder = state.instance_edit.pending_folder;
        end_instance_edit(state);
        state.browser.selected_leaf = leaf;
        state.browser.selected_folder = folder;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Lose the changes and go");

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Keep editing", icon::cancel).c_str())) {
        state.instance_edit.confirm_discard = false;
        state.instance_edit.pending_leaf.clear();
        state.instance_edit.pending_folder.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Stay on this instance");

    ImGui::EndPopup();
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

/**
 * @brief The three series the estate chart draws.
 *
 * The order is the order the lines are drawn in, which does not matter for what
 * is visible because the lines do not cover one another. The words and colours
 * the tree and the details pane use are not this enum: those keep a state of
 * their own for each answer, and only the chart folds them.
 */
enum class estate_state : std::uint8_t {
    ready,
    not_ready,
    fault,
};

/**
 * @brief The colour of @p state, from the theme helpers the row glyph uses.
 *
 * Not one hex value is repeated here. A row is red or amber or green because
 * the theme says so, and these are the theme's own entries for those colours,
 * so a palette change moves the rows and the lines together.
 */
[[nodiscard]] ImVec4 state_colour(estate_state state) {
    switch (state) {
    case estate_state::ready:
        return ImGui::ColorConvertU32ToFloat4(
            theme::readiness_colour(readiness::ready));
    case estate_state::not_ready:
        return ImGui::ColorConvertU32ToFloat4(
            theme::readiness_colour(readiness::bootstrapping));
    case estate_state::fault:
        /*
         * One red for both faults. The service-missing red stays in the tree
         * and the details pane, where the two faults are told apart by word.
         */
        return ImGui::ColorConvertU32ToFloat4(theme::failure_colour);
    }
    return ImGui::ColorConvertU32ToFloat4(
        theme::readiness_colour(readiness::unknown));
}

/** @brief The word this state carries, used as its plot item's id. */
[[nodiscard]] const char* state_label(estate_state state) {
    switch (state) {
    case estate_state::ready:
        return "Ready";
    case estate_state::not_ready:
        return "Not ready";
    case estate_state::fault:
        return "Fault";
    }
    return "Fault";
}

/**
 * @brief The rolling window the chart shows, in minutes.
 *
 * Thirty minutes is the span the ring covers at one poll every few seconds, so
 * the window shows everything that has been kept and a point leaving the left
 * edge is a sample being forgotten rather than one silently dropped. It is also
 * tens of minutes, which is about thirty minute points across a strip this size
 * and leaves each point far enough from its neighbour to read a line through.
 */
constexpr double window_minutes = 30.0;

/**
 * @brief Writes a wall-clock minute as HH:MM for the x axis.
 *
 * The buckets are placed on the wall clock at draw time, because the samples
 * are on the monotonic clock, which has no hour in it. A reader wants the axis
 * in the clock they are living in.
 */
int format_clock(double value, char* buffer, int size, void*) {
    const auto seconds = static_cast<std::time_t>(value * 60.0);
    std::tm parts{};
    if (localtime_r(&seconds, &parts) == nullptr) {
        std::snprintf(buffer, static_cast<std::size_t>(size), "??:??");
        return 5;
    }
    return static_cast<int>(std::strftime(
        buffer, static_cast<std::size_t>(size), "%H:%M", &parts));
}

/**
 * @brief Instances by state over polling time, as one line a series.
 *
 * The panel is not a collapsible header and carries no title. The operator
 * asked for the chart to be part of the window rather than a thing to open, so
 * there is nothing to collapse and nothing above it to say what it is: the clock
 * axis dates the points and the colours say which state is which.
 *
 * One line a series rather than a stack, because the first question the window
 * is asked is about the estate and not about one deployment, and a stack answers
 * it by hiding the part: ready falling while a fault rises is two lines
 * crossing, and one bar of the same total whichever way it moved.
 */
void draw_dashboard(const render_input& input) {
    /*
     * A strip and not a chart window. The tree and the list already floor their
     * body at two hundred and forty pixels and the browser's own chrome takes
     * about sixty more, so the strip is given what the window can spare after
     * those and after the status foot, capped at a hundred and ten and floored
     * at ninety. The cap is a step down from the hundred and thirty it was, and
     * the floor is where four y ticks and the HH:MM labels still sit apart at
     * the theme's font, which is the point below which shrinking would trade a
     * scrollbar for unreadable marks. Taking the spare rather than a constant is
     * what makes a shorter window shrink the chart instead of pushing the
     * browser and the window itself into a scrollbar.
     */
    constexpr float strip_height = 110.0f;
    constexpr float strip_floor = 90.0f;
    constexpr float browser_chrome = 60.0f;
    constexpr float browser_body_floor = 240.0f;
    constexpr float status_foot = 50.0f;
    constexpr float below_strip =
        browser_chrome + browser_body_floor + status_foot;
    const float spare = ImGui::GetContentRegionAvail().y - below_strip;
    const float plot_height = std::clamp(spare, strip_floor, strip_height);

    /*
     * NoScrollbar makes an overflow show as a clip in a frame rather than as a
     * scrollbar an operator has to report, and the canvas below is sized to
     * what is left inside the child so the content box and the canvas are the
     * same box and no overflow is expected in the first place.
     */
    if (ImGui::BeginChild("##instances-estate-plot",
            ImVec2(-1.0f, plot_height), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar)) {
        const auto& history = input.probes.estate();
        const auto count = history.size();

        /*
         * No legend, because the colours are the identification. The clock axis
         * dates the points, and a key naming red, amber and green beside lines
         * drawn in them is the same fact said twice.
         */
        constexpr ImPlotFlags plot_flags = ImPlotFlags_NoLegend;

        /*
         * The x axis is not fitted. Its limits are the rolling window below, so
         * the minute points keep a fixed pixel spacing and the chart does not
         * rescale as polls arrive, which is what an autofitting axis does. The
         * y axis fits the counts, which is the only thing that should move.
         */
        constexpr ImPlotAxisFlags x_flags = ImPlotAxisFlags_None;
        constexpr ImPlotAxisFlags y_flags = ImPlotAxisFlags_AutoFit;

        /*
         * The canvas takes the leftover box exactly, as a measured positive
         * size rather than -1. That matters because ImPlot clamps a negative
         * size up to its PlotMinSize of one hundred and fifty pixels, which is
         * taller than this strip and was what drew a canvas bigger than the
         * child and put a scrollbar on it.
         */
        const ImVec2 canvas = ImGui::GetContentRegionAvail();

        /*
         * A little vertical fit padding, because the y axis fits the counts
         * exactly and a line sitting on the top or the bottom of that range
         * would be drawn under the plot border. Both a full state and an empty
         * one are real readings, so both have to be visible.
         */
        ImPlot::PushStyleVar(
            ImPlotStyleVar_FitPadding, ImVec2(0.0f, 0.10f));
        if (ImPlot::BeginPlot("##estate-over-time", canvas, plot_flags)) {
            /*
             * No axis words. The y ticks are what a reader measures against, the
             * HH:MM ticks date the points, and the window already says what is
             * counted, so a label on either axis would cost a strip this size
             * room without adding a fact the ticks do not already state.
             */
            ImPlot::SetupAxes(nullptr, nullptr, x_flags, y_flags);

            /*
             * The anchor: the newest sample stands in for now, and the wall
             * clock read at draw time is what the axis is labelled in. The
             * samples carry a monotonic clock, which has no hour in it, so the
             * two are married here at the only place a reader looks. The frame's
             * own monotonic clock is the other half of the pair, which is what
             * keeps a sample's wall time fixed as the run advances instead of
             * drifting between polls.
             */
            const double now_wall_s = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const double now_minutes = now_wall_s / 60.0;
            const double window_start = now_minutes - window_minutes;

            /*
             * The rolling window is explicit and always set, so its right edge
             * is now and it walks forward on its own. This is one of the things
             * that keep the minute points a fixed number of pixels apart for the
             * life of the run. The others are the one-minute spacing of the
             * points and the plot's own width, which is the window's width on
             * screen. A minute is therefore always one window-width divided by
             * the window's minutes, whatever the data does. Letting this axis
             * autofit would break that and space the points out as polls
             * arrived, so it must not be "fixed" by fitting.
             */
            ImPlot::SetupAxisLimits(ImAxis_X1, window_start, now_minutes,
                ImPlotCond_Always);

            /*
             * A tick on every minute of the window, and a label only every
             * fifth one. ImPlot's own locator would choose a round interval like
             * five or ten minutes and leave the other minutes without a tick,
             * which would leave points with no mark to belong to and make the
             * reader count the wrong number of them. The labels are thinned
             * instead, because the points are the data and a label is only there
             * to date them.
             */
            constexpr std::int64_t label_every_minutes = 5;
            constexpr std::size_t tick_capacity = 32;
            std::array<std::array<char, 6>, tick_capacity> tick_text{};
            std::array<const char*, tick_capacity> tick_labels{};
            std::array<double, tick_capacity> tick_values{};
            int tick_count = 0;
            for (std::int64_t minute =
                     static_cast<std::int64_t>(std::ceil(window_start));
                 minute <= static_cast<std::int64_t>(std::floor(now_minutes))
                     && tick_count < static_cast<int>(tick_capacity);
                 ++minute) {
                tick_values[tick_count] = static_cast<double>(minute);
                tick_labels[tick_count] = tick_text[tick_count].data();
                if (minute % label_every_minutes == 0)
                    format_clock(static_cast<double>(minute),
                        tick_text[tick_count].data(), 6, nullptr);
                else
                    tick_text[tick_count][0] = '\0';
                ++tick_count;
            }
            if (tick_count > 0)
                ImPlot::SetupAxisTicks(ImAxis_X1, tick_values.data(),
                    tick_count, tick_labels.data());

            /*
             * An empty history draws the axes and no lines rather than a crash.
             */
            if (count > 0) {
                /*
                 * One point per minute and one line per series, not a stack.
                 * Separate lines let a reader watch ready fall while a fault
                 * rises, which a stack hides by construction. The fold and the
                 * gap filling live in the widget layer, where they can be
                 * tested without a frame; this only draws what they return.
                 */
                const auto series = widget::read_estate_series(
                    history, now_wall_s, input.now_ns, window_minutes);

                const auto line = [&](estate_state state,
                                      const std::array<double,
                                          widget::estate_series::capacity>&
                                          values) {
                    const ImVec4 colour = state_colour(state);
                    const ImPlotSpec spec(ImPlotProp_LineColor, colour);
                    /*
                     * ImPlot's own line rasteriser joins the minute points, and
                     * it stops at a NaN. ImPlotLineFlags_SkipNaN is deliberately
                     * not set, because skipping the NaN would join the line
                     * across the gap and draw the readings that were never
                     * taken.
                     */
                    ImPlot::PlotLine(state_label(state), series.minutes.data(),
                        values.data(), static_cast<int>(series.count), spec);
                };

                if (series.count > 0) {
                    line(estate_state::ready, series.ready);
                    line(estate_state::not_ready, series.not_ready);
                    line(estate_state::fault, series.fault);
                }
            }

            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();
    }
    ImGui::EndChild();
}

/** @brief The four columns of the instance list. */
[[nodiscard]] std::vector<hedgr::ui::widget::list_column> instance_columns() {
    using hedgr::ui::widget::column_width;
    return {
        /*
         * The first column is the row's icon, and it is the row's whole state:
         * the icon is drawn in the colour the last probe earned, so there is no
         * second column for a reachability and no third for a readiness. Select
         * the row for the cause behind the colour; the tree's tooltip carries
         * the state's word and nothing more.
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
 * @brief Opens an entry, which for an instance is the accounts behind it.
 *
 * The toolbar's open button and a double click on a row arrive here, so there
 * is one description of what opening an instance means rather than two that
 * can drift.
 *
 * Nothing is done with the window state. Opening is a request to the runtime
 * rather than a change to what this window shows, and the row that was clicked
 * is already the selected one by the time this runs.
 */
void activate_instance(const render_input& input, window_ui_state& state,
    std::string_view leaf_id) {
    static_cast<void>(state);
    hedgr::ui::render::request_accounts_window(input, leaf_id);
}

/**
 * @brief The gallery of pictures an instance may be given, as a popup.
 *
 * The list is what the graphics component uploaded, which is what the install
 * actually carries. A free text key would let a person name a file that is not
 * there and find out only from the blank pane that followed, so the choice is
 * made from files that exist rather than typed.
 *
 * Each tile is the picture itself and the key is the tooltip, because the
 * illustrations are abstract and the key is the word the store holds. A tile
 * that was chosen writes the key onto the instance and closes the list. The
 * control below the tiles clears the key, which is a choice of its own and not
 * the same as a key whose asset is missing.
 */
void draw_picture_gallery(const render_input& input, window_ui_state& state) {
    browser_state& browser = state.browser;
    if (!ImGui::BeginPopup("##instance-picture")) {
        /*
         * The target is dropped while the list is closed, so a selection made
         * later can never act on the instance the last open named.
         */
        browser.picture_target.clear();
        return;
    }

    const auto handle = input.instances.find(browser.picture_target);
    if (!handle) {
        ImGui::TextDisabled("This instance is no longer in the store.");
        ImGui::EndPopup();
        return;
    }
    const auto& row = *input.instances.get(*handle);

    if (input.instance_icons == nullptr
        || input.instance_icons->entries.empty()) {
        ImGui::TextDisabled("No pictures are installed.");
        ImGui::EndPopup();
        return;
    }

    /*
     * Four tiles to a row. The count is fixed rather than measured from the
     * popup, because the popup sizes itself to what is put in it and a width
     * read before the grid is the width of the frame before it.
     */
    constexpr int tiles_per_row = 4;
    constexpr float tile_side = 72.0f;
    int column = 0;
    for (const auto& entry : input.instance_icons->entries) {
        ImGui::PushID(entry.key.c_str());
        const bool chosen = ImGui::ImageButton("##picture",
            static_cast<ImTextureID>(entry.texture),
            ImVec2(tile_side, tile_side));
        ImGui::PopID();
        ImGui::SetItemTooltip("%s", entry.key.c_str());

        /*
         * A tile offers itself to a capture by its key, so a script can pick a
         * named picture and the pane that follows can be read to see the choice
         * landed. The rectangle is the tile's own, so a click lands on the
         * picture that was named rather than on its neighbour.
         */
        {
            widget::capture_element element;
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            element.min_x = item_min.x;
            element.min_y = item_min.y;
            element.max_x = item_max.x;
            element.max_y = item_max.y;
            element.label_min_x = item_min.x;
            element.label = "picture";
            widget::observe_capture_element(entry.key, element);
        }

        if (chosen) {
            save_picture(input, row, entry.key);
            ImGui::CloseCurrentPopup();
        }
        if (++column % tiles_per_row != 0)
            ImGui::SameLine();
    }

    /*
     * A line of its own under the grid, so the clear control is not read as one
     * more picture in it.
     */
    ImGui::NewLine();
    const bool cleared = widget::icon_button("picture-none", icon::remove,
        "Give this instance no picture");
    {
        widget::capture_element element;
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        element.min_x = item_min.x;
        element.min_y = item_min.y;
        element.max_x = item_max.x;
        element.max_y = item_max.y;
        element.label_min_x = item_min.x;
        element.label = "picture";
        widget::observe_capture_element("none", element);
    }
    if (cleared) {
        save_picture(input, row, std::string{});
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
     * These sit under the browser's own "Actions" heading, which is why nothing
     * here opens a group of its own: the strip is a rail of words, and a second
     * heading would put this window's controls in a set of one.
     *
     * Probe-all belongs here rather than in the application's toolbar: it acts
     * on the instances this window lists, and a control that acts on one
     * window's rows belongs with them.
     */
    if (widget::rail_button(
            "probe-all", icon::probe, "Probe all", "Probe every instance now")) {
        command request;
        request.kind = command_kind::probe_all;
        input.commands.push(std::move(request));
    }

    if (widget::rail_button("open-accounts", icon::account, "Accounts",
            "Open the accounts of the selected instance",
            !state.browser.selected_leaf.empty()))
        hedgr::ui::render::request_accounts_window(input,
            state.browser.selected_leaf);

    /*
     * The shortcut toggle used to sit at the pane's top right, where the pane
     * had no other control and the dismiss glyph on it read as an action on the
     * instance rather than on the bar. It lives here now, with the window's
     * other actions, where it carries a word and the pane goes back to being a
     * place that reports. "Pin" and "Unpin" are the words because the thing it
     * changes is the shortcuts bar, and the glyphs stay the add and dismiss the
     * icon set carries since it has no pin of its own.
     */
    const bool have_instance = !state.browser.selected_leaf.empty();
    const bool on_bar = shortcuts_bar_holds(
        input.shortcuts, window_kind::accounts, state.browser.selected_leaf);
    if (widget::rail_button("shortcut-toggle",
            on_bar ? icon::cancel : icon::add, on_bar ? "Unpin" : "Pin",
            on_bar ? "Take this instance off the shortcuts bar"
                   : "Put this instance on the shortcuts bar",
            have_instance)) {
        input.commands.push(shortcut_toggle_command(
            input, window_kind::accounts, state.browser.selected_leaf));
    }

    /*
     * The picture control acts on the selected instance and opens a gallery
     * rather than a text box, because the set of pictures is what is installed
     * and not what a person can spell. It lives in the toolbar rather than in
     * the pane so the pane stays a place that reports and never has to make
     * room for a control.
     *
     * The control offers itself to a capture as @c "choose-picture | picture",
     * the same way a value offers itself, so the gallery can be driven open and
     * read rather than only pressed by a person.
     */
    const bool picture_pressed = widget::rail_button("choose-picture",
        icon::dashboard, "Picture", "Choose this instance's environment picture",
        !state.browser.selected_leaf.empty());
    {
        widget::capture_element element;
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        element.min_x = item_min.x;
        element.min_y = item_min.y;
        element.max_x = item_max.x;
        element.max_y = item_max.y;
        element.label_min_x = item_min.x;
        element.label = "picture";
        widget::observe_capture_element("choose-picture", element);
    }
    if (picture_pressed) {
        state.browser.picture_target = state.browser.selected_leaf;
        ImGui::OpenPopup("##instance-picture");
    }

    draw_picture_gallery(input, state);
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

/**
 * @brief Opens one editable card and marks it when its draft has changed.
 *
 * The mark is the theme's cyan on the card's outline and bar, on its title and
 * around the value the caller draws, so a changed field is legible as changed
 * without a wash behind the text being read.
 */
template <typename Body>
void edit_card(const char* id, const char* glyph, const char* title,
    bool changed, float width, float height, Body body) {
    const bool open = widget::begin_card(id, width, height, changed);
    if (open) {
        widget::card_title(glyph, title,
            changed ? theme::edit_changed_colour
                    : ImGui::GetColorU32(theme::dim_text()));
        if (changed) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::ColorConvertU32ToFloat4(theme::edit_changed_colour));
        }
        body();
        if (changed)
            ImGui::PopStyleColor();
    }
    widget::end_card();
}

/** @brief Records the control just submitted as a target a capture can name. */
void observe_item(std::string_view target, const char* label) {
    widget::capture_element element;
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    element.min_x = item_min.x;
    element.min_y = item_min.y;
    element.max_x = item_max.x;
    element.max_y = item_max.y;
    element.label_min_x = item_min.x;
    element.label = label;
    widget::observe_capture_element(target, element);
}

/**
 * @brief The in-place editor's two controls.
 *
 * Save is on the left of the pair and Reset on the right, and Reset is the
 * safer of the two to reach for by accident because it puts back what the store
 * already holds rather than writing anything.
 *
 * Save ends the edit because the store applies the command on a later frame,
 * and a pane that stayed editable would mark every field changed while it
 * waited for the row to catch up. Reset reads the drafts back from the store
 * and leaves the operator in edit mode, because putting the fields back is not
 * the same as finishing.
 *
 * The pair is drawn where the pane's head has already put the cursor, which is
 * why nothing here aligns them: the head owns the line and the right edge.
 */
void draw_edit_controls(const render_input& input, window_ui_state& window) {
    const bool saved = widget::icon_button("instance-save", icon::save,
        "Save the changes to this instance");
    observe_item("instance-save", "pane");
    if (saved) {
        save_edit(input, window);
        return;
    }

    ImGui::SameLine();
    const bool reset = widget::icon_button("instance-reset", icon::cancel,
        "Reset this instance's fields to the stored values");
    observe_item("instance-reset", "pane");
    if (reset)
        static_cast<void>(fill_edit_drafts(input, window));
}

/**
 * @brief The read-only head's name and labels as the first cards of the wall.
 *
 * The head used to hold the labels as bare pills so they read as tags rather
 * than as one more field, and the operator asked for the card back. A card is
 * what the wall is made of, so a label beside the name reads as a fact about
 * the instance, which is what it is. The pills inside the card are still the
 * list's own, drawn by the shared row, so a label is one colour everywhere.
 *
 * The two cards take the wall's column width, so the head lines up with the
 * topic under it. A card that wrapped to a second row is left there rather than
 * forced wider, because the pane scrolls and a head narrower than the wall is a
 * head that lies about the grid.
 */
void draw_head_cards(const instance& row,
    const std::vector<widget::label_pill>& pills, float width, float height,
    int columns) {
    int column = 0;
    widget::card_slot(column, columns);
    widget::draw_card(
        widget::card_spec{"name", icon::instance, "Name", row.name,
            "the instance's own name"},
        width, height);

    if (pills.empty())
        return;

    widget::card_slot(column, columns);
    const bool open = widget::begin_card("labels", width, height, false);
    if (open) {
        widget::card_title(
            icon::label, "Labels", ImGui::GetColorU32(theme::dim_text()));
        const float interior =
            width - ImGui::GetStyle().WindowPadding.x * 2.0f;
        static_cast<void>(widget::label_pill_row(pills, interior));
    }
    widget::end_card();
}

/**
 * @brief The pane's head: the instance's name, its labels and its picture.
 *
 * The head is what the wall below is about. Its first two cards are the name
 * and the labels, and the picture is taken out of the pane's right edge rather
 * than set beside them, because the operator wants the name read first and the
 * square not to push the facts along with it. Edit mode draws no cards here,
 * because the name and the labels are the form's own fields and belong under
 * Identity with the rest of them.
 *
 * The square takes a card's height so the head reads as the first thing in the
 * wall. An instance that names no picture gets no picture space at all, so the
 * laboratory instance's head is its two cards or its name alone, and the wall
 * begins directly under them. A head that ends in cards still leaves the cursor
 * below the row, because a cursor left beside the picture would draw the
 * Connection heading on the same line and over it.
 *
 * The pane carries no control of its own. The shortcut toggle that used to sit
 * at its top right lives in the toolbar, so what is left here is the instance's
 * own report. Save and Reset join the head only while the pane is editable,
 * pushed against the pane's right edge, and the picture steps aside for them.
 */
void draw_pane_header(const render_input& input, window_ui_state& window,
    const instance& row, bool editing, float pane_width, float width,
    float height, int columns) {
    const std::string directory = input.instance_icons != nullptr
        ? input.instance_icons->directory : std::string{};
    const std::string picture_path =
        widget::instance_picture_path(directory, row.icon_key);
    const std::uint64_t picture = instance_icon_texture(input, row.id);
    /*
     * A key whose asset is not installed says so rather than drawing nothing.
     * Nothing drawn is what an instance with no picture looks like, and a
     * choice the install cannot satisfy is a different fact, so the missing
     * key keeps a place in the head.
     */
    const bool missing_picture = picture == 0 && !row.icon_key.empty();
    const bool has_picture = picture != 0 || missing_picture;
    const auto attached = labels_of(input, labelling_owner::instance, row.id);
    const auto pills = widget::pills_for(attached);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float icon_width = ImGui::GetFrameHeight();
    const float controls =
        editing ? icon_width * 2.0f + style.ItemSpacing.x : 0.0f;
    const float side = widget::card_height();
    const float gap = style.ItemSpacing.x;
    const float start_x = ImGui::GetCursorPosX();
    const float start_y = ImGui::GetCursorPosY();
    float bottom = start_y;

    /*
     * The head shares the wall's own column width while two cards and the
     * square fit on one line together, which is what lines the head up with the
     * topic under it. A pane too narrow for all three falls back to the column
     * rule applied to the room the square leaves, so the cards are never drawn
     * under the picture. The picture is taken from the pane's right edge either
     * way.
     */
    if (!editing) {
        const float picture_room = has_picture ? side + gap : 0.0f;
        int head_columns = columns;
        float head_width = width;
        if (2.0f * head_width + widget::card_gap + picture_room > pane_width) {
            const float cards_room =
                std::max(pane_width - picture_room, 1.0f);
            head_columns = widget::card_columns(cards_room);
            head_width = widget::card_width(cards_room, head_columns);
        }
        draw_head_cards(row, pills, head_width, height, head_columns);
        bottom = std::max(bottom, ImGui::GetCursorPosY());
    }

    if (has_picture) {
        if (!editing)
            ImGui::SetCursorPos(ImVec2(
                start_x + std::max(pane_width - side, 0.0f), start_y));
        if (picture != 0) {
            ImGui::Image(static_cast<ImTextureID>(picture), ImVec2(side, side));
            ImGui::SetItemTooltip("%s", picture_path.c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::ColorConvertU32ToFloat4(theme::failure_colour));
            ImGui::TextUnformatted(icon::warning);
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip(
                "%s No picture is installed for the key \"%s\".", icon::warning,
                row.icon_key.c_str());
        }
        bottom = std::max(bottom, start_y + side);
    }

    /*
     * Save and Reset are pushed against the pane's right edge while the pane is
     * editable, measured from the room left on the line rather than from the
     * pane's own width, so the pair lands there whatever the picture took.
     */
    if (editing) {
        if (has_picture)
            ImGui::SameLine();
        const float room = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + std::max(room - controls, 0.0f));
        draw_edit_controls(input, window);
        bottom = std::max(bottom, ImGui::GetCursorPosY());
    }

    /*
     * The cursor is put back to the pane's left, below everything the head
     * drew. Leaving it at the picture or at the controls would open the first
     * topic on that line, beside the head rather than under it.
     */
    ImGui::SetCursorPos(ImVec2(start_x, bottom));
}

/**
 * @brief The pane while it is the form: every field editable, in cards.
 *
 * Every field is drawn whether or not the row carries it, because an empty
 * field is what an operator fills and a hidden one could never be. The topics
 * and the order are the read-only pane's, so a field does not move when the
 * pane enters edit mode.
 */
void draw_edit_topics(const render_input& input, window_ui_state& window,
    const instance& row, float pane_width, float width, float height,
    int columns) {
    instance_edit_state& edit = window.instance_edit;
    const auto address = widget::split_host_port(row.nats_url);
    int column = 0;

    /*
     * Where the next card goes and how wide it is. A full width card starts its
     * own line, so a row part drawn ends here and the card after it opens a new
     * row. The cursor is already on the next line after the card before, which
     * is why beginning one is a matter of not asking for SameLine.
     */
    const auto next_card_width = [&](bool full_width) {
        if (full_width) {
            column = 0;
            return pane_width;
        }
        widget::card_slot(column, columns);
        return width;
    };

    ImGui::SeparatorText("Identity");
    column = 0;

    widget::card_slot(column, columns);
    edit_card("edit-name", icon::instance, "Name",
        buffer_text(edit.name) != row.name, width, height, [&] {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##instance-edit-name", edit.name.data(),
                edit.name.size());
        });

    widget::card_slot(column, columns);
    edit_card("edit-labels", icon::label, "Labels",
        buffer_text(edit.labels)
            != joined_labels(input, labelling_owner::instance, row.id),
        width, height, [&] {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##instance-edit-labels",
                "labels, comma separated", edit.labels.data(),
                edit.labels.size());
            ImGui::SetItemTooltip("%s",
                "Labels attached to this instance. A name that does not exist "
                "yet is created.");
        });

    ImGui::SeparatorText("Connection");
    column = 0;

    widget::card_slot(column, columns);
    edit_card("edit-host", icon::link, "Host",
        buffer_text(edit.host) != address.host
            || buffer_text(edit.port) != address.port,
        width, height, [&] {
            const float half = (ImGui::GetContentRegionAvail().x
                - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
            ImGui::SetNextItemWidth(half);
            ImGui::InputText("##instance-edit-host", edit.host.data(),
                edit.host.size());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##instance-edit-port", edit.port.data(),
                edit.port.size());
        });

    widget::card_slot(column, columns);
    edit_card("edit-subject", nullptr, "Subject prefix",
        buffer_text(edit.subject) != row.subject_prefix, width, height, [&] {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##instance-edit-subject", edit.subject.data(),
                edit.subject.size());
        });

    widget::card_slot(column, columns);
    edit_card("edit-wire", nullptr, "Wire format",
        buffer_text(edit.wire_format) != row.wire_format, width, height, [&] {
            const std::string_view picked = buffer_text(edit.wire_format);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##instance-edit-wire-format",
                    picked.empty() ? "msgpack" : edit.wire_format.data())) {
                for (const char* format : wire_formats) {
                    if (ImGui::Selectable(format, picked == format))
                        set_buffer(edit.wire_format, format);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("%s", "Wire format");
        });

    ImGui::SeparatorText("Certificates and keys");
    column = 0;

    edit_card("edit-ca", nullptr, "CA certificate",
        buffer_text(edit.tls_ca) != row.tls_ca, next_card_width(true), height,
        [&] {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##instance-edit-tls-ca", edit.tls_ca.data(),
                edit.tls_ca.size());
        });

    edit_card("edit-cert", nullptr, "Client cert",
        buffer_text(edit.tls_cert) != row.tls_cert, next_card_width(true),
        height, [&] {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##instance-edit-tls-cert", edit.tls_cert.data(),
                edit.tls_cert.size());
        });

    edit_card("edit-key", icon::unlock, "Client key",
        buffer_text(edit.tls_key) != row.tls_key, next_card_width(true), height,
        [&] {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##instance-edit-tls-key", edit.tls_key.data(),
                edit.tls_key.size());
        });
}

/**
 * @brief The pane read only: the row's fields as cards, grouped by topic.
 *
 * The topics are the ones the data actually has, and a topic appears only when
 * it has something to show. A topic with one card is not a topic, so a lone
 * card follows the cards above it under their heading rather than carrying a
 * heading of its own.
 *
 * The probe's answers are the last topic, because they are a measurement of the
 * deployment rather than a property of the record, and a reader looking for why
 * a row is red starts at the bottom. The probe's own sentence leads that topic,
 * because the state word is already on the Probe state card below and at the
 * right end of the status foot, and the foot of the wall is what gives when it
 * does not fit.
 */
void draw_read_only_topics(const render_input& input, const instance& row,
    const instance_status& status, float pane_width, float width, float height,
    int columns) {
    const auto address = widget::split_host_port(row.nats_url);

    /*
     * A card is a line of text by default. The failure sentence is the one
     * value here written as more than one line, so it asks for the wrapping
     * form and the card grows to whatever the wrapped text needs.
     */
    const auto text_card = [](const char* id, const char* glyph,
                               const char* title, std::string value,
                               const char* caption, std::uint32_t colour = 0,
                               bool full_width = false, bool wrap = false) {
        return widget::card_spec{id, glyph, title, std::move(value), caption,
            colour, full_width, wrap};
    };
    const auto add = [&](widget::topic_spec& topic, const char* id,
                         const char* glyph,
                         const char* title, std::string value,
                         const char* caption, std::uint32_t colour = 0,
                         bool full_width = false, bool wrap = false) {
        if (value.empty())
            return;
        topic.cards.push_back(text_card(id, glyph, title, std::move(value),
            caption, colour, full_width, wrap));
    };

    std::vector<widget::topic_spec> topics;

    /*
     * There is no Identity topic. Its two facts are the head's own cards and
     * the picture it named is the head's square, so a heading here would sit
     * over cards the head has already drawn. The wall begins where the head
     * stops, with where the instance is and how the client reaches it.
     */

    widget::topic_spec connection;
    connection.title = "Connection";
    add(connection, "host", icon::link, "Host",
        widget::join_host_port(widget::host_port{
            std::string{}, address.host, address.port}),
        "where the broker answers");
    add(connection, "subject", nullptr, "Subject prefix", row.subject_prefix,
        "what every service of it answers to");
    add(connection, "wire", nullptr, "Wire format", row.wire_format,
        "how the client writes its messages");
    topics.push_back(std::move(connection));

    widget::topic_spec certificates;
    certificates.title = "Certificates and keys";
    add(certificates, "ca", nullptr, "CA certificate", row.tls_ca,
        "the authority that signed the others", 0, true);
    add(certificates, "cert", nullptr, "Client cert", row.tls_cert,
        "what this client presents", 0, true);
    add(certificates, "key", icon::unlock, "Client key", row.tls_key,
        "the private half of that certificate", 0, true);
    topics.push_back(std::move(certificates));

    widget::topic_spec diagnostics;
    diagnostics.title = "Diagnostics";
    /*
     * The probe's own sentence comes first in this topic, ahead of the state it
     * explains. The state word still sits on the Probe state card below and at
     * the right end of the status foot, so the row a reader has to work down
     * the wall for is the cause rather than the answer they have been given
     * twice. The sentence is written as more than one line, so the card takes
     * the height the whole of it needs instead of showing only the headline.
     * The path cards take a line each, so this topic is the tallest of the wall
     * and a reader reaches its foot by scrolling the pane.
     */
    if (!status.detail.empty()) {
        const bool failing = status.result.reach == reachability::unreachable
            || status.result.ready != readiness::ready;
        add(diagnostics, "detail", nullptr,
            status.result.bootstrap ? "IAM message" : "Detail",
            status.detail, "what the probe reported",
            failing ? theme::probe_state_colour(status.result, true) : 0, true,
            true);
    }
    diagnostics.cards.push_back(
        text_card("probe-state", icon::probe, "Probe state",
            theme::probe_state_word(status.result, status.observed),
            "the last probe's answer",
            theme::probe_state_colour(status.result, status.observed)));
    diagnostics.cards.push_back(
        text_card("reachability", icon::connect, "Reachability",
            std::string{hedgr::model::domain::to_string(status.result.reach)},
            "whether the broker answered"));
    diagnostics.cards.push_back(
        text_card("readiness", icon::ready, "Readiness",
            std::string{hedgr::model::domain::to_string(status.result.ready)},
            "whether IAM is behind it"));
    if (status.result.bootstrap) {
        const auto& bootstrap = *status.result.bootstrap;
        add(diagnostics, "bootstrap", nullptr, "Bootstrap mode",
            bootstrap.in_bootstrap_mode ? "yes" : "no",
            "whether it is still setting itself up");
        add(diagnostics, "tenant", nullptr, "Has tenant",
            bootstrap.has_tenant ? "yes" : "no",
            "whether it has a tenant yet");
        if (bootstrap.version) {
            add(diagnostics, "version", nullptr, "Version",
                *bootstrap.version, "the version it reported");
        }
    }
    if (status.result.latency_us > 0) {
        add(diagnostics, "latency", icon::clock, "Latency",
            std::format("{:.1f} ms",
                static_cast<double>(status.result.latency_us) / 1000.0),
            "the last round trip");
    }
    if (status.result.observed_at_ns > 0
        && input.now_ns >= status.result.observed_at_ns) {
        add(diagnostics, "observed", icon::clock, "Observed",
            std::format("{:.0f} s ago",
                static_cast<double>(
                    input.now_ns - status.result.observed_at_ns) / 1.0e9),
            "when the last probe ran");
    }
    topics.push_back(std::move(diagnostics));

    /*
     * The wall is drawn from the shared card widget, so the Instances window
     * and the Accounts window lay their cards out by one set of rules: a topic
     * with two cards or more takes its heading, a topic with one folds into the
     * row above it, and a full width card takes a line of its own.
     */
    widget::draw_topics(topics, pane_width, width, height, columns);
}

/**
 * @brief Draws one instance's details, in the browser's right hand pane.
 *
 * Read only until the toolbar's edit control puts the pane into edit mode, and
 * then the same cards become editable in place. The pane is the form while it
 * is in that mode, which is why the drafts and the two controls live here
 * rather than in a dialog: an operator changes the field they are looking at
 * instead of a copy of it.
 *
 * The wall is sized from the pane, so the number of columns follows the window
 * the operator chose. The pane scrolls when the wall is taller than it, so a
 * window too short for every topic raises the theme's bar rather than clipping
 * what falls past its foot, and the topics stay ordered as they are because a
 * reader working down the wall still meets the record before its measurement.
 */
void instance_details(const tree_leaf& leaf, const render_input& input,
    window_ui_state& window) {
    const auto handle = input.instances.find(leaf.id);
    if (!handle) {
        ImGui::TextDisabled("This instance is no longer in the store.");
        return;
    }
    const auto& row = *input.instances.get(*handle);
    const auto status = status_of(input, row.id);

    instance_edit_state& edit = window.instance_edit;
    const bool editing = editing_row(window, row.id);
    /*
     * The fill waits until the store holds the row, because a capture or a
     * click can name an edit before the catalogue is readable. The capture's own
     * value is written after the fill, so it lands on the drafts as typing would
     * rather than being overwritten by them.
     */
    if (editing && edit.pending_fill && fill_edit_drafts(input, window)) {
        edit.pending_fill = false;
        apply_capture_field(window);
    }

    const float pane_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const int columns = widget::card_columns(pane_width);
    const float width = widget::card_width(pane_width, columns);
    const float height = widget::card_height();

    draw_pane_header(
        input, window, row, editing, pane_width, width, height, columns);

    if (editing)
        draw_edit_topics(
            input, window, row, pane_width, width, height, columns);
    else
        draw_read_only_topics(
            input, row, status, pane_width, width, height, columns);
}

}

namespace hedgr::ui::render {

void select_folder_for_capture(std::string_view folder_id) {
    pending_capture_folder = std::string{folder_id};
}

void select_instance_for_capture(std::string_view instance_id) {
    pending_capture_selected_instance = std::string{instance_id};
}

void open_instance_edit_for_capture(std::string_view instance_id) {
    pending_capture_edit_instance = std::string{instance_id};
}

void change_instance_field_for_capture(std::string_view field,
    std::string_view value) {
    pending_capture_field = std::string{field};
    pending_capture_value = std::string{value};
}

void close_instances_for_capture() {
    pending_capture_close_instances = true;
}

void render_instances_window(hedgr::model::state::window_handle handle,
    const render_input& input, window_ui_state& state,
    hedgr::ui::widget::status_bar& status,
    lock_control& lock) {
    static_cast<void>(handle);

    ImGui::PushID("instances");

    /*
     * A capture asks for the root to be closed the way the control asks for it,
     * by pushing the close command the title bar pushes, so the frame that
     * follows is one a click on the control would have produced. It is read here
     * because this is the frame the window is still drawn on; the command
     * removes the row and the frames after it draw no root at all.
     */
    if (pending_capture_close_instances) {
        pending_capture_close_instances = false;
        hedgr::model::command::command request;
        request.kind = hedgr::model::command::command_kind::close_window;
        request.window_id = "instance-manager";
        input.commands.push(std::move(request));
    }

    if (!pending_capture_folder.empty()) {
        state.browser.selected_folder = pending_capture_folder;
        state.browser.selected_leaf.clear();
        pending_capture_folder.clear();
    }
    if (!pending_capture_selected_instance.empty()) {
        state.browser.selected_leaf = pending_capture_selected_instance;
        state.browser.selected_folder.clear();
        pending_capture_selected_instance.clear();
    }
    if (!pending_capture_edit_instance.empty()) {
        /*
         * The same scratch a click on the row's edit icon writes, and no more:
         * the pane fills the drafts from the row on the frame it draws, which
         * is what makes the captured edit the one an operator would see.
         */
        state.browser.selected_leaf = pending_capture_edit_instance;
        state.browser.selected_folder.clear();
        begin_instance_edit(state, pending_capture_edit_instance);
        pending_capture_edit_instance.clear();
    }

    /*
     * A click on another row or another folder changes the selection inside the
     * browser, so leaving is caught here, before the browser draws, and the
     * selection is put back. The operator is warned only when the pane actually
     * holds something: an edit with nothing changed is abandoned silently,
     * because a warning that appears when there is nothing to lose is worse
     * than no warning at all.
     *
     * Closing the window is not a way out of an edit either. The window's own
     * scratch is keyed by its stable id, so the drafts outlive the close and
     * the shortcut that reopens the window resumes them. The lock and the
     * overlays are not ways out either, for the same reason.
     */
    if (state.instance_edit.active && !state.instance_edit.confirm_discard) {
        const bool left_leaf =
            state.browser.selected_leaf != state.instance_edit.target;
        const bool left_folder = !state.browser.selected_folder.empty();
        if (left_leaf || left_folder) {
            if (edit_has_changes(input, state)) {
                state.instance_edit.pending_leaf = state.browser.selected_leaf;
                state.instance_edit.pending_folder =
                    state.browser.selected_folder;
                state.instance_edit.confirm_discard = true;
                state.browser.selected_leaf = state.instance_edit.target;
                state.browser.selected_folder.clear();
            } else {
                end_instance_edit(state);
            }
        }
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
    model.activate_leaf = &activate_instance;
    model.row_cells = &instance_row_cells;
    model.row_labels = &instance_row_labels;
    /*
     * The right hand pane shows one instance in full rather than every instance
     * as a row. The columns and the cells above are still declared, because they
     * are the pane's fallback and a window that stopped describing its rows
     * would have to grow a second description the day the details callback was
     * cleared. The browser draws the details whenever this is set.
     */
    model.draw_details = &instance_details;
    model.create_leaf = [](window_ui_state& window, std::string_view parent) {
        open_create_form(window, parent);
    };
    model.edit_leaf = [](window_ui_state& window, std::string_view leaf_id) {
        /*
         * Clicking edit on the row already being edited does nothing, because
         * refilling the drafts from the store would throw away what the
         * operator has typed and the toolbar button is the one they will reach
         * for again. A reset is offered in the pane for putting the fields back
         * deliberately.
         */
        if (editing_row(window, leaf_id))
            return;
        begin_instance_edit(window, std::string{leaf_id});
    };
    model.leaf_form = &draw_create_form;
    model.remove_form = &draw_remove_form;
    model.can_reparent = [](const render_input& in, std::string_view id,
                             widget::node_kind, std::string_view to) {
        return folder_move_allowed(in, id, to);
    };
    model.can_write = true;
    model.can_refresh = true;

    /*
     * The chart, above the browser and with no header of its own. It answers
     * "is anything wrong" before a single row has been read, and it stays drawn
     * rather than being folded away, because a panel an operator has to open is
     * a panel that hides the one thing the window opens to show.
     */
    draw_dashboard(input);

    widget::render_browser(model, state, input);

    /*
     * The leave warning, drawn over the window once the browser has put it up.
     * It is opened by the guard above and stays up until the operator answers,
     * so the pane behind it keeps showing the row whose changes are at stake.
     */
    if (state.instance_edit.confirm_discard)
        draw_discard_warning(state);

    /*
     * The selected instance's state, read from the probe table by the
     * selection the browser holds. An instance the store does not hold, or
     * none at all, has no state to report and the bar is the message alone.
     */
    widget::foot_state foot;
    if (!state.browser.selected_leaf.empty()
        && input.instances.find(state.browser.selected_leaf)) {
        const auto probe = status_of(input, state.browser.selected_leaf);
        foot.word = theme::probe_state_word(probe.result, probe.observed);
        foot.colour = theme::probe_state_colour(probe.result, probe.observed);
    }

    /*
     * The status bar, last, so that its reservation is made after everything
     * else has taken the height it needs and its own foot is measured back
     * from the window's edge. It carries the runtime's messages and nothing
     * else: the lock control used to live here and moved into the window's one
     * toolbar, so the bar is read and not operated.
     */
    widget::draw_status_foot(status, input.now_ns, foot);

    ImGui::PopID();
}

}
