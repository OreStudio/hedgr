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
#include "hedgr.ui/widget/browser.hpp"

#include "hedgr.model/command/command.hpp"
#include "hedgr.model/domain/store_lock.hpp"
#include "hedgr.ui/icon/icons.hpp"
#include "hedgr.ui/widget/capture.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace widget = hedgr::ui::widget;
namespace icon = hedgr::ui::icon;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/**
 * @brief A folder or a leaf, flattened into one row of the drawn tree.
 *
 * The tree is flattened rather than walked recursively at draw time, because
 * the filter and the drop rules both have to be decided over the whole tree
 * before any of it is drawn, and a flat list with a depth is the shape that
 * lets one pass do that.
 */
struct tree_row final {
    /** @brief Whether this row is a folder or a leaf. */
    widget::node_kind kind = widget::node_kind::folder;

    /** @brief Index into the model's folder or leaf list, by @c kind. */
    std::size_t index = 0;

    /** @brief Nesting depth, the root at zero. */
    std::size_t depth = 0;

    /** @brief Whether the row survives the current filter. */
    bool visible = true;
};

/** @brief The row's id, whichever list it came from. */
[[nodiscard]] std::string_view id_of(const widget::browser_model& model,
    const tree_row& row) {
    return row.kind == widget::node_kind::folder
        ? std::string_view{model.folders[row.index].id}
        : std::string_view{model.leaves[row.index].id};
}

/** @brief The row's parent id, whichever list it came from. */
[[nodiscard]] std::string_view parent_of(const widget::browser_model& model,
    const tree_row& row) {
    return row.kind == widget::node_kind::folder
        ? std::string_view{model.folders[row.index].parent_id}
        : std::string_view{model.leaves[row.index].parent_id};
}

/** @brief The row's shown text, whichever list it came from. */
[[nodiscard]] std::string_view name_of(const widget::browser_model& model,
    const tree_row& row) {
    return row.kind == widget::node_kind::folder
        ? std::string_view{model.folders[row.index].name}
        : std::string_view{model.leaves[row.index].name};
}

/**
 * @brief Whether the row is the tree's root.
 *
 * Defined ahead of @c glyph_of, which is what a folder's glyph depends on.
 */
[[nodiscard]] bool is_root_of(const widget::browser_model& model,
    const tree_row& row) {
    return row.kind == widget::node_kind::folder
        && model.folders[row.index].is_root;
}

/** @brief The row's icon and its colour, whichever list it came from. */
struct row_glyph final {
    const char* glyph = nullptr;
    std::uint32_t colour = 0;
    const std::string* tooltip = nullptr;
};

/**
 * @brief The row's glyph, its colour and its tooltip.
 *
 * A folder has a glyph chosen from its place in the tree and no colour, which
 * is why this is a struct and not a single pointer: the leaf's colour and its
 * tooltip only exist on a leaf, and reaching for them on a folder would be a
 * read of the wrong list.
 *
 * A leaf's glyph and colour come from the model rather than from constants
 * here, so that the state the host computed is what the tree paints. The list
 * draws no glyph at all: one entry has one icon, in the tree.
 */
[[nodiscard]] row_glyph glyph_of(const widget::browser_model& model,
    const tree_row& row) {
    if (row.kind == widget::node_kind::folder) {
        return row_glyph{
            .glyph = is_root_of(model, row) ? icon::folder_root : icon::folder,
        };
    }
    const auto& leaf = model.leaves[row.index];
    return row_glyph{
        .glyph = leaf.icon != nullptr ? leaf.icon : icon::instance,
        .colour = leaf.icon_colour,
        .tooltip = leaf.icon_tooltip.empty() ? nullptr : &leaf.icon_tooltip,
    };
}

/** @brief The tree's root id, or empty when the model names no root. */
[[nodiscard]] std::string root_id_of(const widget::browser_model& model) {
    for (const auto& folder : model.folders) {
        if (folder.is_root)
            return folder.id;
    }
    return {};
}

/**
 * @brief Whether anything in the model hangs below @p row.
 *
 * The walk needs this to tell a folder that is shut from one that is empty: a
 * shut folder's subtree has to be skipped, and an empty folder's does not
 * exist, so a folder with nothing under it is a leaf wearing a folder's glyph
 * and must not make the walk wait for a depth that never comes.
 */
