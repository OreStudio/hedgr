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
#include "ores.logging/make_logger.hpp"
#include "imgui.h"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* bundled_mono_file = "JetBrainsMono-Regular.ttf";

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
 * @brief Where the per-environment instance illustrations live at run time.
 *
 * The same candidates as the icon sheet, with the instances subdirectory in
 * place of the sheet's file. A directory rather than a file because the caller
 * walks it: the set of illustrations is the set of files that shipped, and
 * naming them here would be a second list to keep in step with the assets.
 */
std::filesystem::path instance_icon_directory_path() {
    const auto exe_dir = executable_directory();
    std::vector<std::filesystem::path> candidates;
    if (!exe_dir.empty()) {
        candidates.push_back(
            exe_dir.parent_path() / "share" / "hedgr.ui" / "instances");
        candidates.push_back(exe_dir / "instances");
    }
    candidates.push_back("assets/instances");
    return first_existing(candidates);
}

/**
 * @brief Where the bundled monospace face lives, under @p exe_dir or @p root.
 *
 * Installed beside the library under share/hedgr.ui/fonts, as the icon sheet
 * is, or, for a run out of the source tree, in its assets directory.
 */
std::filesystem::path find_mono_font(
    const std::filesystem::path& exe_dir, const std::filesystem::path& root) {
    std::vector<std::filesystem::path> candidates;
    if (!exe_dir.empty()) {
        candidates.push_back(exe_dir.parent_path() / "share" / "hedgr.ui"
            / "fonts" / bundled_mono_file);
        candidates.push_back(exe_dir / "fonts" / bundled_mono_file);
    }
    if (!root.empty())
        candidates.push_back(root / "assets" / "fonts" / bundled_mono_file);
    return first_existing(candidates);
}

/**
 * @brief Where the base UI font lives.
 *
 * A proportional sans. This was a monospace for as long as the interface was
 * read as columns of numbers, and the operator has since asked for the
 * reference screenshot's look, which is set in a proportional face and sets
 * its own numeric readouts in the same one. Matching it with a monospace would
 * leave the two interfaces looking unlike each other for a reason no reader
 * could name.
 *
 * Inter leads the list because it is the neutral grotesque that the reference
 * most resembles, close enough in proportion and in the shape of the letters
 * that a reader comparing the two would not be looking at the typeface, and
 * because it is installed here by the distribution. DejaVu Sans follows as the
 * face that is present on a bare system, then Roboto, Noto Sans and Liberation
 * Sans, which are metric relatives of one another and of the faces on the
 * desktop this runs on. Each is a fallback rather than a preference, because a
 * font named and not installed falls back silently and the interface would
 * look unlike the reference for a reason nobody could see.
 *
 * An empty result means fall back to the font embedded in ImGui, which always
 * exists.
 */
