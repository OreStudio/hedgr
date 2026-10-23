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
#ifndef HEDGR_UI_RENDER_INTRO_HPP
#define HEDGR_UI_RENDER_INTRO_HPP

#include "hedgr.ui/export.hpp"
#include "hedgr.ui/render/renderer.hpp"
#include <cstdint>
#include <string_view>

namespace hedgr::ui::render {

/**
 * @name The launch intro's timeline
 *
 * The logo is dark at the start, rises to its brightest at the peak, is dark
 * again by the dark point, and from there the whole intro fades out over the
 * main window until the total. The peak is where the intro's sound is loudest:
 * assets/sounds/intro_swell.wav is built by tools/make_intro_sound.py so its
 * impact peaks 1.4 s in, and the two constants move together.
 * @{
 */
inline constexpr std::int64_t intro_peak_ns = 1'400'000'000;
inline constexpr std::int64_t intro_dark_ns = 2'400'000'000;
inline constexpr std::int64_t intro_total_ns = 3'200'000'000;
/** @} */

/**
 * @brief The launch intro's clock, as values a test can drive.
 *
 * Every answer is a pure function of the time since the intro started, so a
 * test states an elapsed time and reads the brightness and the veil without a
 * window or a clock. The intro plays once, at launch, and any key or click ends
 * it: a skip is a flag on the timing and nothing else.
 */
struct intro_timing final {
    /** @brief The steady clock reading it began at, in nanoseconds. */
    std::int64_t started_ns = 0;

    /** @brief Whether the operator, a failure or the environment ended it. */
    bool skipped = false;

    /**
     * @brief How bright the logo is, 0 for dark and 1 at the peak.
     *
     * A square on the way up, so the glow gathers slowly and then arrives, as
     * the sound's swell does, and a square on the way down so the impact decays
     * rather than hanging. Before the start and after the dark point it is 0.
     */
    [[nodiscard]] static constexpr float brightness(
        std::int64_t elapsed_ns) noexcept {
        if (elapsed_ns <= 0 || elapsed_ns >= intro_dark_ns)
            return 0.0f;
        if (elapsed_ns <= intro_peak_ns) {
            const float x = static_cast<float>(elapsed_ns)
                / static_cast<float>(intro_peak_ns);
            return x * x;
        }
        const float x = static_cast<float>(intro_dark_ns - elapsed_ns)
            / static_cast<float>(intro_dark_ns - intro_peak_ns);
        return x * x;
    }

    /**
     * @brief How much of the intro is still over the main window, 1 to 0.
     *
     * Opaque until the dark point, so the main window is never seen through a
     * lit logo, and then a straight fade to nothing at the total.
     */
    [[nodiscard]] static constexpr float veil_alpha(
        std::int64_t elapsed_ns) noexcept {
        if (elapsed_ns <= intro_dark_ns)
            return 1.0f;
        if (elapsed_ns >= intro_total_ns)
            return 0.0f;
        return static_cast<float>(intro_total_ns - elapsed_ns)
            / static_cast<float>(intro_total_ns - intro_dark_ns);
    }

    /** @brief Whether the intro is over, by its own length or by a skip. */
    [[nodiscard]] static constexpr bool finished(std::int64_t elapsed_ns,
        bool skipped) noexcept {
        return skipped || elapsed_ns >= intro_total_ns;
    }

    /** @brief Whether the intro should be drawn at @p now_ns. */
    [[nodiscard]] constexpr bool active(std::int64_t now_ns) const noexcept {
        return !finished(now_ns - started_ns, skipped);
    }
};

/**
 * @brief Whether a key or a mouse button went down this frame.
 *
 * Reads ImGui's input state, so it is called between NewFrame and the draw.
 * Gamepad and wheel input do not count: a stray scroll should not end the
 * brand moment.
 */
[[nodiscard]] HEDGR_UI_EXPORT bool intro_skip_requested();

/**
 * @brief Draws the launch intro over the whole viewport, above every window.
 *
 * The desk wallpaper, darker, with the ORE swirl large and centred and
 * glowing at @p elapsed_ns. Emitted on the foreground draw list, so it is above
 * every window and popup, and the whole of it is faded by the veil alpha so
 * the main window comes up through it. The logo and its glow are textures
 * drawn through @p input's texture source; with no source or no document the
 * wallpaper and the halo still draw. Nothing allocates after the first frame,
 * because the textures are cached by the source.
 *
 * @param logo_svg The logo's SVG document, as the menu bar draws it.
 */
HEDGR_UI_EXPORT void draw_intro(const render_input& input,
    std::string_view logo_svg, std::int64_t elapsed_ns);

}

#endif
