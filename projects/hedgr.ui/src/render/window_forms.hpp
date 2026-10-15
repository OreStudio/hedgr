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
#ifndef HEDGR_UI_RENDER_WINDOW_FORMS_HPP
#define HEDGR_UI_RENDER_WINDOW_FORMS_HPP

#include "hedgr.model/domain/folder.hpp"
#include "hedgr.model/domain/label.hpp"
#include "hedgr.model/domain/labelling.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief The parts of building a form that both hosting windows need.
 *
 * The Instances window and the Accounts window browse different leaves and
 * collect different fields, but both attach labels by name, both fill a
 * fixed-size buffer for ImGui, and both read a folder's contents. None of that
 * belongs to either window, so it lives here rather than being copied into
 * both.
 *
 * This is a header of inline helpers rather than a translation unit, because
 * every one of them is a handful of lines over the model's tables and a call is
 * cheaper than a symbol.
 */
namespace hedgr::ui::render::forms {

/**
 * @brief Copies text into a fixed-size buffer, truncating it to fit.
 *
 * The buffers are what ImGui::InputText edits in place, so this is where the
 * truncation rule is stated once: a name longer than the buffer is cut, and the
 * store never sees more than a person could have typed into the field.
 */
inline void set_buffer(auto& buffer, std::string_view text) {
    std::snprintf(buffer.data(), buffer.size(), "%.*s",
        static_cast<int>(text.size()), text.data());
}

/** @brief A buffer's contents as a string. */
[[nodiscard]] inline std::string_view buffer_text(const auto& buffer) {
    return std::string_view{buffer.data()};
}

/** @brief The labels attached to an owner, resolved to rows. */
[[nodiscard]] inline std::vector<const hedgr::model::domain::label*>
labels_of(const render_input& input,
    hedgr::model::domain::labelling_owner owner, std::string_view owner_id) {
    std::vector<const hedgr::model::domain::label*> result;
    for (const auto& id : input.labellings.labels_of(owner, owner_id)) {
        if (const auto handle = input.labels.find(id))
            result.push_back(input.labels.get(*handle));
    }
    return result;
}

/** @brief Label names joined for a form field. */
[[nodiscard]] inline std::string joined_labels(const render_input& input,
    hedgr::model::domain::labelling_owner owner, std::string_view owner_id) {
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
[[nodiscard]] inline std::string label_key_for(const render_input& input,
    std::string_view name) {
    if (const auto handle = input.labels.find_by_name(name))
        return input.labels.get(*handle)->id;
    return std::format("label:{}", name);
}

/** @brief Attaches and detaches labels until an owner's set matches @p typed. */
inline void apply_labels(const render_input& input,
    hedgr::model::domain::labelling_owner owner, std::string_view owner_id,
    std::string_view typed) {
    const auto wanted = widget::split_labels(typed);
    const auto current = labels_of(input, owner, owner_id);

    const auto attached = [&](std::string_view name) {
        return std::any_of(current.begin(), current.end(),
            [&](const hedgr::model::domain::label* row) {
                return row->name == name;
            });
    };

    for (const auto& name : wanted) {
        if (attached(name))
            continue;

        if (!input.labels.find_by_name(name)) {
            hedgr::model::command::command make;
            make.kind = hedgr::model::command::command_kind::save_label;
            make.label_row.id = label_key_for(input, name);
            make.label_row.name = name;
            make.label_row.colour = hedgr::model::domain::label_colour::grey;
            make.label_row.severity = hedgr::model::domain::label_severity::none;
            input.commands.push(std::move(make));
        }

        hedgr::model::command::command attach;
        attach.kind = hedgr::model::command::command_kind::attach_label;
        attach.labelling_row.owner = owner;
        attach.labelling_row.owner_id = std::string{owner_id};
        attach.labelling_row.label_id = label_key_for(input, name);
        input.commands.push(std::move(attach));
    }

    for (const auto* row : current) {
        if (std::find(wanted.begin(), wanted.end(), row->name) != wanted.end())
            continue;
        hedgr::model::command::command detach;
        detach.kind = hedgr::model::command::command_kind::detach_label;
        detach.labelling_row.owner = owner;
        detach.labelling_row.owner_id = std::string{owner_id};
        detach.labelling_row.label_id = row->id;
        input.commands.push(std::move(detach));
    }
}

/**
 * @brief The live folders of one tree belonging to one owner.
 *
 * The owner is part of the filter, not an afterthought. Every instance has its
 * own accounts tree, so an accounts tree filtered on the tree alone returns
 * every instance's root and the browser draws them all: one instance's window
 * then shows one Accounts node per instance in the store, which is where the
 * wall of empty account trees came from.
 *
 * The instances tree has no owner, so its callers pass nothing and match on the
 * empty owner every one of its folders carries.
 */
[[nodiscard]] inline std::vector<widget::tree_folder> tree_folders(
    const render_input& input, hedgr::model::domain::folder_tree tree,
    std::string_view owner_id = {}) {
    std::vector<widget::tree_folder> folders;
    const auto& layout = input.folders.layout();
    const auto rows = input.folders.rows();

    for (std::uint32_t index = 0; index < layout.capacity(); ++index) {
        if (!layout.is_alive(index))
            continue;
        const auto& row = rows[index];
        if (row.tree != tree || row.owner_id != owner_id)
            continue;
        folders.push_back(widget::tree_folder{row.id, row.parent_id, row.name,
            row.parent_id.empty()});
    }
    return folders;
}

/**
 * @brief Whether the folder table would accept a move of @p id under @p to.
 *
 * A root is refused because it is what its tree is anchored by. A cycle is
 * refused here as well as in the table, because asking first is what keeps the
 * refusal ahead of the attempt: the drop is declined and nothing is written,
 * rather than written and then rolled back.
 *
 * The two ends are also required to be in the same tree, so a folder of one
 * tree cannot be dragged into the other's.
 */
[[nodiscard]] inline bool folder_move_allowed(const render_input& input,
    std::string_view id, std::string_view to) {
    const auto moving = input.folders.find(id);
    const auto target = input.folders.find(to);
    if (!moving || !target)
        return false;

    const auto* from = input.folders.get(*moving);
    const auto* into = input.folders.get(*target);
    if (from->parent_id.empty())
        return false;
    if (from->tree != into->tree || from->owner_id != into->owner_id)
        return false;
    if (from->parent_id == to)
        return false;

    for (const auto& above : input.folders.ancestors_of(to)) {
        if (above == id)
            return false;
    }
    return true;
}

}

#endif