[[nodiscard]] bool has_children(const widget::browser_model& model,
    const tree_row& row) {
    const std::string_view id = id_of(model, row);
    const auto hangs_below = [&](std::string_view parent) {
        return parent == id;
    };
    return std::any_of(model.folders.begin(), model.folders.end(),
               [&](const widget::tree_folder& folder) {
                   return !folder.is_root && hangs_below(folder.parent_id);
               })
        || std::any_of(model.leaves.begin(), model.leaves.end(),
            [&](const widget::tree_leaf& leaf) {
                return hangs_below(leaf.parent_id);
            });
}

/**
 * @brief Flattens the model's folders and leaves into the order they are drawn.
 *
 * Folders before leaves within one parent, at every level: an operator looks
 * for a container far more often than for the thing inside it, and a stable
 * order is what lets the eye learn the shape of a tree. The root is first
 * because it is what the tree is anchored by.
 *
 * A folder whose parent is missing from the model is drawn at the top rather
 * than dropped: a row the store holds must not become invisible because the
 * host assembled the tree in an unexpected order, and drawing it where it can
 * be seen is a symptom rather than a silence.
 *
 * @return Every row, in draw order.
 */
[[nodiscard]] std::vector<tree_row>
flatten_tree(const widget::browser_model& model) {
    std::vector<tree_row> rows;
    rows.reserve(model.folders.size() + model.leaves.size());

    /* Stable, so siblings keep the order the host gave them. */
    auto ordered = std::vector<std::size_t>(model.folders.size());
    for (std::size_t index = 0; index < ordered.size(); ++index)
        ordered[index] = index;
    std::stable_sort(ordered.begin(), ordered.end(),
        [&](std::size_t left, std::size_t right) {
            const auto& l = model.folders[left];
            const auto& r = model.folders[right];
            if (l.is_root != r.is_root)
                return l.is_root;
            return l.parent_id < r.parent_id;
        });

    const auto append = [&](std::string_view parent, std::size_t depth) {
        const auto walk = [&](auto&& self, std::string_view current,
                              std::size_t level) -> void {
            for (const auto index : ordered) {
                const auto& folder = model.folders[index];
                if (folder.is_root || folder.parent_id != current)
                    continue;
                rows.push_back(tree_row{widget::node_kind::folder, index, level});
                self(self, folder.id, level + 1);
            }
            for (std::size_t index = 0; index < model.leaves.size(); ++index) {
                if (model.leaves[index].parent_id != current)
                    continue;
                rows.push_back(tree_row{widget::node_kind::leaf, index, level});
            }
        };
        walk(walk, parent, depth);
    };

    for (const auto index : ordered) {
        if (!model.folders[index].is_root)
            continue;
        rows.push_back(tree_row{widget::node_kind::folder, index, 0});
        append(model.folders[index].id, 1);
    }

    const auto placed = [&](std::size_t folder_index) {
        return std::any_of(rows.begin(), rows.end(), [&](const tree_row& row) {
            return row.kind == widget::node_kind::folder
                && row.index == folder_index;
        });
    };
    for (const auto index : ordered) {
        if (placed(index))
            continue;
        rows.push_back(tree_row{widget::node_kind::folder, index, 0});
        append(model.folders[index].id, 1);
    }

    return rows;
}

/**
 * @brief Marks which rows survive the filter.
 *
 * A row survives when it matches, and a folder also survives when anything
 * below it matches. The propagation runs backwards over the flattened order,
 * which puts every row after its own children, so one pass is enough: a row's
 * parent is always earlier and is therefore marked after the child that
 * justified it.
 *
 * A leaf's @c search_text is read as well as its name, which is how an instance
 * is found by its address without the address taking a column.
 */
void mark_visible(const widget::browser_model& model,
    std::vector<tree_row>& rows, std::string_view needle, bool& any_visible) {
    any_visible = false;

    auto matched = std::vector<bool>(rows.size(), needle.empty());
    if (!needle.empty()) {
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            matched[index] = row.kind == widget::node_kind::folder
                ? widget::filter_matches(model.folders[row.index].name, needle)
                : widget::filter_matches_any(
                      {model.leaves[row.index].name,
                          model.leaves[row.index].search_text},
                      needle);

            /*
             * A folder survives because something below it matched, and a
             * matching leaf survives on its own. A leaf that does not match is
             * dropped here rather than merely hidden, because the list reads
             * this flag.
             */
            rows[index].visible = row.kind == widget::node_kind::leaf && matched[index];
            any_visible = any_visible || matched[index];
        }
    }

    /*
     * Walked backwards, so every row is visited after its own children: the
     * flattened order puts a parent before its children, and a folder is marked
     * visible once anything below it has been.
     */
    for (std::size_t index = rows.size(); index-- > 0;) {
        if (!matched[index])
            continue;
        rows[index].visible = true;

        const std::string_view parent = parent_of(model, rows[index]);
        for (std::size_t above = index; above-- > 0;) {
            if (rows[above].kind != widget::node_kind::folder)
                continue;
            if (id_of(model, rows[above]) != parent)
                continue;
            rows[above].visible = true;
            break;
        }
    }
}

