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
#include "hedgr.ui/render/splash.hpp"

#include "hedgr.ui/theme/theme.hpp"
#include "imgui.h"
#include "ores.platform/process/executable.hpp"
#include <algorithm>
#include <system_error>
#include <vector>

namespace {

/**
 * @brief Where the splash artwork lives at run time.
 *
 * The same candidates the icon sheet and the instance pictures use: installed
 * beside the library under share/hedgr.ui, or the source tree's assets
 * directory for a run straight out of the build tree. The image lives in a
 * branding subdirectory so a reader can tell it apart from the icon sheet and
 * the per-environment illustrations that share the folder.
 */
std::filesystem::path splash_image_path_impl() {
    const auto self = ores::platform::process::executable_path();
    std::vector<std::filesystem::path> candidates;
    if (!self.empty()) {
        const auto exe_dir = self.parent_path();
        candidates.push_back(exe_dir.parent_path() / "share" / "hedgr.ui"
            / "branding" / "ore-studio-splash.png");
        candidates.push_back(exe_dir / "branding" / "ore-studio-splash.png");
    }
    candidates.push_back("assets/branding/ore-studio-splash.png");

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec) && !ec)
            return candidate;
    }
    return {};
}

}

namespace hedgr::ui::render {

std::filesystem::path splash_image_path() {
    return splash_image_path_impl();
}

void draw_splash(std::uint64_t texture, int image_width, int image_height) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    /*
     * The foreground draw list rather than a window. ImGui owns the display
     * order of windows and a window submitted last is not necessarily drawn
     * last, which was where the splash first landed behind the unlock prompt.
     * The foreground list is emitted after every window and every popup, so the
     * veil is on top by construction rather than by winning an ordering rule.
     *
     * It also takes no input, for free. The store is the gate and the splash is
     * an announcement: there is no title bar, no close button and nothing to
     * click, and a pointer over it reaches whatever is behind it.
     */
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 extent(viewport->Pos.x + viewport->Size.x,
        viewport->Pos.y + viewport->Size.y);
    list->AddRectFilled(origin, extent,
        ImGui::ColorConvertFloat4ToU32(hedgr::ui::theme::base_surface()));

    if (texture != 0 && image_width > 0 && image_height > 0) {
        /*
         * The artwork is centred at its own size and only ever scaled down, so
         * the ORE mark is never stretched by a small window or blown up soft by
         * a large one. A copy that did not fit the desk would be the one case
         * the scale below handles.
         */
        const float scale = std::min(1.0f,
            std::min(viewport->Size.x / static_cast<float>(image_width),
                viewport->Size.y / static_cast<float>(image_height)));
        const ImVec2 size(static_cast<float>(image_width) * scale,
            static_cast<float>(image_height) * scale);
        const ImVec2 pos(
            viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f,
            viewport->Pos.y + (viewport->Size.y - size.y) * 0.5f);
        list->AddImage(static_cast<ImTextureID>(texture), pos,
            ImVec2(pos.x + size.x, pos.y + size.y));
    }
}

}
