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
#include "hedgr.ui/widget/capture.hpp"

#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace widget = hedgr::ui::widget;

/**
 * @brief One gesture, resolved against a drawn row and placed in time.
 *
 * A step becomes this once and is never re-resolved, because the rectangle it
 * points at belongs to one frame. A script that re-resolved every frame would
 * follow the interface as the interface changed under it, which is how a click
 * ends up on the row below the one that was asked for.
 */
/**
 * @brief A gesture's own part, placed in time.
 *
 * An event carries a position or a button and never both: the frame input is
 * built from several events and an event that claimed both would do only the
 * first of them, which is how a press built from the move it follows can be
 * lost.
 */
struct scheduled_event final {
    /** @brief The frame it is driven on. */
    std::uint32_t frame = 0;

    /** @brief Where the pointer is put. */
    float x = 0.0f;
    float y = 0.0f;

    /** @brief Whether the left button goes down or comes up. */
    bool down = false;

    /** @brief Whether this event moves the pointer at all. */
    bool moves = false;
};

/**
 * @brief The run's script, its observations and its timeline.
 *
 * One object rather than a set of globals per concern, so that what a capture
 * knows to do can be followed in one place, and so a process given no script
 * keeps nothing here beyond the flag that says so.
 */
struct capture_run final {
    /** @brief Whether a script was given. */
    bool active = false;

    /** @brief The steps as written. */
    std::vector<widget::capture_step> steps;

    /** @brief What the last frame drew. */
    std::vector<widget::capture_element> elements;

    /**
     * @brief What the frame before this one drew, once this one has begun.
     *
     * A frame is observed as it is drawn, and the gesture of the frame after it
     * has to resolve against a frame that is already finished. Two lists and a
     * swap is what makes that true by construction: a step can never resolve
     * against half of the frame that is still being submitted.
     */
    std::vector<widget::capture_element> last_frame;

    /** @brief The gestures, placed in time. */
    std::vector<scheduled_event> events;

    /**
     * @brief Which steps have been resolved, one entry per step.
     *
     * A step is resolved on the frame before its own gesture, so this is what
     * says which of them have been and which are still waiting for their turn.
     */
    std::vector<bool> driven;

    /** @brief The frame being drawn. */
    std::uint32_t frame = 0;

    /**
     * @brief Where the script last put the pointer, and whether it has.
     *
     * Held rather than recomputed, because the frames between a move and the
     * press it belongs to carry no event of their own and the pointer still has
     * to be where the move put it.
     */
    bool have_pointer = false;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
};

/** @brief The one run this process has, script or no script. */
[[nodiscard]] capture_run& run() {
    static capture_run instance;
    return instance;
}

/**
 * @brief Whether @p target and @p label name the same element as @p element.
 *
 * A step with a label means a thing wearing that label, and a step without one
 * means a row, which wears none. Either way the two have to be equal, because
 * rows of the same name are on screen at once: an entry appears in the tree and
 * again in the list, and a search that took the label loosely would answer a
 * step about a tree row with the list row drawn over it.
 */
[[nodiscard]] bool names_same(const widget::capture_element& element,
    std::string_view target, std::string_view label) {
    return element.target == target && element.label == label;
}

/**
 * @brief The element a step names, or nothing.
 *
 * The finished frame is searched first and the frame in progress second. The
 * finished one is what a person writing the step could see, and the one in
 * progress matters only for a window that has just appeared, which is on screen
 * and reachable but was not there a frame ago.
 *
 * Each is searched backwards, because the last match in it is the topmost thing
 * drawn with that name: a window drawn over another is drawn after it, and a
 * step means the row it is looking at.
 */
[[nodiscard]] const widget::capture_element*
find_element(const capture_run& state, const widget::capture_step& step) {
    const auto search = [&](const std::vector<widget::capture_element>& pool)
        -> const widget::capture_element* {
        const auto found = std::find_if(pool.rbegin(), pool.rend(),
            [&](const widget::capture_element& element) {
                return names_same(element, step.target, step.label);
            });
        return found == pool.rend() ? nullptr : &*found;
    };

    if (const auto* found = search(state.last_frame); found != nullptr)
        return found;
    return search(state.elements);
}

