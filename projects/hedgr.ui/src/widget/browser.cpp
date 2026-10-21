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
#include "hedgr.ui/theme/theme.hpp"
#include "hedgr.ui/widget/capture.hpp"
#include "hedgr.ui/widget/controls.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace widget = hedgr::ui::widget;
namespace icon = hedgr::ui::icon;
namespace theme = hedgr::ui::theme;
using hedgr::ui::render::render_input;
using hedgr::ui::render::window_ui_state;

/**
 * @brief One group of the rail: a folder and the entries that name it.
 *
 * The tree the model holds is a general one and the rail shows one level of it.
 * A folder is a heading at a fixed indent rather than a node inside a node, and
 * an entry sits under the heading of the folder it names. A folder nested under
 * another is therefore drawn as a heading beside it, which is what keeps the
 * surface one level while every row the store holds is still drawn.
 *
 * The root is a group too, because an entity created with the root selected
 * names it as its parent, and it is drawn only when something sits directly
 * under it.
 */
struct rail_group final {
    /** @brief Index into the model's folder list. */
    std::size_t folder = 0;

    /** @brief Whether this group is the tree's root. */
    bool is_root = false;

    /** @brief Indices into the model's leaf list, in the order the model gives. */
    std::vector<std::size_t> leaves;
};

/**
 * @brief Flattens the model into the rail's groups.
 *
 * Every folder but the root is one group, in the order the host gave them, and
 * every leaf is placed in the group its parent names. A leaf whose parent the
 * model does not hold lands in the root's group rather than being dropped: a
 * row the store holds must not become invisible because the host assembled the
 * tree in an unexpected order.
 *
 * The root's group is appended last and taken off again when nothing names it,
 * so the tree's own name is never a heading over an empty list.
 */
[[nodiscard]] std::vector<rail_group>
build_rail_groups(const widget::browser_model& model) {
    std::vector<rail_group> groups;

    std::size_t root = model.folders.size();
    for (std::size_t index = 0; index < model.folders.size(); ++index) {
        if (model.folders[index].is_root) {
            root = index;
            continue;
        }
        groups.push_back(rail_group{index, false, {}});
    }

    const std::size_t root_group = groups.size();
    if (root < model.folders.size())
        groups.push_back(rail_group{root, true, {}});

    const auto bucket_of = [&](std::string_view parent_id) -> std::size_t {
        for (std::size_t index = 0; index < groups.size(); ++index) {
            if (model.folders[groups[index].folder].id == parent_id)
                return index;
        }
        return root_group;
    };

    for (std::size_t index = 0; index < model.leaves.size(); ++index) {
        const auto bucket = bucket_of(model.leaves[index].parent_id);
        if (bucket < groups.size())
            groups[bucket].leaves.push_back(index);
    }

    if (root < model.folders.size() && groups[root_group].leaves.empty())
        groups.pop_back();

    return groups;
}

/**
 * @brief The height of one rail entry.
 *
 * A rail row is roomier than a control, which is the reference's own measure:
 * about two text heights rather than a hair over one. Measured from the font so
 * a theme that installs a larger face gets a row that still holds it.
 */
[[nodiscard]] float rail_entry_height() {
    constexpr float rail_row_extra = 6.0f;
    return ImGui::GetFrameHeight() + rail_row_extra;
}

/** @brief The height of a rail group's heading. */
[[nodiscard]] float rail_heading_height() {
    return ImGui::GetTextLineHeight() + 12.0f;
}

/** @brief @p text in capitals, for a group heading. */
[[nodiscard]] std::string rail_case(std::string_view text) {
    std::string result{text};
    for (char& letter : result)
        letter = static_cast<char>(
            std::toupper(static_cast<unsigned char>(letter)));
    return result;
}

/**
 * @brief Writes a group heading on a line of its own.
 *
 * Shared by the rail and the toolbar, because the reference uses one treatment
 * for both: the name of a set of rows, dimmed and in capitals, so a reader
 * learns one shape for "the rows under this belong together".
 */
void draw_group_label(std::string_view text) {
    ImGui::Spacing();
    ImGui::TextColored(
        ImGui::ColorConvertU32ToFloat4(theme::rail_heading_colour), "%s",
        rail_case(text).c_str());
    ImGui::Spacing();
}

/**
 * @brief The bar and the box the reference draws on its chosen row.
 *
 * Drawn after the row itself, because the row's own fill is ImGui's and its
 * text has already been submitted: a bar along the left edge and a one pixel
 * box around the row cover neither. The bar is inset from the row's edge and
 * stops short of its top and bottom, which is what makes it read as a mark
 * beside the row rather than as the row's own border.
 */