/** @brief The index of a folder in the model, or nothing. */
[[nodiscard]] std::optional<std::size_t>
folder_index_of(const widget::browser_model& model, std::string_view id) {
    for (std::size_t index = 0; index < model.folders.size(); ++index) {
        if (model.folders[index].id == id)
            return index;
    }
    return std::nullopt;
}

/** @brief A drop target: the root, or one folder row. */
struct drop_target final {
    /** @brief The id the dropped row would gain as its parent. */
    std::string id;
};

/**
 * @brief The ids that may not receive a drop of @p dragged, itself first.
 *
 * A folder's own id and everything below it, which is exactly what a drop must
 * not name: dropping a folder into its own subtree would cut that subtree free
 * of the tree and make the walk that draws it never terminate.
 */
[[nodiscard]] std::vector<std::string>
forbidden_for(const widget::browser_model& model, std::string_view dragged_id) {
    std::vector<std::string> forbidden{std::string{dragged_id}};

    const auto walk = [&](auto&& self, std::string_view parent) -> void {
        for (const auto& folder : model.folders) {
            if (folder.is_root || folder.parent_id != parent)
                continue;
            forbidden.push_back(folder.id);
            self(self, folder.id);
        }
    };
    walk(walk, dragged_id);
    return forbidden;
}

/** @brief The payload type of a dragged tree row. */
constexpr const char* drag_payload = "hedgr.browser.node";

/** @brief What a dragged row carries: what it is and which row it was. */
struct drag_payload_data final {
    /** @brief Whether the dropped row is a folder or a leaf. */
    widget::node_kind kind = widget::node_kind::folder;

    /** @brief The row's id. */
    std::string id;
};

/**
 * @brief Moves a dragged folder to @p target, if the tree and the host allow.
 *
 * Two checks, deliberately separate. The tree refuses a drop into the dragged
 * folder's own subtree, because that is a property of the tree it can see for
 * itself; the host refuses everything else, because only it knows whether the
 * folder table would accept the move. Both refuse before anything is
 * attempted, so a rejected drop leaves the row exactly where it was rather than
 * leaving it somewhere the operator has to drag it back from.
 *
 * A leaf is not re-parented here: only the host knows which table a leaf lives
 * in, and it does that from its own row callback. The tree's own payload still
 * carries a leaf, so a host that supports it sees one consistent event.
 */
void accept_drop(const widget::browser_model& model,
    const drag_payload_data& dragged, const drop_target& target,
    const render_input& input) {
    namespace command = hedgr::model::command;

    if (dragged.kind != widget::node_kind::folder)
        return;

    const auto forbidden = forbidden_for(model, dragged.id);
    if (!widget::can_drop_onto(dragged.id, target.id, forbidden))
        return;

    const auto index = folder_index_of(model, dragged.id);
    if (!index)
        return;
    if (model.folders[*index].parent_id == target.id)
        return;

    if (model.can_reparent != nullptr
        && !model.can_reparent(input, dragged.id, widget::node_kind::folder,
            target.id))
        return;

    /*
     * The row is sent with its parent changed and nothing else, because the
     * host's save command replaces the whole row: an id, a name and an owner
     * are read back from the model rather than invented here.
     */
    command::command request;
    request.kind = command::command_kind::save_folder;
    request.folder_row.id = dragged.id;
    request.folder_row.parent_id = target.id;
    input.commands.push(std::move(request));
}

/** @brief Whether the store is open, which is what every action needs. */
[[nodiscard]] bool store_is_open(const render_input& input) noexcept {
    return input.store_lock == hedgr::model::domain::store_lock::unlocked;
}

