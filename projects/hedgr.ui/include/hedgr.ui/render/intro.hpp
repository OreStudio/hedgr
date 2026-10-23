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
#include "hedgr.ui/sound/player.hpp"
#include "hedgr.ui/theme/theme.hpp"
#include <cmath>
#include <cstdint>
#include <string_view>

namespace hedgr::ui::render {

/**
 * @name The launch intro's timeline
 *
 * Two slow pulses, like a neon sign flaring twice. The geometry never changes,
 * only the light: the logo and the word start as a dim white, the rest level,
 * brighten smoothly to a peak, dim smoothly back to the rest level, brighten
 * to the same peak again and dim back to rest by the rest point, and from
 * there the whole intro fades out over the main window until the total. The
 * pulses touch the rest level once between them, so they read as two flashes.
 * Pulse k peaks at intro_peak_ns + k * intro_pulse_period_ns. The peaks are
 * where the intro's sound is loudest: assets/sounds/intro_swell.wav is
 * measured by tools/measure_intro_peak.py, which gives the centre of the
 * loudest 50 ms window of each swell, 3.023 s and 6.223 s, and the two move
 * together.
 *
 * @c intro_max_step is the most the brightness may change between two samples
 * 10 ms apart. Smoothstep's steepest slope is 1.5 over the ramp, which is a
 * step of about 0.006 over 1.6 s, so this holds a pulse that cannot jump or
 * flicker.
 * @{
 */
inline constexpr std::int64_t intro_peak_ns = 3'023'000'000;
inline constexpr int intro_pulse_count = 2;
inline constexpr std::int64_t intro_pulse_period_ns = 3'200'000'000;
inline constexpr std::int64_t intro_rise_ns = 1'600'000'000;
inline constexpr std::int64_t intro_fall_ns = 1'600'000'000;
inline constexpr std::int64_t intro_rest_ns = 7'823'000'000;
inline constexpr std::int64_t intro_total_ns = 8'623'000'000;
inline constexpr float intro_max_step = 0.01f;
/**
 * @brief The calibration between the sound and the picture, in milliseconds.
 *
 * Positive means the sound is heard later than the picture would otherwise
 * show it, and the visual clock starts that much later. Zero is the best
 * static estimate until a real run's log says otherwise: the intro already
 * waits out the output buffer's own length, which the player reports.
 * HEDGR_INTRO_AUDIO_OFFSET_MS overrides it for a run.
 */
inline constexpr std::int64_t intro_audio_offset_ms = 0;
/** @} */

/**
 * @brief When the intro's visual clock starts, given when its sound did.
 *
 * The clock never starts before the sound has been started, and a positive
 * @p offset_ns, the sound's delay before it is heard, holds the picture back by
 * as much. A negative offset starts the clock earlier than the sound call
 * returned, for a sound that is heard before the call comes back.
 */
[[nodiscard]] constexpr std::int64_t intro_clock_start(
    std::int64_t audio_started_ns, std::int64_t offset_ns) noexcept {
    return audio_started_ns + offset_ns;
}

/**
 * @brief The audio offset a run uses, from the text of HEDGR_INTRO_AUDIO_OFFSET_MS.
 *
 * Null, or text that is not a whole number of milliseconds within a few
 * seconds either way, is the compiled-in @c intro_audio_offset_ms.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::int64_t intro_audio_offset_ns(
    const char* environment_text) noexcept;

/**
 * @brief Starts the intro's sound, then returns the visual clock's start.
 *
 * The order is the point: the sound is started on the calling thread first, and
 * only then is the steady clock read, so the picture cannot begin before the
 * sound has. The returned instant is the sound call's return, plus the player's
 * output latency and @p offset_ns, through intro_clock_start. A silent host, or
 * a player that is not started, begins the clock now. Logs the instants at info
 * so a run shows where any delay goes.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::int64_t start_intro_clock(
    sound::player& player, std::int64_t offset_ns);

/**
 * @brief Builds everything the intro's first frames would otherwise build.
 *
 * Asks @p textures for the crisp swirl and the three blurred glows, so the SVG
 * is rasterised and blurred and uploaded before the intro's clock starts and
 * the first frames do not hitch. Call it inside a frame, where the viewport
 * has its size.
 */
HEDGR_UI_EXPORT void warm_intro(image_texture_source& textures,
    std::string_view logo_svg);

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
     * @brief How bright the logo and word are: the rest level at rest, 1 at a peak.
     *
     * Each pulse is a smoothstep from the rest level up to 1 over
     * @c intro_rise_ns and a smoothstep back down over @c intro_fall_ns, so the
     * slope is zero at the start, the peak and the end of every pulse: no
     * jump, one maximum each. Pulses are @c intro_pulse_period_ns apart and
     * the brightness is the highest of them, so it is the rest level between
     * them only if a fall and the next rise add up to the period. Before the
     * first pulse and after the rest point it is the rest level.
     */
    [[nodiscard]] static constexpr float brightness(
        std::int64_t elapsed_ns) noexcept {
        constexpr float rest = theme::intro_rest_brightness;
        float bump = 0.0f;
        for (int k = 0; k < intro_pulse_count; ++k) {
            const std::int64_t from_peak
                = elapsed_ns - intro_peak_ns - k * intro_pulse_period_ns;
            const std::int64_t span = from_peak <= 0 ? intro_rise_ns : intro_fall_ns;
            const std::int64_t left = span - (from_peak <= 0 ? -from_peak : from_peak);
            if (left <= 0)
                continue;
            const float x = static_cast<float>(left) / static_cast<float>(span);
            const float pulse = x * x * (3.0f - 2.0f * x);
            bump = pulse > bump ? pulse : bump;
        }
        return rest + (1.0f - rest) * bump;
    }

    /**
     * @brief How far @p brightness is above the rest level, 0 at rest and 1 at the peak.
     *
     * What the light is made of: the halo and the colour shift take it, not the
     * raw brightness, so nothing glows at rest.
     */
    [[nodiscard]] static constexpr float lift(float brightness) noexcept {
        const float above = (brightness - theme::intro_rest_brightness)
            / (1.0f - theme::intro_rest_brightness);
        return above < 0.0f ? 0.0f : above;
    }

    /**
     * @brief The halo's share of its peak alpha at @p brightness.
     *
     * A power of the lift, so the halo is nothing at rest, ignites late and
     * fades early, as a neon tube's does.
     */
    [[nodiscard]] static float halo_gain(float brightness) noexcept {
        return std::pow(lift(brightness), theme::intro_halo_gamma);
    }

    /**
     * @brief How much of the intro is still over the main window, 1 to 0.
     *
     * Opaque until the rest point, so the main window is never seen through a
     * lit logo, and then a straight fade to nothing at the total.
     */
    [[nodiscard]] static constexpr float veil_alpha(
        std::int64_t elapsed_ns) noexcept {
        if (elapsed_ns <= intro_rest_ns)
            return 1.0f;
        if (elapsed_ns >= intro_total_ns)
            return 0.0f;
        return static_cast<float>(intro_total_ns - elapsed_ns)
            / static_cast<float>(intro_total_ns - intro_rest_ns);
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