void draw_selection_mark(const ImVec2& min, const ImVec2& max) {
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    constexpr float inset = 4.0f;
    constexpr float bar_width = 3.0f;
    constexpr float rounding = 6.0f;

    draw->AddRect(min, max, theme::rail_selection_border, rounding, 0, 1.0f);
    draw->AddRectFilled(ImVec2(min.x + inset, min.y + inset),
        ImVec2(min.x + inset + bar_width, max.y - inset),
        theme::rail_selection_bar, bar_width * 0.5f);
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
    const widget::dragged_row& dragged, const drop_target& target,
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
 * @brief The padlock, which is the whole of the store's state in the strip.
 *
 * An icon, the word for the action and a tooltip. The colour is the state --
 * amber for a store that is shut and for one with no password yet, grey for an
 * open one, red for a refusal -- and the word is the action the click takes, so
 * the row says what it does as well as what it is. The sentence that names the
 * state and any reason the last attempt was refused lives in the tooltip,
 * because a toolbar row holds a word and not a paragraph.
 *
 * One control with two jobs, chosen by the state it is showing. An open store
 * is locked, and anything else asks for the password. That is what makes this
 * the only lock control in the interface.
 *
 * It sits at the end of the row rather than among the buttons that change a
 * row, because it is not an action on the thing being browsed: it is the state
 * of the store everything on the window is read from.
 */
void draw_lock_button(hedgr::ui::render::lock_control& lock) {
    using hedgr::model::domain::store_lock;

    const char* state_text = "The store is unlocked";
    const char* action = "click to lock it";
    const char* word = "Lock";
    switch (lock.lock) {
    case store_lock::absent:
        state_text = "There is no master password yet";
        action = "click to create one";
        word = "Create";
        break;
    case store_lock::locked:
        state_text = "The store is locked";
        action = "click to enter the master password";
        word = "Unlock";
        break;
    case store_lock::unlocked:
        break;
    case store_lock::failed:
        state_text = "The store could not be unlocked";
        action = "click to try again";
        word = "Retry";
        break;
    }

    /*
     * The reason travels with the state rather than on an icon of its own. When
     * the last attempt was refused the reason is the whole of what the operator
     * wants, and the tooltip is the only place with room for it.
     */
    const std::string tooltip = lock.lock == store_lock::failed
            && !lock.reason.empty()
        ? std::format("{}. {}. {}", state_text, action, lock.reason)
        : std::format("{}. {}.", state_text, action);

    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::ColorConvertU32ToFloat4(theme::store_lock_colour(lock.lock)));
    const bool clicked = widget::rail_button("lock-control",
        theme::store_lock_icon(lock.lock), word, tooltip.c_str());
    ImGui::PopStyleColor();

    if (!clicked)
        return;

    if (lock.lock == store_lock::unlocked)
        lock.close_requested = true;
    else
        lock.open_requested = true;
}

/**
 * @brief The toolbar: every action the browser offers, as a rail of words.
 *
 * The operator asked for this strip to carry its controls as an icon and a word
 * rather than as bare glyphs, which reverses the interface's rule that a button
 * carries no text label. That reversal is this strip alone and it is deliberate:
 * the rule stands everywhere else. The tooltips are kept as well, so a reader
 * who knows an action by its glyph still has the glyph and a reader who does not
 * is given the word.
 *
 * Which controls appear is the host's choice, because the two browsers offer
 * different sets: only the Instances window has probes to run, and only the
 * Accounts window has a session to sign in. A control for an action a browser
 * cannot take would be a lie told in a tooltip.
 */