/**
 * @brief The toolbar: every action the browser offers, as icons with tooltips.
 *
 * Which of them appear is the host's choice, because the two browsers offer
 * different sets: only the Instances window has probes to run, and only the
 * Accounts window has a session to sign in. A button for an action a browser
 * cannot take would be a lie told in a tooltip.
 */
void draw_toolbar(const widget::browser_model& model,
    window_ui_state& window, const render_input& input) {
    widget::browser_state& state = window.browser;
    namespace icon = hedgr::ui::icon;
    using hedgr::model::command::command;
    using hedgr::model::command::command_kind;

    const bool writable = model.can_write && store_is_open(input);
    const std::string root_id = root_id_of(model);
    const bool have_folder = !state.selected_folder.empty();
    const bool have_leaf = !state.selected_leaf.empty();

    const auto folder_index = have_folder
        ? folder_index_of(model, state.selected_folder)
        : std::optional<std::size_t>{};
    const bool folder_is_root = folder_index
        && model.folders[*folder_index].is_root;
    const bool folder_removable = folder_index && !folder_is_root;

    if (model.create_leaf != nullptr) {
        const bool allowed = writable && have_folder;
        if (widget::icon_button("add-leaf", icon::add,
                "Add an entry to the selected folder", allowed)) {
            model.create_leaf(window, state.selected_folder);
        }
    }

    ImGui::SameLine();
    {
        /*
         * A folder is created inside the selection, root included, which is one
         * of the two things root is for. This button only decides where; the
         * name is asked for in the form.
         */
        if (widget::icon_button("add-folder", icon::folder,
                "Create a folder in the selected folder", writable && have_folder)) {
            state.form = widget::form_kind::folder;
            state.form_folder = state.selected_folder;
            state.form_name.fill('\0');
        }
    }

    /*
     * The host's own actions, in the middle of the strip and after the two
     * "add" controls: the browser's half of the row is what the tree is for,
     * and the host's half is what the whole window is for, which is the more
     * prominent thing and belongs where the eye arrives first among the rest.
     */
    if (model.window_actions != nullptr) {
        /*
         * The browser owns the row and the host contributes to it, rather than
         * the host having to remember to begin by joining it.
         */
        ImGui::SameLine();
        model.window_actions(input, window);
    }

    ImGui::SameLine();
    if (widget::icon_button("edit", icon::edit, "Edit the selected entry",
            writable && have_leaf)
        && model.edit_leaf != nullptr) {
        model.edit_leaf(window, state.selected_leaf);
    }

    if (model.can_duplicate) {
        ImGui::SameLine();
        if (widget::icon_button("duplicate", icon::duplicate,
                "Copy the selected entry", writable && have_leaf)
            && model.duplicate_leaf != nullptr) {
            model.duplicate_leaf(window, state.selected_leaf);
        }
    }

    ImGui::SameLine();
    {
        /*
         * Removal is offered for a leaf and for a folder that is not the root.
         * The root is not offered it at all -- see @c affordances_for -- and
         * that is why the button is disabled rather than absent here: the
         * toolbar is one row, and a button that vanished as the selection moved
         * would make the row jump under the cursor.
         */
        const bool allowed = writable && (have_leaf || folder_removable);
        if (widget::icon_button("remove", icon::remove,
                "Remove the selected entry", allowed)) {
            state.confirm_remove_leaf = have_leaf;
            state.confirm_remove_folder = !have_leaf && folder_removable;
        }
    }

    if (model.can_refresh) {
        ImGui::SameLine();
        if (widget::icon_button("probe-one", icon::refresh,
                "Probe the selected instance now", writable && have_leaf)) {
            command request;
            request.kind = command_kind::probe_instance;
            request.instance_id = state.selected_leaf;
            input.commands.push(std::move(request));
        }
    }
}

/**
 * @brief The folder form: a name, and whether that name is already taken.
 *
 * The only form the browser owns, because it is the only one that collects
 * something no host knows anything about. Everything else a form collects --
 * an address, a principal, a wire format -- belongs to the host.
 */
