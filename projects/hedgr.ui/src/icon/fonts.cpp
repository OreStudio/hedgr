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

#include "hedgr.ui/icon/icons.hpp"
#include "imgui.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

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
 * @brief Where the rasterised icon sheet lives at run time.
 *
 * Installed beside the library under share/hedgr.ui, or, for a run straight
 * out of the build tree, in the source tree's assets directory. The sheet is
 * generated from the icon table by build/scripts/build-fluent-icons.py.
 */
std::filesystem::path icon_sheet_path() {
    const auto exe_dir = executable_directory();
    std::vector<std::filesystem::path> candidates;
    if (!exe_dir.empty()) {
        candidates.push_back(
            exe_dir.parent_path() / "share" / "hedgr.ui" / "fluent-icons.pgm");
        candidates.push_back(exe_dir / "fluent-icons.pgm");
    }
    candidates.push_back("assets/fonts/fluent-icons.pgm");
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
        "/usr/share/fonts/gfonts/roboto/roboto-v20-latin-regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    });
}

/**
 * @brief The coverage bytes of the icon sheet, or an empty vector.
 *
 * The sheet is a binary PGM, which is a small text header followed by one byte
 * per pixel. A PGM rather than a PNG because this component links no image
 * decoder and the whole format is the twenty lines below. The header is read
 * with the stream operators, which stop on whitespace, and the one separator
 * byte the format asks for after the maximum is consumed explicitly so that
 * the first pixel is not taken for it.
 *
 * Any malformed or truncated sheet gives back nothing, which the caller reads
 * as "draw no icons" rather than as a half-drawn set.
 */
std::vector<std::uint8_t> read_icon_sheet(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    std::string magic;
    int width = 0;
    int height = 0;
    int maximum = 0;
    file >> magic >> width >> height >> maximum;
    if (!file || magic != "P5" || width <= 0 || height <= 0 || maximum != 255)
        return {};
    file.get();

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    file.read(reinterpret_cast<char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size()));
    if (file.gcount() != static_cast<std::streamsize>(pixels.size()))
        return {};
    return pixels;
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

    /*
     * The icons are registered as glyphs of the base font rather than as a
     * font of their own, which is what lets a label say an icon and a word in
     * one string without a PushFont around the icon.
     */
    ImFont* font = io.Fonts->Fonts[0];

    const auto sheet_path = icon_sheet_path();
    const auto sheet = sheet_path.empty()
        ? std::vector<std::uint8_t>{}
        : read_icon_sheet(sheet_path);

    constexpr std::size_t cell = static_cast<std::size_t>(glyph_pixel_size);
    const std::size_t sheet_width = icon_count * cell;
    if (sheet.size() != sheet_width * cell) {
        /*
         * Without a usable sheet the base font is already loaded and the
         * interface is merely uglier. Adding nothing keeps the atlas valid, and a caller
         * that draws an icon string gets ImGui's missing-glyph box rather than
         * a crash.
         */
        return;
    }

    /*
     * A normal glyph's Y0 is measured from the top of the text line and the
     * line box runs from that top down to the descent, so centring the icon on
     * that box is what makes it sit with the text beside it instead of hanging
     * below the baseline.
     */
    const ImFontBaked* metrics = font->GetFontBaked(base_size_px);
    const float line_height = metrics->Ascent - metrics->Descent;
    const ImVec2 offset(0.0f,
        (line_height - static_cast<float>(cell)) * 0.5f);

    /*
     * A fixed advance width for every icon is what makes an icon-only toolbar
     * line up: without it a narrow glyph and a wide one make buttons of
     * different widths, and the strip looks ragged.
     */
    const float advance = base_size_px;

    std::vector<ImFontAtlasRectId> ids;
    ids.reserve(icon_count);
    for (std::size_t i = 0; i < icon_count; ++i) {
        ids.push_back(io.Fonts->AddCustomRectFontGlyphForSize(font, base_size_px,
            static_cast<ImWchar>(icon_sources[i].codepoint),
            glyph_pixel_size, glyph_pixel_size, advance, offset));
    }

    /*
     * A custom glyph is drawn untinted by default, which would make every icon
     * ignore the colour the toolbar or a tree row pushes. Clearing the flag is
     * what lets ImGui multiply the glyph by the current text colour, and that
     * is the property the rest of the interface relies on.
     */
    ImFontBaked* baked = font->GetFontBaked(base_size_px);
    for (std::size_t i = 0; i < icon_count; ++i) {
        if (ImFontGlyph* glyph = baked->FindGlyphNoFallback(
                static_cast<ImWchar>(icon_sources[i].codepoint)))
            glyph->Colored = false;
    }

    /*
     * Packing a rectangle may have grown the atlas, so the texture is fetched
     * once every rectangle exists rather than before the first one.
     */
    ImTextureData* texture = io.Fonts->TexData;
    if (texture == nullptr || texture->Pixels == nullptr
        || texture->BytesPerPixel != 4)
        return;

    for (std::size_t i = 0; i < icon_count; ++i) {
        ImFontAtlasRect rect;
        if (!io.Fonts->GetCustomRect(ids[i], &rect))
            continue;
        for (std::size_t y = 0; y < cell; ++y) {
            const std::uint8_t* source =
                sheet.data() + y * sheet_width + i * cell;
            auto* destination = reinterpret_cast<ImU32*>(texture->Pixels)
                + (rect.y + y) * texture->Width + rect.x;
            for (std::size_t x = 0; x < cell; ++x) {
                /*
                 * White with the sheet's coverage as alpha, so the text colour
                 * shows through and the glyph reads as part of the run of text
                 * rather than as a picture pasted into it.
                 */
                destination[x] = IM_COL32(255, 255, 255, source[x]);
            }
        }
    }

    /*
     * The pixels are white and alpha only, so a backend is free to choose a
     * smaller texture format. Saying so is what lets it.
     */
    texture->UseColors = false;
}

}