void draw_toolbar(const widget::browser_model& model,
    window_ui_state& window, const render_input& input) {
    widget::browser_state& state = window.browser;
    namespace icon = hedgr::ui::icon;
    using hedgr::model::command::command;
    using hedgr::model::command::command_kind;

    const bool writable = model.can_write && store_is_open(input);
    const bool have_folder = !state.selected_folder.empty();
    const bool have_leaf = !state.selected_leaf.empty();

    const auto folder_index = have_folder
        ? folder_index_of(model, state.selected_folder)
        : std::optional<std::size_t>{};
    const bool folder_is_root = folder_index
        && model.folders[*folder_index].is_root;
    const bool folder_removable = folder_index && !folder_is_root;

    draw_group_label("Manage");

    if (model.create_leaf != nullptr) {
        const bool allowed = writable && have_folder;
        if (widget::rail_button("add-leaf", icon::add, "Add",
                "Add an entry to the selected folder", allowed)) {
            model.create_leaf(window, state.selected_folder);
        }
    }

    /*
     * A folder is created inside the selection, root included, which is one of
     * the two things root is for. This control only decides where; the name is
     * asked for in the form.
     */
    if (widget::rail_button("add-folder", icon::folder, "Folder",
            "Create a folder in the selected folder",
            writable && have_folder)) {
        state.form = widget::form_kind::folder;
        state.form_folder = state.selected_folder;
        state.form_name.fill('\0');
    }

    const bool edit_pressed = widget::rail_button("edit", icon::edit, "Edit",
        "Edit the selected entry", writable && have_leaf);
    {
        /*
         * The edit control offers itself to a capture by name, so a script can
         * put the pane into edit mode the way a person does rather than through
         * a hook beside it. The rectangle is the control's own, read here where
         * ImGui's geometry for it is still current.
         */
        widget::capture_element element;
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        element.min_x = item_min.x;
        element.min_y = item_min.y;
        element.max_x = item_max.x;
        element.max_y = item_max.y;
        element.label_min_x = item_min.x;
        element.label = "toolbar";
        widget::observe_capture_element("edit", element);
    }
    if (edit_pressed && model.edit_leaf != nullptr)
        model.edit_leaf(window, state.selected_leaf);

    if (model.can_duplicate) {
        if (widget::rail_button("duplicate", icon::duplicate, "Copy",
                "Copy the selected entry", writable && have_leaf)
            && model.duplicate_leaf != nullptr) {
            model.duplicate_leaf(window, state.selected_leaf);
        }
    }

    {
        /*
         * Removal is offered for a leaf and for a folder that is not the root.
         * The root is not offered it at all -- see @c affordances_for -- and
         * that is why the control is disabled rather than absent here: the
         * strip is a fixed column, and a control that vanished as the selection
         * moved would make the column jump under the cursor.
         */
        const bool allowed = writable && (have_leaf || folder_removable);
        if (widget::rail_button("remove", icon::remove, "Remove",
                "Remove the selected entry", allowed)) {
            state.confirm_remove_leaf = have_leaf;
            state.confirm_remove_folder = !have_leaf && folder_removable;
        }
    }

    if (model.can_refresh) {
        if (widget::rail_button("probe-one", icon::refresh, "Probe",
                "Probe the selected instance now", writable && have_leaf)) {
            command request;
            request.kind = command_kind::probe_instance;
            request.instance_id = state.selected_leaf;
            input.commands.push(std::move(request));
        }
    }

    /*
     * The host's own actions, after the browser's and under a heading of their
     * own: the browser's half of the column is what the tree is for, and the
     * host's half is what the whole window is for. The host draws its own share,
     * which is also where the words for its controls are chosen.
     */
    if (model.window_actions != nullptr) {
        draw_group_label("Actions");
        model.window_actions(input, window);
    }

    /*
     * The padlock is the one control here that is not about the thing being
     * browsed, so it is pushed to the foot of the strip and away from the
     * actions that change a row. The strip is as tall as the tree beside it, so
     * the bottom is where the far edge of the old row used to be.
     */
    if (model.lock != nullptr) {
        const float foot = ImGui::GetWindowHeight()
            - rail_entry_height() - ImGui::GetStyle().WindowPadding.y;
        ImGui::SetCursorPosY(foot);
        draw_lock_button(*model.lock);
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
 * @brief One row of the rail: a group's heading or a leaf beneath it.
 *
 * One construction for both kinds, because a heading and an entry are the same
 * row to the pointer and to the eye: a full width item that can be chosen,
 * dragged and dropped on. Two constructions would be two descriptions of what a
 * row is, and the one that drifts is the one whose rows stop being items ImGui
 * knows about.
 *
 * The glyph and the name are painted into the draw list rather than submitted
 * as the selectable's label, because the glyph carries the entry's probe state
 * and the name does not. An entry whose whole row took the state's colour would
 * shout, and the reference keeps an entry's icon and its word apart in colour.
 *
 * A heading is a row rather than a label, because in this tree a folder is a
 * container an operator selects and adds into, and the toolbar's create
 * controls act on the selection. The reference's headings are not selectable
 * because its entries are not grouped by anything an operator made.
 *
 * @param element When given, the row as a capture sees it, filled here because
 * this is where ImGui's own geometry for the row is still current.
 */
void draw_rail_row(const widget::browser_model& model, widget::node_kind kind,
    std::size_t index, window_ui_state& window, const render_input& input) {
    namespace icon = hedgr::ui::icon;

    widget::browser_state& state = window.browser;
    const bool heading = kind == widget::node_kind::folder;
    const std::string_view id = heading
        ? std::string_view{model.folders[index].id}
        : std::string_view{model.leaves[index].id};
    const std::string_view name = heading
        ? std::string_view{model.folders[index].name}
        : std::string_view{model.leaves[index].name};

    const bool selected = heading ? state.selected_folder == id
                                  : state.selected_leaf == id;
    const bool root = heading && model.folders[index].is_root;
    const auto affordances = widget::affordances_for(kind, root);
    const bool draggable = affordances.move && store_is_open(input);

    const float height = heading ? rail_heading_height() : rail_entry_height();
    const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::PushID(id.data());
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, 6.0f);

    /*
     * ImGui reaches for the hover colour whether or not the row is chosen, so a
     * chosen row keeps its wash by having the wash pushed over both. The
     * unselected hover is the theme's own neutral, which is what tells a row the
     * pointer crossed from the row that is chosen.
     */
    const ImVec4 wash = ImGui::GetStyleColorVec4(ImGuiCol_Header);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, wash);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, wash);
    }
    const bool clicked = ImGui::Selectable("##row", selected,
        ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, height));
    if (selected)
        ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* const draw = ImGui::GetWindowDrawList();

    const float left = min.x + style.WindowPadding.x;
    /*
     * The text is centred in the rectangle the row is drawn as, which is not
     * the height it was asked for. A selectable widens its hit box by half an
     * item spacing on each side to close the gap to its neighbour, so the
     * highlight is six pixels taller than the row and centring on the requested
     * height would leave the word a few pixels above the middle of the box a
     * reader sees. The heading and the entry are the same row here and both
     * take this.
     */
    const float text_y = min.y
        + ((max.y - min.y) - ImGui::GetTextLineHeight()) * 0.5f;

    if (heading) {
        const std::string title = rail_case(name);
        draw->AddText(ImVec2(left, text_y),
            selected ? ImGui::GetColorU32(ImGuiCol_Text)
                     : theme::rail_heading_colour,
            title.c_str());
    } else {
        const auto& leaf = model.leaves[index];
        const char* glyph = leaf.icon != nullptr ? leaf.icon : icon::instance;
        const ImU32 glyph_colour = leaf.icon_colour != 0
            ? leaf.icon_colour
            : ImGui::GetColorU32(ImGuiCol_Text);
        draw->AddText(ImVec2(left, text_y), glyph_colour, glyph);

        const float word_x =
            left + ImGui::CalcTextSize(glyph).x + style.ItemInnerSpacing.x;
        draw->AddText(
            ImVec2(word_x, text_y), ImGui::GetColorU32(ImGuiCol_Text),
            leaf.name.c_str());
    }

    if (selected)
        draw_selection_mark(min, max);

    if (root) {
        ImGui::SetItemTooltip("%s",
            "The root of this tree. New folders and entries are created here, "
            "and it cannot be renamed, moved or removed.");
    } else if (!heading) {
        const auto& leaf = model.leaves[index];
        if (!leaf.icon_tooltip.empty())
            ImGui::SetItemTooltip("%s", leaf.icon_tooltip.c_str());
    }

    widget::capture_element element;
    element.min_x = min.x;
    element.min_y = min.y;
    element.max_x = max.x;
    element.max_y = max.y;
    element.label_min_x = left;
    element.label.clear();
    widget::observe_capture_element(name, element);

    /*
     * A click on the row chooses it. Opening a group is not a gesture here,
     * because the rail has no closed groups: every heading is followed by its
     * own entries on the frame it is drawn.
     */
    if (clicked) {
        if (heading) {
            state.selected_folder = std::string{id};
            state.selected_leaf.clear();
        } else {
            state.selected_leaf = std::string{id};
            state.selected_folder.clear();
        }
    }

    /*
     * A second click on an entry opens it, which is the gesture every list in
     * every desktop program uses. It is asked of ImGui rather than counted
     * here, because ImGui already knows the interval two clicks have to fall
     * inside to count as a double.
     */
    if (!heading && clicked && model.activate_leaf != nullptr
        && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        model.activate_leaf(input, window, id);

    if (draggable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        const std::string payload = widget::encode_drag_payload(kind, id);
        ImGui::SetDragDropPayload(drag_payload, payload.data(), payload.size());
        ImGui::TextUnformatted(std::string{name}.c_str());
        ImGui::EndDragDropSource();
    }

    if (heading && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(drag_payload);
            payload != nullptr) {
            if (const auto dragged = widget::decode_drag_payload(
                    payload->Data, payload->DataSize))
                accept_drop(model, *dragged, drop_target{std::string{id}}, input);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PopID();
}

/**
 * @brief The list's rows, as indices into the model's leaves.
 *
 * A leaf selection lists that one leaf. A folder selection lists the entries
 * that name it, because the rail shows one level and a heading stands for its
 * own entries. With nothing selected, every entry the filter let through is
 * listed, which is what an untouched tree reports.
 *
 * The list reads the filter the rail does, so a search that narrows the rail to
 * one heading narrows the list to that heading's entries rather than to a
 * different set.
 */
[[nodiscard]] std::vector<std::size_t> visible_leaf_indices(
    const widget::browser_model& model, const widget::browser_state& state) {
    const std::string_view needle{state.filter.data()};
    const auto matches = [&](const widget::tree_leaf& leaf) {
        return needle.empty()
            || widget::filter_matches_any({leaf.name, leaf.search_text}, needle);
    };

    std::vector<std::size_t> kept;
    if (!state.selected_leaf.empty()) {
        for (std::size_t index = 0; index < model.leaves.size(); ++index) {
            if (model.leaves[index].id == state.selected_leaf
                && matches(model.leaves[index]))
                kept.push_back(index);
        }
        return kept;
    }

    for (std::size_t index = 0; index < model.leaves.size(); ++index) {
        const auto& leaf = model.leaves[index];
        if (!matches(leaf))
            continue;
        if (!state.selected_folder.empty()
            && leaf.parent_id != state.selected_folder)
            continue;
        kept.push_back(index);
    }
    return kept;
}

/**
 * @brief Whether the right hand pane has to keep the store's rows back.
 *
 * A store that is shut or still unlocking has nothing for either pane to
 * report, so both ask this one question rather than repeating the pair of
 * conditions and drifting apart on the second.
 */
[[nodiscard]] bool store_is_shut(const render_input& input) noexcept {
    return !store_is_open(input) || input.unlocking;
}

/**
 * @brief How far above the pane's own background the shut padlock is lifted.
 *
 * The pane is a child whose background is transparent, so what shows through is
 * the window's background, and the padlock is derived from that colour rather
 * than from a grey of its own. It is a watermark: the point of it is that a
 * reader notices the pane is shut, not that they read a shape, so it sits one
 * small step above the background and no further. A larger step was tried first
 * and a field of colour this size read as the brightest thing on the desk.
 *
 * Derived rather than stated so that a change to the window background carries
 * the padlock with it, instead of leaving two literals that happened to agree
 * on the day they were written.
 */
constexpr float shut_pane_lift = 1.9f;

/**
 * @brief The shut store's pane: a padlock and no words.
 *
 * One drawing for the two panes that can stand on the right, so a locked store
 * looks the same whether the pane behind it would have been a list or a details
 * view. The host's callbacks answer with blank text until the catalogue can be
 * read, so a table or a form submitted anyway would be a shape over rows that
 * are empty but not absent, which reads as a rendering fault rather than as a
 * state.
 *
 * The state already has a home on the toolbar's own padlock, so a sentence here
 * would be the third place the same fact is said. A large grey shape in the
 * middle of the pane is read at a glance and says the one thing that matters,
 * which is that the pane is shut rather than broken.
 *
 * The padlock is built from draw primitives rather than taken from the icon
 * set. An icon is a bitmap cell packed into the atlas at the base text size,
 * and a custom rectangle carries no outline for the font to rescale, so asking
 * for the same glyph at several times that size draws the atlas's
 * missing-glyph mark instead of the shape. A body and a shackle measured from
 * the pane take whatever size the pane has and cannot go missing.
 */
void draw_shut_store_pane() {
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    /*
     * A fraction of the shorter side, so one drawing fills the pane whatever
     * shape the window has. The floor keeps it a shape on a pane squeezed small
     * and the ceiling stops it swallowing a pane with room to spare.
     */
    const float extent = std::min(room.x, room.y);
    const float size = std::clamp(extent * 0.28f, 48.0f, 260.0f);

    /*
     * The window's own background, lifted by one small step. The style is read
     * rather than a colour named here, so the padlock follows the theme the way
     * the words around it do, and it stays a watermark on a theme whose
     * background is not the dark one this was written against.
     */
    ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    tint.x *= shut_pane_lift;
    tint.y *= shut_pane_lift;
    tint.z *= shut_pane_lift;
    const ImU32 colour = ImGui::ColorConvertFloat4ToU32(tint);

    const float left = origin.x + (room.x - size) * 0.5f;
    const float top = origin.y + (room.y - size) * 0.5f;

    /*
     * The body takes the bottom of a square of @p size and the shackle stands
     * on its top edge, narrower than the body so the two are told apart. The
     * shackle is a stroked arc so it encloses a hole, which is what separates a
     * padlock from a bag.
     */
    const float body_height = size * 0.62f;
    const float body_top = top + size - body_height;
    draw->AddRectFilled(ImVec2(left, body_top),
        ImVec2(left + size, body_top + body_height), colour, size * 0.09f,
        ImDrawFlags_RoundCornersAll);

    const float shackle_radius = size * 0.30f;
    const float shackle_thickness = size * 0.11f;
    const ImVec2 shackle_centre(left + size * 0.5f, body_top);
    constexpr float half_turn = std::numbers::pi_v<float>;
    draw->PathArcTo(shackle_centre, shackle_radius, half_turn,
        half_turn * 2.0f, 32);
    draw->PathStroke(colour, ImDrawFlags_None, shackle_thickness);

    /*
     * The keyhole is a darker shade of the body rather than a hole through to
     * the pane behind it, because the pane is drawn over a photograph and a
     * hole painted one flat colour would read as a patch. It is the second cue
     * that says padlock, after the shackle.
     */
    ImVec4 groove = tint;
    groove.x *= 0.45f;
    groove.y *= 0.45f;
    groove.z *= 0.45f;
    const ImU32 groove_colour = ImGui::ColorConvertFloat4ToU32(groove);
    const ImVec2 key_centre(left + size * 0.5f, body_top + body_height * 0.40f);
    draw->AddCircleFilled(key_centre, size * 0.075f, groove_colour, 24);
    draw->AddTriangleFilled(
        ImVec2(key_centre.x - size * 0.05f, key_centre.y),
        ImVec2(key_centre.x + size * 0.05f, key_centre.y),
        ImVec2(key_centre.x, key_centre.y + body_height * 0.36f),
        groove_colour);
}

/** @brief The leaf with @p id, or nothing when the model does not hold one. */
[[nodiscard]] const widget::tree_leaf*
leaf_of(const widget::browser_model& model, std::string_view id) {
    for (const auto& leaf : model.leaves) {
        if (leaf.id == id)
            return &leaf;
    }
    return nullptr;
}

/**
 * @brief The details pane, where a host asked for one.
 *
 * The list's place, taken by the host's own view of one entry. The pane is
 * sized to the space it is given and scrolls when the host draws more than
 * fits. It used to clip instead, on the reasoning that a pane which scrolls
 * hides things, and that reasoning broke on the card wall: at 1920x1080 a
 * selected instance is taller than the pane, so the probes and the failure
 * Detail sat below the fold with no way to reach them. A bar the operator can
 * drag, and the wheel over the pane, are what make every field reachable.
 *
 * A leaf selection is what the pane reports on, which is the rule the list
 * already followed, so a folder selection and an empty one read the same way.
 * The sentence for that case belongs to the browser rather than to the host,
 * because the browser is what knows whether anything is selected and every host
 * should say the same thing.
 */
void draw_details_pane(const widget::browser_model& model,
    window_ui_state& window, const render_input& input, float width,
    float height) {
    widget::browser_state& state = window.browser;

    /*
     * The child is given the exact rectangle the layout computed and is left
     * with its own scrolling, so a host that draws more than fits raises the
     * theme's bar at the pane's right edge and answers the wheel over its own
     * body. The pane is bordered rather than sized to the content, so the bar
     * belongs to the pane and not to the window around it, and a card is laid
     * out inside the room the bar leaves rather than under it.
     */
    if (!ImGui::BeginChild("##browser-details", ImVec2(width, height),
            ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }

    if (store_is_shut(input)) {
        draw_shut_store_pane();
        ImGui::EndChild();
        return;
    }

    const widget::tree_leaf* selected = leaf_of(model, state.selected_leaf);
    if (selected == nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted("Select an entry to see its details.");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    model.draw_details(*selected, input, window);
    ImGui::EndChild();
}

/**
 * @brief The list, on the right.
 *
 * A table whose columns the host declares, so the Instances window and the
 * Accounts window show different things without the browser knowing what either
 * of them is looking at.
 */
void draw_list(const widget::browser_model& model, window_ui_state& window,
    const render_input& input, float width, float height) {
    widget::browser_state& state = window.browser;

    if (!ImGui::BeginChild("##browser-list", ImVec2(width, height),
            ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }

    if (store_is_shut(input)) {
        draw_shut_store_pane();
        ImGui::EndChild();
        return;
    }

    const auto kept = visible_leaf_indices(model, state);
    if (kept.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(state.filter[0] != '\0'
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

            /*
             * One selectable across the whole row, submitted in the first
             * column and told to span the rest. Without it a list row was a
             * handful of text items, and text is not a row: clicking one
             * selected nothing and clicking it twice opened nothing, which is
             * what the operator reported.
             *
             * AllowOverlap is what makes the spanned selectable the thing the
             * pointer hits rather than the cell texts drawn over it, and
             * AllowDoubleClick is what lets the same item answer the second
             * click. Both are the pair ImGui documents for selecting a row.
             */
            ImGui::TableSetColumnIndex(0);
            const ImVec2 row_top = ImGui::GetCursorScreenPos();
            const bool selected = state.selected_leaf == leaf.id;
            /*
             * The chosen row keeps the reference's navy wash while the pointer
             * is over it. ImGui reaches for the hover colour whether or not the
             * row is chosen, so the wash is pushed over it for this row alone;
             * an unchosen row keeps the theme's neutral hover.
             */
            const ImVec4 wash = ImGui::GetStyleColorVec4(ImGuiCol_Header);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, wash);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, wash);
            }
            const bool clicked = ImGui::Selectable("##row", selected,
                ImGuiSelectableFlags_SpanAllColumns
                    | ImGuiSelectableFlags_AllowOverlap
                    | ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(0.0f, ImGui::GetFrameHeight()));
            if (selected)
                ImGui::PopStyleColor(2);

            /*
             * The bar and the box are the list's half of the rail's cue, so a
             * chosen row reads the same on either surface. They are drawn before
             * the cells because the cells are text and the mark is behind it.
             */
            if (selected) {
                draw_selection_mark(
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            }

            /*
             * The selectable advances the cursor past itself, and the cells are
             * drawn over it rather than under it, so the first cell's cursor is
             * put back where the row began. Without this the name was drawn a
             * line below the columns beside it, which reads as a row with a
             * blank name.
             */
            ImGui::TableSetColumnIndex(0);
            ImGui::SetCursorScreenPos(row_top);

            /*
             * Recorded here rather than after the cells, because this is where
             * ImGui's own rectangle for the row is still the current item's.
             * The list carries the qualifier "list" so a script can name a row
             * in the list rather than the row of the same name in the tree:
             * both are on screen, and both are called the same thing.
             */
            widget::capture_element element;
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            element.min_x = item_min.x;
            element.min_y = item_min.y;
            element.max_x = item_max.x;
            element.max_y = item_max.y;
            element.label_min_x = item_min.x;
            element.label = "list";
            widget::observe_capture_element(leaf.name, element);

            if (clicked) {
                state.selected_leaf = leaf.id;
                state.selected_folder.clear();
            }

            if (clicked && model.activate_leaf != nullptr
                && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                model.activate_leaf(input, window, leaf.id);

            for (int column = 0; column < columns; ++column) {
                ImGui::TableSetColumnIndex(column);
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

namespace {

/**
 * @brief The fixed part of a drag payload; the row's id follows it.
 *
 * ImGui copies the payload as bytes and hands the copy back on later frames,
 * after the source's objects are gone, so the payload holds the id's bytes
 * rather than a string that points somewhere else.
 */
struct drag_payload_head final {
    node_kind kind = node_kind::folder;
};
static_assert(std::is_trivially_copyable_v<drag_payload_head>);

}

std::string encode_drag_payload(node_kind kind, std::string_view id) {
    const drag_payload_head head{kind};
    std::string bytes(sizeof(head), '\0');
    std::memcpy(bytes.data(), &head, sizeof(head));
    bytes.append(id);
    return bytes;
}

std::optional<dragged_row> decode_drag_payload(const void* data, int size) {
    if (data == nullptr || size < static_cast<int>(sizeof(drag_payload_head)))
        return std::nullopt;
    const auto* bytes = static_cast<const char*>(data);
    drag_payload_head head;
    std::memcpy(&head, bytes, sizeof(head));
    return dragged_row{head.kind,
        std::string{bytes + sizeof(head),
            static_cast<std::size_t>(size) - sizeof(head)}};
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
    const ImGuiStyle& style = ImGui::GetStyle();

    /*
     * The strip is a column of rows carrying an icon and a word, so its width is
     * the longest word the browser itself draws plus the icon and the padding
     * around both. The host draws its own controls into the same strip, so the
     * measurement is taken from the browser's own longest word and given room
     * rather than fitted to every word either side could use. A floor keeps the
     * strip a column rather than a sliver on a window squeezed narrow.
     */
    constexpr float strip_floor = 150.0f;
    const float strip_width = std::max(
        ImGui::CalcTextSize("Remove").x + ImGui::GetFrameHeight()
            + style.FramePadding.x * 2.0f + style.WindowPadding.x * 2.0f
            + style.ItemInnerSpacing.x,
        strip_floor);

    /*
     * The tree and the right hand pane share what the strip does not take, and
     * they divide it with the tree taking a tenth and a half and the pane the
     * rest. That is half the three tenths it was: the tree holds a folder name
     * and an instance name, both short, and the room the left column was
     * keeping was room the details pane wanted for its paths. The longest
     * instance name the store seeds, "production cluster", is about a hundred
     * and fifty pixels with its icon at this font, so the floor under the rail
     * is what keeps a heading and its entries whole on a window squeezed
     * narrow, and the pane is a list or a details view depending on whether the
     * host supplied one, the split being the same either way. The strip's width
     * and the gaps between the three children are subtracted from the available
     * width rather than guessed at, so the three fit the window exactly and the
     * split does not depend on a magic number.
     */
    const float beside =
        std::max(region.x - strip_width - style.ItemSpacing.x, 0.0f);
    const float tree_width = std::max(beside * 0.15f, 190.0f);
    const float list_width =
        std::max(beside - tree_width - style.ItemSpacing.x, 200.0f);
    /*
     * A floor on the height as well as the width. A window squeezed by a
     * layout would otherwise clip the tree to one row and the list to none,
     * and a browser whose own controls have been clipped away is not a browsed
     * browser any more.
     */
    const float body_height = std::max(region.y - 60.0f, 240.0f);

    /*
     * The strip is a child of its own so the controls inside it stack one to a
     * row and the tree, which begins on the same line, is not pushed down by
     * them. Bordered like the tree and the list, so the three read as the three
     * regions of one body.
     */
    if (ImGui::BeginChild("##browser-actions", ImVec2(strip_width, body_height),
            ImGuiChildFlags_Borders)) {
        draw_toolbar(model, window, input);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("##browser-tree",
            ImVec2(tree_width, body_height), ImGuiChildFlags_Borders)) {
        /*
         * The tree drawn as a single level. Every folder is a heading and its
         * own entries follow it, whatever depth the model gave them, because
         * the operator asked for one level of grouping rather than a tree to
         * work open.
         */
        const auto groups = build_rail_groups(model);
        const std::string_view needle{state.filter.data()};
        bool any_visible = false;

        for (const auto& group : groups) {
            const auto& folder = model.folders[group.folder];
            const bool heading_matches =
                needle.empty() || widget::filter_matches(folder.name, needle);

            /*
             * A heading that matches keeps every entry under it, which is what
             * makes searching for a group's name show the group; a heading that
             * does not match keeps only the entries that do.
             */
            std::vector<std::size_t> shown;
            for (const auto index : group.leaves) {
                const auto& leaf = model.leaves[index];
                if (heading_matches
                    || widget::filter_matches_any(
                        {leaf.name, leaf.search_text}, needle)) {
                    shown.push_back(index);
                }
            }
            if (!heading_matches && shown.empty())
                continue;

            /*
             * The message is about the filter rather than about the tree, so
             * the tree counts itself visible as soon as it draws anything: a
             * heading with no matching entry is still a row on screen, and a
             * tree with nothing in it at all is what the list beside it
             * already reports.
             */
            any_visible = true;
            draw_rail_row(
                model, widget::node_kind::folder, group.folder, window, input);
            for (const auto index : shown) {
                draw_rail_row(
                    model, widget::node_kind::leaf, index, window, input);
            }
        }

        if (!any_visible && !needle.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted("Nothing matches the filter.");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (model.draw_details != nullptr)
        draw_details_pane(model, window, input, list_width, body_height);
    else
        draw_list(model, window, input, list_width, body_height);

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
