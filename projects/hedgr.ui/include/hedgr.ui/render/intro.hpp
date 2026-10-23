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
#include "hedgr.ui/theme/theme.hpp"
#include <cmath>
#include <cstdint>
#include <string_view>

namespace hedgr::ui::render {

/**
 * @name The launch intro's timeline
 *
 * One slow pulse, like a neon sign switching on and off. The geometry never
 * changes, only the light: the logo and the word are dark at the start, brighten
 * smoothly to their peak, dim smoothly back to dark by the dark point, and from
 * there the whole intro fades out over the main window until the total. The
 * peak is where the intro's sound is loudest: assets/sounds/intro_swell.wav is
 * measured by tools/measure_intro_peak.py: the centre of its loudest 50 ms window
 * is 3.023 s, and the two move together.
 *
 * @c intro_max_step is the most the brightness may change between two samples
 * 10 ms apart. Smoothstep's steepest slope is 1.5 over the longer ramp, which
 * is a step of about 0.007 on the falling ramp, so this holds a pulse that cannot jump or flicker.
 * @{
 */
inline constexpr std::int64_t intro_peak_ns = 3'023'000'000;
inline constexpr std::int64_t intro_dark_ns = 5'200'000'000;
inline constexpr std::int64_t intro_total_ns = 6'000'000'000;
inline constexpr float intro_max_step = 0.01f;
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
     * @brief How bright the logo and word are, 0 for dark and 1 at the peak.
     *
     * Smoothstep up to the peak and smoothstep down to the dark point, so the
     * slope is zero at the start, the peak and the dark point: one eased
     * swell with a single maximum. Before the start and after the dark point it
     * is 0.
     */
    [[nodiscard]] static constexpr float brightness(
        std::int64_t elapsed_ns) noexcept {
        if (elapsed_ns <= 0 || elapsed_ns >= intro_dark_ns)
            return 0.0f;
        const float x = elapsed_ns <= intro_peak_ns
            ? static_cast<float>(elapsed_ns) / static_cast<float>(intro_peak_ns)
            : static_cast<float>(intro_dark_ns - elapsed_ns)
                / static_cast<float>(intro_dark_ns - intro_peak_ns);
        return x * x * (3.0f - 2.0f * x);
    }

    /**
     * @brief The halo's share of its peak alpha at @p brightness.
     *
     * A power of the brightness, so the halo ignites late and fades early, as
     * a neon tube's does: dim light on the sign is mostly the crisp shapes.
     */
    [[nodiscard]] static float halo_gain(float brightness) noexcept {
        return std::pow(brightness, theme::intro_halo_gamma);
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
 * The desk wallpaper, darker, with the ORE swirl and the word
 * as a lockup a little above the centre, lit at @p elapsed_ns like a neon
 * sign: fixed geometry, with the colour of the shapes and the alpha and colour
 * of their soft halo following the brightness. Emitted on the foreground
 * draw list, so it is above every window and popup, and the whole of it is
 * faded by the veil alpha so the main window comes up through it. The logo and
 * its blurred glow are textures drawn through @p input's texture source; with
 * no source or no document the wallpaper and the word still draw. Nothing allocates after the first frame,
 * because the textures are cached by the source.
 *
 * @param logo_svg The logo's SVG document, as the menu bar draws it.
 */
HEDGR_UI_EXPORT void draw_intro(const render_input& input,
    std::string_view logo_svg, std::int64_t elapsed_ns);

}

#endif
