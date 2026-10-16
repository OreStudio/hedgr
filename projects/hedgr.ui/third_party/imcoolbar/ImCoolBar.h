/*
MIT License

Copyright (c) 2024-2026 Stephane Cuillerdier (aka Aiekick)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

// ImCoolBar, v1.0.0
// macOS Dock-style magnification bar for Dear ImGui

#define IMCOOLBAR_VERSION "1.0.0"
#define IMCOOLBAR_VERSION_NUM 10000

#ifndef IMCOOLBAR_API
#define IMCOOLBAR_API
#endif

#include <stdbool.h>
#include <stddef.h>

typedef int ImCoolBarFlags;                //
enum ImCoolBarFlags_ {                     //
    ImCoolBarFlags_None = 0,               //
    ImCoolBarFlags_Horizontal = (1 << 0),  //
    ImCoolBarFlags_Vertical = (1 << 1),    //
};

#ifdef __cplusplus

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif  // IMGUI_DEFINE_MATH_OPERATORS

#ifdef IMGUI_INCLUDE
#include IMGUI_INCLUDE
#else  // IMGUI_INCLUDE
#include <imgui_internal.h>
#endif  // IMGUI_INCLUDE

// #define ENABLE_IMCOOLBAR_DEBUG

namespace ImGui {

struct ImCoolBarSettings {
    ImVec2 anchor{0.5f, 0.5f};
    float normalSize{40.0f};
    float hoveredSize{150.0f};
    float animStep{0.05f};
    float effectStrength{0.5f};
    ImCoolBarFlags mode{ImCoolBarFlags_Horizontal};
    ImCoolBarSettings(const ImVec2& aAnchor = ImVec2(0.5f, 0.5f),
                      const float aNormalSize = 40.0f,
                      const float aHoveredSize = 150.0f,
                      const float aAnimStep = 0.05f,
                      const float aEffectStrength = 0.5f,
                      const ImCoolBarFlags aMode = ImCoolBarFlags_Horizontal)
        : anchor(aAnchor), normalSize(aNormalSize), hoveredSize(aHoveredSize), animStep(aAnimStep), effectStrength(aEffectStrength), mode(aMode) {
    }
};

// Check that version and struct sizes match between compiled ImCoolBar code and caller.
#define IMCOOLBAR_CHECKVERSION() ImGui::CoolBarDebugCheckVersion(IMCOOLBAR_VERSION, sizeof(ImGui::ImCoolBarSettings))

IMCOOLBAR_API bool CoolBarDebugCheckVersion(const char* aVersion, size_t aSettingsSize);
IMCOOLBAR_API bool BeginCoolBar(const char* aLabel,
                                const ImCoolBarSettings& arSettings = {},
                                ImGuiWindowFlags aWinFlags = ImGuiWindowFlags_None);
IMCOOLBAR_API void EndCoolBar();
IMCOOLBAR_API bool CoolBarItem();
IMCOOLBAR_API float GetCoolBarItemWidth();
IMCOOLBAR_API float GetCoolBarItemScale();
IMCOOLBAR_API void ShowCoolBarMetrics(bool* apoOpen);
IMCOOLBAR_API void ShowCoolBarDemoWindow(bool* apoOpen = nullptr, ImCoolBarSettings* apoSettings = nullptr, ImCoolBarSettings* apoDefaultSettings = nullptr);

}  // namespace ImGui

#endif  // __cplusplus

/////////////////////////////////////////////////
////// C LANG API ///////////////////////////////
/////////////////////////////////////////////////

#ifdef __cplusplus
#define IMCOOLBAR_C_API extern "C" IMCOOLBAR_API
#else  // __cplusplus
#define IMCOOLBAR_C_API
#endif  // __cplusplus

typedef struct ImCoolBar_Settings {
    float anchorX;
    float anchorY;
    float normalSize;
    float hoveredSize;
    float animStep;
    float effectStrength;
} ImCoolBar_Settings;

IMCOOLBAR_C_API const char* ImCoolBar_GetVersion(void);
IMCOOLBAR_C_API int ImCoolBar_GetVersionNum(void);
IMCOOLBAR_C_API bool ImCoolBar_DebugCheckVersion(const char* aVersion, size_t aSettingsSize);
IMCOOLBAR_C_API bool ImCoolBar_BeginCoolBar(const char* aLabel, const ImCoolBar_Settings* apSettings, int aWinFlags);
IMCOOLBAR_C_API void ImCoolBar_EndCoolBar(void);
IMCOOLBAR_C_API bool ImCoolBar_CoolBarItem(void);
IMCOOLBAR_C_API float ImCoolBar_GetCoolBarItemWidth(void);
IMCOOLBAR_C_API float ImCoolBar_GetCoolBarItemScale(void);
IMCOOLBAR_C_API void ImCoolBar_ShowCoolBarMetrics(bool* apoOpen);
IMCOOLBAR_C_API void ImCoolBar_ShowCoolBarDemoWindow(bool* apoOpen, ImCoolBar_Settings* apoSettings, ImCoolBar_Settings* apoDefaultSettings);
