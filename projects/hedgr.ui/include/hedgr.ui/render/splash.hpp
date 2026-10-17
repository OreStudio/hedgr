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
#ifndef HEDGR_UI_RENDER_SPLASH_HPP
#define HEDGR_UI_RENDER_SPLASH_HPP

#include "hedgr.ui/export.hpp"
#include <cstdint>
#include <filesystem>

namespace hedgr::ui::render {

/**
 * @brief When the splash is on screen, as a value a test can drive.
 *
 * The splash exists to be looked at while the store is opening, and the two
 * clocks below are the whole of its life. It never waits to be dismissed,
 * because an unattended launch has nobody to dismiss it, and it never sits over
 * an interface that is already usable for the sake of a timer, which is why the
 * store becoming ready ends it early once it has been seen for long enough.
 *
 * The times are fields rather than constants so a test can state a start and a
 * pair of bounds and check the predicate without a window or a clock.
 */
struct splash_timing final {
    /** @brief The steady clock reading it was shown at, in nanoseconds. */
    std::int64_t started_ns = 0;

    /**
     * @brief The shortest time it stays up.
     *
     * A launch that opens an already unlocked store would otherwise finish the
     * unlock before the first frame and the splash would be a single frame
     * nobody could see. A short floor is what makes the splash and the sound
     * that goes with it observable at all.
     */
    std::int64_t minimum_ns = 350'000'000;

    /**
     * @brief The longest time it stays up, whatever the store is doing.
     *
     * The derivation is normally a second or so, but a store that never opens
     * must not leave the splash up forever, because the failure notice behind
     * it is the thing the operator needs to read.
     */
    std::int64_t maximum_ns = 1'500'000'000;

    /**
     * @brief Whether the splash should be drawn at @p now_ns.
     *
     * @param store_ready Whether the store has finished unlocking.
     *
     * A store that is ready before the floor is still covered until the floor,
     * so the splash does not blink; after the floor it is gone, even though the
     * maximum has not passed, because a veil over a usable interface is a lie
     * about the interface being busy.
     */
    [[nodiscard]] bool visible(std::int64_t now_ns, bool store_ready) const noexcept {
        const std::int64_t elapsed = now_ns - started_ns;
        if (elapsed < 0)
            return true;
        if (elapsed >= maximum_ns)
            return false;
        if (store_ready && elapsed >= minimum_ns)
            return false;
        return true;
    }
};

/**
 * @brief Where the splash artwork lives at run time, or an empty path.
 *
 * The image is a hedgr.ui asset, shipped beside the icon sheet and the instance
 * pictures, so the component that packages it is the component that names it.
 * The graphics component decodes and uploads the file, because it is the one
 * that links stb_image and OpenGL, exactly as it does for the background and
 * the instance illustrations.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::filesystem::path splash_image_path();

/**
 * @brief Draws the splash over the whole viewport, above every window.
 *
 * Called after the workspace has drawn, and emitted on the foreground draw list
 * so it is above every window and popup whatever ImGui's display order does
 * with them. It takes no input: the store is the gate and the splash is an
 * announcement, and a veil that swallowed clicks would be a second gate that
 * does not say what it is. It is cheap, because the texture was decoded and
 * uploaded once when the graphics were initialised.
 *
 * @param texture The uploaded image, or zero when it could not be loaded.
 * @param image_width The image's own width in pixels.
 * @param image_height The image's own height in pixels.
 */
HEDGR_UI_EXPORT void draw_splash(std::uint64_t texture, int image_width,
    int image_height);

}

#endif