/**
 * @brief Reports a step that cannot be driven, and where it was written.
 *
 * What was on screen is printed with it. A step that names a row under a folder
 * that is shut, or a row the filter removed, is the common case, and the answer
 * to "why is it not there" is the list of what is. Without it a script's author
 * is left guessing whether the name is wrong, the row is hidden, or the hook
 * never recorded it at all.
 */
void report_unreachable(const capture_run& state, std::string_view target,
    std::string_view label, std::size_t line, const char* reason) {
    std::fprintf(stderr, "hedgr capture: line %zu: no %s named \"%s\"\n", line,
        reason, std::string{target}.c_str());

    const auto& pool = state.last_frame.empty() ? state.elements
                                                : state.last_frame;
    std::fprintf(stderr, "  on screen:");
    std::size_t shown = 0;
    for (const auto& element : pool) {
        if (element.target.empty() || shown++ >= 24)
            continue;
        std::fprintf(stderr, " %s%s%s", element.target.c_str(),
            element.label.empty() ? "" : " [", element.label.c_str());
        if (!element.label.empty())
            std::fprintf(stderr, "]");
    }
    std::fprintf(stderr, "%s\n", label.empty() ? "" : " (and others)");
}

/**
 * @brief Whether the element was drawn on the frame already on screen.
 *
 * A step resolves as its own frame begins, so this can only fail when nothing
 * has been drawn yet, which is a script whose first gesture is asked for before
 * the interface has had a frame. It is a guard rather than a policy.
 */
[[nodiscard]] bool still_on_screen(const capture_run& state,
    const widget::capture_element& element) {
    return element.frame + 1 >= state.frame;
}

/** @brief Whether a step's gesture wants the left button pressed. */
[[nodiscard]] bool wants_press(const widget::capture_step& step,
    const widget::capture_element& element) {
    switch (step.kind) {
    case widget::capture_step_kind::click:
    case widget::capture_step_kind::click_arrow:
        return true;
    case widget::capture_step_kind::focus:
        return false;
    case widget::capture_step_kind::expand:
        return !element.open;
    case widget::capture_step_kind::collapse:
        return element.open;
    }
    return false;
}

/** @brief Whether a step drives a row's expander rather than its label. */
[[nodiscard]] bool wants_arrow(const widget::capture_step& step) {
    return step.kind == widget::capture_step_kind::click_arrow
        || step.kind == widget::capture_step_kind::expand
        || step.kind == widget::capture_step_kind::collapse;
}

/**
 * @brief Where in the element a step's gesture lands.
 *
 * The arrow's own middle for a step that works the expander, and the label's
 * middle for one that selects the row, because a row spans the tree's whole
 * width and its middle is past the end of its text. Both come from the
 * rectangle ImGui computed, so a step lands wherever the row actually is.
 */
[[nodiscard]] ImVec2 point_for(const widget::capture_element& element,
    const widget::capture_step& step) {
    const float middle_y = (element.min_y + element.max_y) * 0.5f;
    if (wants_arrow(step))
        return ImVec2(element.min_x + element.arrow_width * 0.5f, middle_y);

    const float middle_x = (element.label_min_x + element.max_x) * 0.5f;
    return ImVec2(std::max(element.min_x, middle_x), middle_y);
}

/** @brief The frame a gesture's own part is driven on. */
[[nodiscard]] std::uint32_t frame_for(std::size_t gesture, std::size_t part) {
    return widget::capture_first_step_frame
        + static_cast<std::uint32_t>(gesture * widget::capture_frames_per_step
              + part);
}

/** @brief Adds one gesture's move, and its press and release when it has one. */
void schedule(capture_run& state, const widget::capture_step& step,
    const widget::capture_element& element, std::size_t gesture) {
    scheduled_event move;
    move.frame = frame_for(gesture, 0);
    const ImVec2 point = point_for(element, step);
    move.x = point.x;
    move.y = point.y;
    move.moves = true;

    /*
     * The move is driven on its own frame, before any press. That is what puts
     * the pointer on the row while it is still up, so the frame a script
     * captures shows the same highlight the operator's own pointer leaves.
     */
    state.events.push_back(move);
    if (!wants_press(step, element))
        return;

    scheduled_event down = move;
    down.frame = frame_for(gesture, 1);
    down.moves = false;
    down.down = true;
    state.events.push_back(down);

    scheduled_event up = move;
    up.frame = frame_for(gesture, 2);
    up.moves = false;
    up.down = false;
    state.events.push_back(up);
}

