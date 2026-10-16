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
#include "hedgr.ui/widget/controls.hpp"
#include "imgui.h"
#include <catch2/catch_all.hpp>
#include <string>
#include <string_view>

/*
 * The copy control itself cannot be pressed here, because pressing it needs a
 * frame and a frame needs a rendering backend. What is tested instead is the
 * step between the value and the clipboard, which is where a regression would
 * hide: the button is three lines of ImGui around this call. The context is
 * stood up without a backend, which is enough for SetClipboardText, and the
 * window system's clipboard is replaced by a string this file can read.
 */

namespace {

/** @brief Where the backend's clipboard write lands, for the test to read. */
std::string written;

/** @brief The backend ImGui calls instead of the window system's clipboard. */
void capture_clipboard(ImGuiContext*, const char* text) {
    written = text == nullptr ? std::string{} : std::string{text};
}

/**
 * @brief A live ImGui context whose clipboard is the string above.
 *
 * A context is all SetClipboardText needs. NewFrame is deliberately not called,
 * because that is the call that needs an atlas and a backend and would abort.
 *
 * The callback is installed on the platform io rather than on the io. ImGui
 * 1.92 routes the clipboard through the platform backend, which is the pair of
 * functions the GLFW backend fills in at start up, and the older io callbacks
 * are no longer consulted by SetClipboardText.
 */
struct clipboard_context final {
    clipboard_context() {
        ImGui::CreateContext();
        ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        platform.Platform_SetClipboardTextFn = &capture_clipboard;
        written.clear();
    }
    ~clipboard_context() { ImGui::DestroyContext(); }
};

}

TEST_CASE("a copied value reaches the clipboard whole", "[controls]") {
    clipboard_context context;

    REQUIRE(hedgr::ui::widget::copy_to_clipboard("nats://manta:20405"));
    REQUIRE(written == "nats://manta:20405");
}

TEST_CASE("a copied failure sentence keeps its whole text", "[controls]") {
    clipboard_context context;

    /*
     * The value the pane most needs to be copyable is the one that is too long
     * for its cell and wraps, so the copy is checked against a sentence rather
     * than a word: nothing may be trimmed at the copy.
     */
    constexpr std::string_view sentence =
        "iam.v1.ops.bootstrap_status: in bootstrap mode = false, has tenant = "
        "true, version = capture build";
    REQUIRE(hedgr::ui::widget::copy_to_clipboard(sentence));
    REQUIRE(written == sentence);
}

TEST_CASE("an empty value is not put on the clipboard", "[controls]") {
    clipboard_context context;
    written = "something the operator already had";

    /*
     * A field the model does not hold draws no row, so this should not be
     * reachable from the pane, but the guard is what stops a copied absence
     * from quietly clearing the clipboard the operator was using.
     */
    REQUIRE_FALSE(hedgr::ui::widget::copy_to_clipboard(""));
    REQUIRE(written == "something the operator already had");
}