std::filesystem::path numeric_font_path() {
    std::vector<std::filesystem::path> candidates;
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        const auto user_fonts = std::filesystem::path(home) / ".local/share/fonts";
        candidates.push_back(user_fonts / "JetBrainsMono-Regular.ttf");
        candidates.push_back(user_fonts / "RobotoMono-Regular.ttf");
    }
    candidates.push_back(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
    candidates.push_back(
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf");
    candidates.push_back(
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf");
    return first_existing(candidates);
}

std::filesystem::path base_font_path() {
    std::vector<std::filesystem::path> candidates;

    /*
     * A face the user installed is looked at first, since a person who has put
     * a UI face in their own font directory has a font they chose. HOME is
     * read rather than written as a literal path so the lookup works for
     * whoever runs it.
     */
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        const auto user_fonts = std::filesystem::path(home) / ".local/share/fonts";
        candidates.push_back(user_fonts / "Inter-Regular.otf");
        candidates.push_back(user_fonts / "Inter-Regular.ttf");
    }

    candidates.push_back("/usr/share/fonts/opentype/inter/Inter-Regular.otf");
    candidates.push_back("/usr/share/fonts/truetype/inter/Inter-Regular.ttf");
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back(
        "/usr/share/fonts/truetype/roboto/hinted/Roboto-Regular.ttf");
    candidates.push_back("/usr/share/fonts/truetype/roboto/Roboto-Regular.ttf");
    candidates.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
    candidates.push_back(
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    return first_existing(candidates);
}

/**
 * @brief The bold file of the family @p base belongs to, or an empty path.
 *
 * Every base candidate names its weight the same way: Inter-Regular.otf beside
 * Inter-Bold.otf, DejaVuSans.ttf beside DejaVuSans-Bold.ttf.
 */
std::filesystem::path bold_font_path(const std::filesystem::path& base) {
    if (base.empty())
        return {};
    std::string stem = base.stem().string();
    constexpr std::string_view regular = "-Regular";
    if (stem.ends_with(regular))
        stem.resize(stem.size() - regular.size());
    return first_existing(
        {base.parent_path() / (stem + "-Bold" + base.extension().string())});
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

namespace {

/** @brief This loader's logger. A missing asset is reported, not fatal. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.ui.icon.fonts");
    return instance;
}

}

namespace {

ImFont* numeric_font_instance = nullptr;
ImFont* mono_font_instance = nullptr;
ImFont* bold_font_instance = nullptr;

}

namespace hedgr::ui::icon {

ImFont* numeric_font() {
    ImGuiIO& io = ImGui::GetIO();
    if (numeric_font_instance != nullptr
        && io.Fonts->Fonts.contains(numeric_font_instance))
        return numeric_font_instance;
    return io.Fonts->Fonts.empty() ? nullptr : io.Fonts->Fonts[0];
}

ImFont* mono_font() {
    ImGuiIO& io = ImGui::GetIO();
    if (mono_font_instance != nullptr
        && io.Fonts->Fonts.contains(mono_font_instance))
        return mono_font_instance;
    return io.Fonts->Fonts.empty() ? nullptr : io.Fonts->Fonts[0];
}

std::filesystem::path mono_font_path(const std::filesystem::path& exe_dir,
    const std::filesystem::path& source_root) {
    return find_mono_font(exe_dir, source_root);
}

ImFont* bold_font() {
    ImGuiIO& io = ImGui::GetIO();
    /*
     * Checked against the current atlas because the pointer outlives the
     * context that loaded it, and a test process creates several.
     */
    if (bold_font_instance != nullptr
        && io.Fonts->Fonts.contains(bold_font_instance))
        return bold_font_instance;
    return io.Fonts->Fonts.empty() ? nullptr : io.Fonts->Fonts[0];
}

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
    if (base.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "No interface font found; using ImGui's built-in font";
        io.Fonts->AddFontDefaultVector();
    } else {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Loaded the interface font from " << base.string();
        io.Fonts->AddFontFromFileTTF(base.string().c_str(), base_size_px);
    }

    /*
     * The numbers get a face of their own. It is loaded before the icons are
     * merged, so the icon glyphs stay on the base font and the numbers' face
     * carries digits and nothing else.
     */
    const auto bold = bold_font_path(base);
    if (!bold.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Loaded the bold font from " << bold.string();
        bold_font_instance =
            io.Fonts->AddFontFromFileTTF(bold.string().c_str(), base_size_px);
    }

    /*
     * The bundled monospace is the menus' and tab labels' face. It doubles as
     * the numeric face when it is there, so one file is one atlas entry, and
     * the installed monospaces below remain the numbers' fallback.
     */
    const auto mono = find_mono_font(executable_directory(), ".");
    if (!mono.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Loaded the monospace font from " << mono.string();
        mono_font_instance =
            io.Fonts->AddFontFromFileTTF(mono.string().c_str(), base_size_px);
        numeric_font_instance = mono_font_instance;
    } else {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The bundled monospace font is missing; menus use the base font";
    }

    const auto numeric = mono.empty() ? numeric_font_path()
                                      : std::filesystem::path{};
    if (!numeric.empty()) {
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Loaded the numeric font from " << numeric.string();
        numeric_font_instance =
            io.Fonts->AddFontFromFileTTF(numeric.string().c_str(), base_size_px);
    }

    /*
     * The icons are registered as glyphs of the base font (and the monospace,
     * below) rather than as a font of their own, which is what lets a label say an icon and a word in
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
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "The icon sheet is missing or the wrong size (expected "
            << sheet_width * cell << " bytes, read " << sheet.size()
            << "); icons will draw as missing glyphs";
        /*
         * Without a usable sheet the base font is already loaded and the
         * interface is merely uglier. Adding nothing keeps the atlas valid, and a caller
         * that draws an icon string gets ImGui's missing-glyph box rather than
         * a crash.
         */
        return;
    }

    /*
     * The tab labels and the menus are set in the monospace, and a window's
     * title begins with its icon, so the monospace carries the icons as well.
     * Each face gets its own rectangles because a glyph belongs to one font.
     */
    std::vector<ImFont*> faces{font};
    if (mono_font_instance != nullptr && mono_font_instance != font)
        faces.push_back(mono_font_instance);

    /*
     * A fixed advance width for every icon is what makes an icon-only toolbar
     * line up: without it a narrow glyph and a wide one make buttons of
     * different widths, and the strip looks ragged.
     */
    const float advance = base_size_px;

    std::vector<ImFontAtlasRectId> ids;
    ids.reserve(icon_count * faces.size());
    for (ImFont* face : faces) {
        /*
         * A normal glyph's Y0 is measured from the top of the text line and the
         * line box runs from that top down to the descent, so centring the icon
         * on that box is what makes it sit with the text beside it instead of
         * hanging below the baseline.
         */
        const ImFontBaked* metrics = face->GetFontBaked(base_size_px);
        const float line_height = metrics->Ascent - metrics->Descent;
        const ImVec2 offset(0.0f,
            (line_height - static_cast<float>(cell)) * 0.5f);
        for (std::size_t i = 0; i < icon_count; ++i) {
            ids.push_back(io.Fonts->AddCustomRectFontGlyphForSize(face,
                base_size_px, static_cast<ImWchar>(icon_sources[i].codepoint),
                glyph_pixel_size, glyph_pixel_size, advance, offset));
        }
    }

    /*
     * A custom glyph is drawn untinted by default, which would make every icon
     * ignore the colour the toolbar or a tree row pushes. Clearing the flag is
     * what lets ImGui multiply the glyph by the current text colour, and that
     * is the property the rest of the interface relies on.
     */
    for (ImFont* face : faces) {
        ImFontBaked* baked = face->GetFontBaked(base_size_px);
        for (std::size_t i = 0; i < icon_count; ++i) {
            if (ImFontGlyph* glyph = baked->FindGlyphNoFallback(
                    static_cast<ImWchar>(icon_sources[i].codepoint)))
                glyph->Colored = false;
        }
    }

    /*
     * Packing a rectangle may have grown the atlas, so the texture is fetched
     * once every rectangle exists rather than before the first one.
     */
    ImTextureData* texture = io.Fonts->TexData;
    if (texture == nullptr || texture->Pixels == nullptr
        || texture->BytesPerPixel != 4)
        return;

    for (std::size_t n = 0; n < ids.size(); ++n) {
        const std::size_t i = n % icon_count;
        ImFontAtlasRect rect;
        if (!io.Fonts->GetCustomRect(ids[n], &rect))
            continue;
        for (std::size_t y = 0; y < cell; ++y) {
            const std::uint8_t* source =
                sheet.data() + y * sheet_width + i * cell;
            auto* destination = reinterpret_cast<ImU32*>(texture->Pixels)
                + (static_cast<std::size_t>(rect.y) + y)
                    * static_cast<std::size_t>(texture->Width)
                + static_cast<std::size_t>(rect.x);
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

std::filesystem::path instance_icon_directory() {
    return instance_icon_directory_path();
}

}