void draw_folder_form(widget::browser_state& state,
    const widget::browser_model& model, const render_input& input) {
    namespace icon = hedgr::ui::icon;

    const ImVec2 size(420.0f, 150.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    const char* title = "Folder###hedgr-browser-folder";
    ImGui::OpenPopup(title);
    if (!ImGui::BeginPopupModal(title, nullptr, flags))
        return;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##folder-name", "folder name", state.form_name.data(),
        state.form_name.size());

    const std::string_view name{state.form_name.data()};
    const bool sibling_exists = std::any_of(model.folders.begin(),
        model.folders.end(), [&](const widget::tree_folder& folder) {
            return folder.parent_id == state.form_folder && folder.name == name;
        });

    if (name.empty())
        ImGui::TextDisabled("A folder needs a name.");
    else if (sibling_exists)
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f),
            "There is already a folder called %s here.", std::string{name}.c_str());
    else
        ImGui::TextDisabled("It will be created inside %s.", [&] {
            const auto parent = folder_index_of(model, state.form_folder);
            return parent ? model.folders[*parent].name.c_str() : "the root";
        }());

    const bool complete = !name.empty() && !sibling_exists;

    if (!complete)
        ImGui::BeginDisabled();
    const bool saved = ImGui::Button(std::format("{} Save", icon::save).c_str());
    if (!complete)
        ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", "Create the folder");

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Cancel", icon::cancel).c_str())) {
        state.form = widget::form_kind::none;
        state.form_folder.clear();
        state.form_name.fill('\0');
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemTooltip("%s", "Abandon this folder");

    if (saved && complete) {
        hedgr::model::command::command request;
        request.kind = hedgr::model::command::command_kind::save_folder;
        /*
         * The id is derived from where the folder goes and what it is called,
         * which is unique among its siblings by the check above. A serial or a
         * clock reading would be unique too, and would also mean the same
         * folder had a different name every time it was created in a test.
         */
        request.folder_row.id =
            std::format("folder:{}:{}", state.form_folder, name);
        request.folder_row.parent_id = state.form_folder;
        request.folder_row.name = std::string{name};
        input.commands.push(std::move(request));

        state.form = widget::form_kind::none;
        state.form_folder.clear();
        state.form_name.fill('\0');
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

/**
 * @brief One row of the tree, submitted as one ImGui tree node.
 *
 * One construction for both kinds of row. A leaf is a tree node carrying
 * @c ImGuiTreeNodeFlags_Leaf and @c ImGuiTreeNodeFlags_NoTreePushOnOpen: it has
 * no children, so it never pushes an id and never opens, and it is drawn by the
 * same code that draws a folder. Two constructions would be two descriptions of
 * what a row is, and the one that drifts is the one whose rows stop being items
 * that ImGui knows about -- which is exactly what happened when a leaf was a
 * hand-painted label over a selectable.
 *
 * The glyph is the first thing in the label, submitted as ordinary text, so its
 * size, its baseline, its spacing and its hit box are the font's and ImGui's
 * rather than numbers chosen here. That is also what stops a row from being
 * drawn somewhere its hit box is not.
 *
 * The state's colour is pushed around the whole node and colours the name as
 * well as the glyph. The alternative, a label in the ordinary colour and a
 * glyph in the state's, means either two items on one row or text painted into
 * the draw list, and both put the hit box and the pixels in different places.
 * A uniform tree that works beats a two-colour row that does not.
 *
 * Root is a row like any other here. It is a folder with no parent drawn at
 * depth zero, so it shares the tree's indentation, scrolls with the tree and
 * takes a drop the same way; the one thing that separates it is that
 * @c affordances_for gives it no rename, no move and no delete, so it is not
 * draggable and offers no removal.
 *
 * @param opened Set to whether this row's node was pushed, which is what the
 * caller has to close.
 * @param element When given, the item as a capture sees it, filled here because
 * this is where ImGui's own geometry for the row is still current.
 */
void draw_tree_row(const widget::browser_model& model, const tree_row& row,
    widget::browser_state& state, const render_input& input, bool& opened,
    widget::capture_element* element) {
    namespace icon = hedgr::ui::icon;

    const std::string_view id = id_of(model, row);
    const bool root = is_root_of(model, row);
    const bool leaf = row.kind == widget::node_kind::leaf;

    const bool selected = leaf ? state.selected_leaf == id
                               : state.selected_folder == id;
    const auto affordances = widget::affordances_for(row.kind, root);
    const bool draggable = affordances.move && store_is_open(input);

    ImGui::PushID(id.data());

    ImGuiTreeNodeFlags flags = (selected ? ImGuiTreeNodeFlags_Selected : 0)
        | ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (leaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    } else {
        flags |= ImGuiTreeNodeFlags_FramePadding;
    }

    const row_glyph glyph = glyph_of(model, row);
    const std::string_view name = name_of(model, row);
    const std::string label =
        std::format("{}  {}", glyph.glyph, std::string{name});

    /*
     * Root opens the first time it is drawn and its own expander owns the state
     * from then on, so a reader opening Hedgr sees the tree's contents rather
     * than a single closed name, and a reader who shuts it keeps it shut.
     *
     * The id is taken once, before the node is submitted, and reused after it.
     * TreeNodeEx pushes the node's own id while it is open, so an id computed
     * after it is hashed against a different stack and lands in a different
     * slot: the read would then never see what the write stored, and a root
     * that had been shut could never be opened again.
     */
    const ImGuiID open_state = root ? ImGui::GetID("##hedgr-root-open") : 0;
    if (root) {
        ImGui::SetNextItemOpen(
            ImGui::GetStateStorage()->GetInt(open_state, 1) != 0,
            ImGuiCond_Always);
    }
    if (glyph.colour != 0)
        ImGui::PushStyleColor(ImGuiCol_Text, glyph.colour);
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
    if (glyph.colour != 0)
        ImGui::PopStyleColor();

    /*
     * Folders are open when the node says so. A leaf never pushes, so a leaf is
     * always closed: that flag is what keeps "this row has a subtree below it"
     * from being true of a row that has none.
     */
    opened = !leaf && open;

    if (element != nullptr) {
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        /*
         * ImGui's own geometry, read back: an arrow occupies the frame height
         * at the row's start, and the label begins one GetTreeNodeToLabelSpacing
         * past it. Worked out here rather than in the capture, which then needs
         * no opinion about how a tree row is laid out.
         */
        const float arrow_width = leaf ? 0.0f : ImGui::GetFrameHeight();
        element->min_x = item_min.x;
        element->min_y = item_min.y;
        element->max_x = item_max.x;
        element->max_y = item_max.y;
        element->label_min_x =
            item_min.x + arrow_width + ImGui::GetTreeNodeToLabelSpacing();
        element->arrow_width = arrow_width;
        element->open = opened;
        element->label.clear();
    }

    if (root) {
        /*
         * Written only when the operator worked the expander, and only after
         * the node: the first frame reads the default above, and every later
         * frame reads what the last click left.
         */
        if (ImGui::IsItemToggledOpen())
            ImGui::GetStateStorage()->SetInt(open_state, open ? 1 : 0);

        ImGui::SetItemTooltip("%s",
            "The root of this tree. New folders and entries are created here, "
            "and it cannot be renamed, moved or removed.");
    }
    if (glyph.tooltip != nullptr)
        ImGui::SetItemTooltip("%s", glyph.tooltip->c_str());

    /*
     * A click on the row selects it, and a click on the row's own expander does
     * not: opening a folder is not choosing it, which is the whole reason the
     * node is drawn with @c OpenOnArrow. ImGui reports the two separately, so
     * the rule is its answer rather than a comparison of coordinates.
     */
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (leaf) {
            state.selected_leaf = std::string{id};
            state.selected_folder.clear();
        } else {
            state.selected_folder = std::string{id};
            state.selected_leaf.clear();
        }
    }

    if (draggable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        const drag_payload_data payload{row.kind, std::string{id}};
        ImGui::SetDragDropPayload(drag_payload, &payload, sizeof(payload));
        ImGui::TextUnformatted(std::string{name}.c_str());
        ImGui::EndDragDropSource();
    }

    if (!leaf && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(drag_payload);
            payload != nullptr
            && payload->DataSize == static_cast<int>(sizeof(drag_payload_data))) {
            accept_drop(model,
                *static_cast<const drag_payload_data*>(payload->Data),
                drop_target{std::string{id}}, input);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PopID();
}

/**
 * @brief The list's rows, as indices into the model's leaves.
 *
 * A leaf selection lists that one leaf. A folder selection lists every leaf the
 * walk reached under it, filtered, which is what "selecting a folder lists
 * everything beneath it" comes to once the tree is already flattened.
 */
[[nodiscard]] std::vector<std::size_t> visible_leaf_indices(
    const widget::browser_model& model, const std::vector<tree_row>& rows,
    const widget::browser_state& state) {
    std::vector<std::size_t> kept;
    for (const auto& row : rows) {
        if (row.kind != widget::node_kind::leaf || !row.visible)
            continue;
        if (!state.selected_leaf.empty())
            continue;
        kept.push_back(row.index);
    }

    if (!state.selected_leaf.empty()) {
        for (std::size_t index = 0; index < model.leaves.size(); ++index) {
            if (model.leaves[index].id == state.selected_leaf)
                kept.push_back(index);
        }
    }
    return kept;
}

/**
 * @brief The list, on the right.
 *
 * A table whose columns the host declares, so the Instances window and the
 * Accounts window show different things without the browser knowing what either
 * of them is looking at.
 */
void draw_list(const widget::browser_model& model,
    const std::vector<tree_row>& rows, const widget::browser_state& state,
    const render_input& input, float width, float height) {
    namespace icon = hedgr::ui::icon;

    if (!ImGui::BeginChild("##browser-list", ImVec2(width, height),
            ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }

    const auto kept = visible_leaf_indices(model, rows, state);
    if (kept.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool filtering = state.filter[0] != '\0';
        ImGui::TextUnformatted(filtering
                ? "Nothing under this folder matches the filter."
                : "Nothing here yet. Select a folder, or add an entry.");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    const int columns = static_cast<int>(model.columns.size());
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_ScrollY;

    if (columns > 0
        && ImGui::BeginTable("##browser-rows", columns, flags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const auto& column : model.columns) {
            ImGui::TableSetupColumn(column.label,
                column.width == widget::column_width::stretch
                    ? ImGuiTableColumnFlags_WidthStretch
                    : ImGuiTableColumnFlags_WidthFixed);
        }
        ImGui::TableHeadersRow();

        widget::row_cells cells;
        auto row_labels = std::vector<std::vector<widget::label_pill>>{};
        for (const auto index : kept) {
            const auto& leaf = model.leaves[index];
            cells.clear();
            if (model.row_cells != nullptr)
                model.row_cells(leaf, input, cells);
            row_labels.clear();
            if (model.row_labels != nullptr)
                model.row_labels(leaf, input, row_labels);

            ImGui::TableNextRow();
            ImGui::PushID(leaf.id.c_str());
            for (int column = 0; column < columns; ++column) {
                ImGui::TableNextColumn();
                const auto position = static_cast<std::size_t>(column);
                const std::string_view text = position < cells.texts.size()
                    ? std::string_view{cells.texts[position]}
                    : std::string_view{};

                const std::vector<widget::label_pill>* pills =
                    position < row_labels.size() && !row_labels[position].empty()
                    ? &row_labels[position]
                    : nullptr;

                if (pills != nullptr) {
                    /*
                     * The cell's own width, so a narrow column truncates its
                     * labels rather than drawing them over the next column.
                     */
                    static_cast<void>(widget::label_pill_row(*pills,
                        ImGui::GetContentRegionAvail().x));
                } else if (position < cells.colours.size()
                    && cells.colours[position] != 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, cells.colours[position]);
                    ImGui::TextUnformatted(text.data(), text.data() + text.size());
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextUnformatted(text.data(), text.data() + text.size());
                }

                if (position < cells.tooltips.size() && !cells.tooltips[position].empty())
                    ImGui::SetItemTooltip("%s", cells.tooltips[position].c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (columns == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("This browser declares no columns.");
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
}

}

namespace hedgr::ui::widget {

bool filter_matches(std::string_view haystack, std::string_view needle) {
    if (needle.empty())
        return true;
    const auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    return std::search(haystack.begin(), haystack.end(), needle.begin(),
               needle.end(), [&](char a, char b) { return lower(a) == lower(b); })
        != haystack.end();
}

bool filter_matches_any(const std::vector<std::string_view>& haystacks,
    std::string_view needle) {
    if (needle.empty())
        return true;
    return std::any_of(haystacks.begin(), haystacks.end(),
        [&](std::string_view text) { return filter_matches(text, needle); });
}

node_affordances affordances_for(node_kind kind, bool is_root) noexcept {
    node_affordances result;
    if (kind == node_kind::leaf) {
        /*
         * A leaf holds nothing, so the one affordance it loses is the one only
         * a container can offer. Everything else it may do: an instance can be
         * renamed, moved between folders and removed.
         */
        result.create_inside = false;
        return result;
    }
    if (!is_root)
        return result;

    /*
     * Root is where new folders and new entries are made, and it is the one
     * folder that cannot be renamed, moved or removed. Those three are then
     * never drawn at all, rather than drawn disabled: the root is what the tree
     * is anchored by, so there is nothing for them to do, and a permanently
     * grey icon teaches a reader that this application sometimes withholds
     * things, which is a worse lesson than an empty space.
     */
    result.rename = false;
    result.move = false;
    result.remove = false;
    result.create_inside = true;
    return result;
}

bool can_drop_onto(std::string_view dragged_id, std::string_view target_id,
    const std::vector<std::string>& forbidden) {
    if (target_id.empty() || target_id == dragged_id)
        return false;
    return std::none_of(forbidden.begin(), forbidden.end(),
        [&](const std::string& id) { return id == target_id; });
}

void render_browser(const browser_model& model, window_ui_state& window,
    const render_input& input) {
    namespace icon = hedgr::ui::icon;
    browser_state& state = window.browser;

    auto rows = flatten_tree(model);
    bool any_visible = false;
    mark_visible(model, rows, std::string_view{state.filter.data()}, any_visible);

    draw_toolbar(model, window, input);
    ImGui::Separator();

    /*
     * The filter box sits above the tree, because it narrows the tree and the
     * list together: a box beside either of them would look like it narrowed
     * that one alone.
     */
    ImGui::TextUnformatted(icon::search);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##browser-filter", "filter",
        state.filter.data(), state.filter.size());

    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float tree_width = std::max(region.x * 0.34f, 180.0f);
    const float list_width =
        std::max(region.x - tree_width - ImGui::GetStyle().ItemSpacing.x, 200.0f);
    /*
     * A floor on the height as well as the width. A window squeezed by a
     * layout would otherwise clip the tree to one row and the list to none,
     * and a browser whose own controls have been clipped away is not a browsed
     * browser any more.
     */
    const float body_height = std::max(region.y - 60.0f, 240.0f);

    if (ImGui::BeginChild("##browser-tree",
            ImVec2(tree_width, body_height), ImGuiChildFlags_Borders)) {
        /*
         * The message is about the filter rather than about the tree, so it is
         * only said when a filter is what emptied it. A tree with no leaves is
         * not empty in the same way: it is a tree with nothing in it yet, and
         * the list beside it already says so.
         */
        if (!any_visible && state.filter[0] != '\0') {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted("Nothing matches the filter.");
            ImGui::PopStyleColor();
        }
        /*
         * A flattened tree drawn into ImGui's nested tree API. A row is closed
         * when the next visible row is no deeper than it is, which is where its
         * subtree ends in a flat list.
         *
         * The stack holds the rows that pushed an id, so what it holds is
         * exactly what has to be popped. A leaf is a tree node with no children
         * and never pushes, so it is never on the stack and never closed; a
         * folder that is shut never pushes either, and is never closed. The two
         * cannot get out of step, because the one condition that pushes also
         * decides whether a row enters the walk's own bookkeeping.
         *
         * A shut folder's subtree is not submitted at all. ImGui does not hide
         * the children of a shut node -- it never had them, they are separate
         * items -- so a walk that submitted every row would draw them outside
         * the folder that holds them, and collapsing would do nothing. Skipping
         * them is what makes an expander mean something.
         */
        std::vector<std::size_t> open_rows;
        std::size_t shut_depth = 0;
        bool inside_shut_folder = false;

        for (const auto& row : rows) {
            if (!row.visible)
                continue;

            if (inside_shut_folder) {
                if (row.depth > shut_depth)
                    continue;
                inside_shut_folder = false;
            }

            while (!open_rows.empty() && row.depth <= open_rows.back()) {
                ImGui::TreePop();
                open_rows.pop_back();
            }

            bool opened = false;
            widget::capture_element element;
            draw_tree_row(model, row, state, input, opened, &element);
            widget::observe_capture_element(name_of(model, row), element);

            if (opened) {
                open_rows.push_back(row.depth);
            } else if (row.kind == widget::node_kind::folder
                && has_children(model, row)) {
                inside_shut_folder = true;
                shut_depth = row.depth;
            }
        }
        while (!open_rows.empty()) {
            ImGui::TreePop();
            open_rows.pop_back();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    draw_list(model, rows, state, input, list_width, body_height);

    if (model.sign_in_form != nullptr)
        model.sign_in_form(input, window);
    if (model.leaf_form != nullptr && state.form == form_kind::leaf)
        model.leaf_form(input, window);
    if (model.remove_form != nullptr
        && (state.confirm_remove_folder || state.confirm_remove_leaf))
        model.remove_form(input, window);
    if (state.form == form_kind::folder)
        draw_folder_form(state, model, input);
}

}
