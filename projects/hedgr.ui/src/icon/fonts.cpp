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
#include "hedgr.ui/icon/fonts.hpp"

#include "hedgr.ui/icon/third_party/IconsFontAwesome6.h"
#include "imgui.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/**
 * @brief The glyph ranges the interface actually draws.
 *
 * ImGui stores the pointer rather than copying the list, so this array has to
 * outlive the font. A function-local static gives it static storage duration
 * and one construction, which is exactly the lifetime a font needs.
 *
 * The icon range is bounded by ICON_MAX_16_FA rather than ICON_MAX_FA: this
 * build does not define IMGUI_USE_WCHAR32, so ImWchar is 16 bits wide and the
 * values beyond the basic multilingual plane would truncate into it.
 */
const ImWchar* icon_glyph_ranges() {
    static const std::array<ImWchar, 5> ranges{
        ICON_MIN_FA, ICON_MAX_16_FA,
        0x2000, 0x206f,   // General punctuation: the em dash and friends.
        0
    };
    return ranges.data();
}

/**
 * @brief The directory holding the running executable.
 *
 * The absolute path comes from /proc/self/exe so the lookup works whatever
 * directory the process was started from. Where that is unavailable, an empty
 * path is returned and every candidate is simply skipped.
 */
std::filesystem::path executable_directory() {
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
        return {};
    return self.parent_path();
}

/**
 * @brief The first candidate path that exists, or an empty path.
 */
std::filesystem::path first_existing(const std::vector<std::filesystem::path>& candidates) {
    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (candidate.empty())
            continue;
        if (std::filesystem::exists(candidate, ec) && !ec)
            return candidate;
    }
    return {};
}

/**
 * @brief Where the vendored icon font lives at run time.
 *
 * Installed beside the library under share/hedgr.ui, or, for a run straight
 * out of the build tree, in the source tree's assets directory.
 */
std::filesystem::path icon_font_path() {
    const auto exe_dir = executable_directory();
    std::vector<std::filesystem::path> candidates;
    if (!exe_dir.empty()) {
        candidates.push_back(
            exe_dir.parent_path() / "share" / "hedgr.ui" / "fa-solid-900.ttf");
        candidates.push_back(exe_dir / "fa-solid-900.ttf");
    }
    candidates.push_back("assets/fonts/fa-solid-900.ttf");
    return first_existing(candidates);
}

/**
 * @brief Where the base UI font lives.
 *
 * A condensed sans is the trading-desk convention: more label per pixel. The
 * list is ordered by preference, and an empty result means fall back to the
 * font embedded in ImGui, which always exists.
 */
std::filesystem::path base_font_path() {
    return first_existing({
        "/usr/share/fonts/truetype/roboto/hinted/RobotoCondensed-Regular.ttf",
        "/usr/share/fonts/truetype/roboto/hinted/Roboto-Medium.ttf",
        "/usr/share/fonts/truetype/gfonts/roboto/roboto-v20-latin-regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    });
}

}

namespace hedgr::ui::icon {

void load_ui_fonts(float base_size_px) {
    ImGuiIO& io = ImGui::GetIO();

    /*
     * The icon atlas is dense and mostly empty space: a 512x128 texture, the
     * default, would be resized on the first frame and the whole atlas
     * re-uploaded. Asking for a megapixel up front costs one allocation and
     * saves that rebuild.
     */
    io.Fonts->TexMinWidth = 1024;
    io.Fonts->TexMinHeight = 1024;

    const auto base = base_font_path();
    if (base.empty())
        io.Fonts->AddFontDefaultVector();
    else
        io.Fonts->AddFontFromFileTTF(base.string().c_str(), base_size_px);

    const auto glyphs = icon_font_path();
    if (glyphs.empty()) {
        /*
         * No icon font: the base font is already loaded and the interface is
         * merely uglier. Adding nothing keeps the atlas valid.
         */
        return;
    }

    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;

    /*
     * A fixed advance width for every icon is what makes an icon-only toolbar
     * line up: without it a narrow glyph and a wide one make buttons of
     * different widths, and the strip looks ragged.
     */
    config.GlyphMinAdvanceX = base_size_px;
    config.GlyphOffset.y = base_size_px * 0.1f;

    io.Fonts->AddFontFromFileTTF(glyphs.string().c_str(), base_size_px,
        &config, icon_glyph_ranges());
}

}