/**
 * @brief Resolves one step against what is on screen now, and schedules it.
 *
 * A step is resolved on the frame before its own gesture, not all of them at
 * once, because a step can only name a row an earlier step brought into being:
 * "expand Development, then click one of its entries" is a thing an operator
 * does, and the entry does not exist on screen until the folder is open. A
 * script resolved in one pass could only ever click what the first frame
 * happened to show, which is the shape of check that missed a broken tree.
 *
 * @return Whether the step could be driven.
 */
[[nodiscard]] bool resolve_step(capture_run& state,
    const widget::capture_step& step, std::size_t gesture) {
    const auto* element = find_element(state, step);
    if (element == nullptr) {
        report_unreachable(state, step.target, step.label, step.line,
            "row on screen");
        return false;
    }
    if (!still_on_screen(state, *element)) {
        report_unreachable(state, step.target, step.label, step.line,
            "row still on screen");
        return false;
    }
    if (element->arrow_width <= 0.0f && wants_arrow(step)) {
        report_unreachable(state, step.target, step.label, step.line,
            "row with an expander");
        return false;
    }

    schedule(state, step, *element, gesture);
    return true;
}

/**
 * @brief Resolves every step whose own frame has arrived and is not yet driven.
 *
 * A step that resolves to nothing is retired here, which is where its line
 * number is still the one an error can name.
 */
void resolve_due_steps(capture_run& state) {
    for (std::size_t index = 0; index < state.steps.size(); ++index) {
        if (state.driven[index])
            continue;
        if (frame_for(index, 0) > state.frame)
            break;
        static_cast<void>(resolve_step(state, state.steps[index], index));
        state.driven[index] = true;
    }
}

/** @brief What the pointer does on one frame, if anything. */
struct frame_input final {
    /** @brief Whether the pointer is moved, and where to. */
    bool moves = false;
    float x = 0.0f;
    float y = 0.0f;

    /** @brief Whether the left button is driven, and in which direction. */
    bool have_button = false;
    bool down = false;
};

[[nodiscard]] frame_input input_for(const capture_run& state,
    std::uint32_t frame) {
    frame_input input;
    for (const auto& event : state.events) {
        if (event.frame != frame)
            continue;
        if (event.moves) {
            input.moves = true;
            input.x = event.x;
            input.y = event.y;
        } else {
            input.have_button = true;
            input.down = event.down;
        }
    }
    return input;
}

/** @brief The script text, from the environment or from the file it names. */
[[nodiscard]] std::string script_text() {
    if (const char* path = std::getenv("HEDGR_CAPTURE_INPUT_FILE");
        path != nullptr && *path != '\0') {
        std::ifstream in(path);
        if (!in)
            return {};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }
    if (const char* text = std::getenv("HEDGR_CAPTURE_INPUT"); text != nullptr)
        return std::string{text};
    return {};
}

/**
 * @brief Reads a name from the front of @p text, or nothing.
 *
 * Quoted form is @c "a name with spaces" and unquoted form is one word. The
 * quoting is not decoration: a row's name is allowed to hold spaces and a
 * step's fields are separated by them, so @c "Brave Hopper" is how a step says
 * one name of two words rather than a name and a label.
 */
[[nodiscard]] std::string read_target(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return {};

    if (text[first] == '"') {
        const auto closing = text.find('"', first + 1);
        if (closing == std::string_view::npos)
            return {};
        return std::string{text.substr(first + 1, closing - first - 1)};
    }

    const auto last = text.find_first_of(" \t", first);
    if (last == std::string_view::npos)
        return std::string{text.substr(first)};
    return std::string{text.substr(first, last - first)};
}

/**
 * @brief Reads everything left on the line, trimmed, or nothing.
 *
 * A label is the whole remainder and not one word, because a control's label is
 * a sentence: "Probe the selected instance now" is one label and the bar before
 * it is what says so.
 */
