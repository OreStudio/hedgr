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
#ifndef HEDGR_UI_WIDGET_STATUS_FOOT_HPP
#define HEDGR_UI_WIDGET_STATUS_FOOT_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/widget/status_bar.hpp"
#include "imgui.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hedgr::ui::widget {

/**
 * @brief One choice on the foot, drawn as the value it currently holds.
 *
 * The market-data screens put their period, style and overlay choices here
 * rather than in a row above the plot. A value and a list rather than a string
 * label, so the control writes the caller's choice in place and the two never
 * disagree about what is selected. The options are borrowed for the frame,
 * which is why the caller owns the arrays and the foot only reads them.
 */
struct foot_combo final {
    /** @brief ImGui id, unique among the controls of one foot. */
    const char* id;

    /** @brief The control's name, shown on hover. */
    const char* tooltip;

    /** @brief A quiet word drawn before the control, or null for none. */
    const char* caption = nullptr;

    /** @brief The option labels, in menu order. */
    const char* const* options;

    /** @brief How many entries @c options holds. */
    int option_count = 0;

    /** @brief The selected index, which the control writes. */
    int* selected = nullptr;
};

/**
 * @brief The choices one foot carries, in a fixed room.
 *
 * A foot is one line and has space for a couple of controls; a vector here
 * allocated once per frame for a control that never changes size. The cap is
 * asserted rather than grown, because a third control would not fit.
 */
class foot_controls final {
public:
    /** @brief How many controls a foot can carry. */
    static constexpr std::size_t capacity = 2;

    void push_back(const foot_combo& control) {
        assert(count_ < capacity);
        if (count_ >= capacity)
            return;
        items_[count_++] = control;
    }

    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    [[nodiscard]] const foot_combo* begin() const noexcept {
        return items_.data();
    }
    [[nodiscard]] const foot_combo* end() const noexcept {
        return items_.data() + count_;
    }

private:
    std::array<foot_combo, capacity> items_{};
    std::size_t count_ = 0;
};

/**
 * @brief The state a window's foot reports, at its right end and as its dot.
 *
 * A value rather than a drawing call, because the foot knows how to lay a state
 * out and knows nothing about instances or accounts: each window resolves its
 * own selection to a word and a colour and hands the result over. That is what
 * lets the Instances window and the Accounts window share one foot without
 * either reaching into the other's tables.
 *
 * The name of the thing the state is about is deliberately not here. The pane
 * above already names the selection, and repeating it at the foot made the two
 * read as two different subjects.
 */
struct foot_state final {
    /** @brief The state's own word, or empty when there is no state. */
    std::string word;

    /** @brief The state's colour, for the word and for the dot. */
    ImU32 colour = 0;

    /** @brief What the word means, shown in a tooltip over it, or empty. */
    std::string detail;

    /**
     * @brief Who the window's session is and where it is, or empty.
     *
     * Drawn at the left in the disabled colour, and only while there is no
     * message to draw. A message is news and the identity is not, so the news
     * takes the line when it comes.
     */
    std::string identity;

    /**
     * @brief How old the data is, compact, or empty when there is none.
     *
     * Drawn before the instant in the state's colour, so a feed going late
     * turns yellow and a dead one red where the eye already is. The instant
     * after it stays quiet and stays put.
     */
    std::string age;

    /**
     * @brief The instant the data is as of, or empty when there is none.
     *
     * Drawn after the state word as "as of ...". The state and the instant are
     * one statement rather than two: a freshness word without a time is a
     * claim the reader cannot check.
     */
    std::string as_of;

    /** @brief Whether the data behind the foot is being read again. */
    bool updating = false;

    /** @brief The choices drawn to the left of the state, in order. */
    foot_controls controls;
};

/**
 * @brief How tall a window's foot is, whether or not anything is on it.
 *
 * Counted from the font rather than fixed, so a theme that installs a larger
 * face gets a foot that still fits its own text.
 */
[[nodiscard]] HEDGR_UI_EXPORT float status_foot_height(
    const status_bar& status);

/**
 * @brief How tall a window's foot is once its controls are accounted for.
 *
 * A foot carrying a combo needs the combo's frame height; a foot carrying only
 * a sentence keeps the text line it had. Every other window sees the same foot
 * it did before.
 */
[[nodiscard]] HEDGR_UI_EXPORT float status_foot_height(
    const status_bar& status, const foot_state& state);

/**
 * @brief Measures the foot of the window and draws the line into it.
 *
 * Both halves are needed and the split is the point. Reserving the space first
 * is what stops the table above from changing height as messages come and go:
 * a table that grew by two rows whenever something was announced would move
 * the row under the pointer at the worst possible moment. Drawing into a child
 * of its own is what pins the line to the window's foot instead of leaving it
 * wherever the browser happened to stop.
 *
 * A small coloured disc sits at the foot's right end, ahead of the state word
 * and carrying the same colour as it. A reader gets the state at a glance from
 * the dot and reads it in words beside it, which is what this interface asks of
 * a colour: it is learned, and the word is what a person reads the first time.
 *
 * @param status The bar whose message is drawn, advanced to this frame.
 * @param now_ns The frame's clock, for the fade.
 * @param state The selected thing's state, or an empty one for no state.
 * @return Whether one of the foot's controls changed its choice this frame.
 */
HEDGR_UI_EXPORT bool draw_status_foot(status_bar& status,
    std::int64_t now_ns, const foot_state& state);

}

#endif