[[nodiscard]] std::string read_label(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t");
    return std::string{text.substr(first, last - first + 1)};
}

/**
 * @brief Splits a step line into its target and its control label.
 *
 * A bar separates them, and only a bar. A step that named a multi-word target
 * and a label by adjacency alone would mean two things, which is the shape of
 * mistake this parser exists to make impossible.
 */
void read_step_fields(std::string_view rest, widget::capture_step& step) {
    const auto bar = rest.find('|');
    step.target = read_target(rest.substr(0, bar));
    if (bar != std::string_view::npos)
        step.label = read_label(rest.substr(bar + 1));
}

}

namespace hedgr::ui::widget {

std::vector<capture_step> parse_capture_steps(std::string_view text) {
    std::vector<capture_step> steps;
    std::istringstream lines{std::string{text}};
    std::string line;
    std::size_t number = 0;

    while (std::getline(lines, line)) {
        ++number;
        std::istringstream fields{line};
        std::string gesture;
        if (!(fields >> gesture) || gesture[0] == '#')
            continue;

        capture_step step;
        step.line = number;
        if (gesture == "click")
            step.kind = capture_step_kind::click;
        else if (gesture == "click_arrow")
            step.kind = capture_step_kind::click_arrow;
        else if (gesture == "expand")
            step.kind = capture_step_kind::expand;
        else if (gesture == "collapse")
            step.kind = capture_step_kind::collapse;
        else if (gesture == "focus")
            step.kind = capture_step_kind::focus;
        else
            continue;

        std::string rest;
        std::getline(fields, rest);
        read_step_fields(rest, step);
        if (step.target.empty())
            continue;

        steps.push_back(std::move(step));
    }
    return steps;
}

void start_capture_run() {
    capture_run& state = run();
    state.steps = parse_capture_steps(script_text());
    state.active = !state.steps.empty();
    state.driven.assign(state.steps.size(), false);
    state.elements.clear();
    state.last_frame.clear();
    state.events.clear();
}

void begin_capture_frame(std::uint32_t frame) {
    capture_run& state = run();
    if (!state.active)
        return;
    state.frame = frame;
    state.elements.clear();
}

void end_capture_frame() {
    capture_run& state = run();
    if (!state.active)
        return;

    /*
     * The frame just submitted becomes the one a gesture is measured against,
     * and an empty list becomes the one this frame's successor fills. Swapping
     * rather than copying keeps both buffers for reuse and leaves nothing that
     * was drawn half-submitted visible to a step.
     */
    state.last_frame.swap(state.elements);
}

void observe_capture_element(std::string_view target,
    const capture_element& element) {
    capture_run& state = run();
    if (!state.active || target.empty())
        return;

    capture_element seen = element;
    seen.target = std::string{target};
    seen.frame = state.frame;
    state.elements.push_back(std::move(seen));
}

void drive_capture_steps(std::uint32_t frame) {
    capture_run& state = run();
    if (!state.active)
        return;
    if (frame < capture_first_step_frame)
        return;
    resolve_due_steps(state);

    const frame_input input = input_for(state, frame);
    if (!input.moves && !input.have_button)
        return;

    /*
     * Queued rather than applied: ImGui drains its input queue inside NewFrame,
     * which is the call this runs before, so the frame that is about to be
     * drawn is the frame that sees the gesture.
     */
    ImGuiIO& io = ImGui::GetIO();
    if (input.moves) {
        state.have_pointer = true;
        state.pointer_x = input.x;
        state.pointer_y = input.y;
        io.AddMousePosEvent(input.x, input.y);
    }
    if (input.have_button)
        io.AddMouseButtonEvent(0, input.down);
}

void hold_capture_pointer() {
    capture_run& state = run();
    if (!state.active || !state.have_pointer)
        return;

    /*
     * Written after the queue has been drained, which is the only moment the
     * pointer can be taken back from whatever else set it. ImGui's own idea of
     * where its mouse is governs hovering, so this is where the script's
     * gesture is made to be the one the frame acts on.
     */
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(state.pointer_x, state.pointer_y);
    io.MousePosPrev = io.MousePos;
}

}
