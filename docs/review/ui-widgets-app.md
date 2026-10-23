# Review: hedgr.ui (widget, theme, icon, sound, image), hedgr.imgui, build setup

Scope: projects/hedgr.ui excluding src/render, projects/hedgr.imgui, top-level
CMake, CMakePresets.json, vcpkg.json, build/cmake/modules. Every finding was
checked against the code. ImGui is 1.92.9b (vcpkg_installed/x64-linux/include/imgui.h:32).
OreStudio reference: /home/marco/Development/OreStudio/ores_dev_solid_dirac only.

Counts: 1 high, 9 med, 16 low (26 total). Of these, 3 are OreStudio reuse or
consistency findings, tagged [ores].

---

## UW-1 (high). Drag payload memcpys a struct holding std::string
- File: projects/hedgr.ui/src/widget/browser.cpp:228-234, 731-735, 739-745
- Defect: `drag_payload_data` contains a `std::string id`, and `SetDragDropPayload(drag_payload, &payload, sizeof(payload))` byte-copies it into ImGui's buffer, so the drop target reads a `std::string` whose pointer refers to a destroyed stack local (SSO buffer) or to freed heap memory.
- Failure: an operator drags folder A onto folder B. On the drop frame `accept_drop` copies `dragged.id` from ImGui's copy. The source `payload` local died at the end of the source frame's `if` block. A UUID id (36 chars) is on the heap and has already been freed: this is a use-after-free. A short id points at a dead stack slot, so the copy reads garbage. The result is a wrong reparent command, a crash, or ASan noise. The `DataSize == sizeof(drag_payload_data)` check passes, so nothing guards against it.
- Fix: make the payload trivially copyable. Use either a fixed `char id[N]` plus a length and the kind, or an index into `model.folders`/`model.leaves` that is valid for this frame. Add `static_assert(std::is_trivially_copyable_v<drag_payload_data>)` beside the struct.
- Size: S

## UW-2 (med). No warning flags at all; -Werror only guards the compiler defaults
- File: projects/CMakeLists.txt:23, 27-29; CMakePresets.json (no flags in any preset)
- Defect: `${flags}` is never set anywhere, so `CMAKE_CXX_FLAGS` gains nothing. build.ninja for application.cpp shows `FLAGS = -g -std=c++23 -Werror`, with no -Wall, -Wextra, -Wshadow or -Wconversion.
- Failure: shadowing, sign-compare and narrowing warnings never fire. Examples are `static_cast<unsigned int>(capture_frame_)` compared with an int frame count, and int*int pixel byte counts in svg_raster.cpp. UW-1 is the kind of bug -Wclass-memaccess (gcc) can surface. The build looks warning-clean only because nothing is enabled.
- Fix: set `flags` (or better, `add_compile_options`) to `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor` for GNU/Clang and `/W4` for MSVC. Mark OreStudio and vcpkg include dirs SYSTEM so their headers do not trip -Werror. Expect a cleanup pass first.
- Size: M

## UW-3 (med). No sanitizer preset or option
- File: CMakePresets.json (all configure presets); CMakeLists.txt
- Defect: no preset or option enables ASan, UBSan or TSan. The design depends on cross-thread channels (event channel, signal bus, sound queue, market SPSC ring), and nothing exercises them under a sanitizer.
- Failure: UW-1 and any data race on the channels ship unnoticed. The sound tests (sound_tests.cpp) check that work happens off the caller's thread but never run under TSan.
- Fix: add `linux-clang-asan` and `linux-clang-tsan` presets that inherit linux-clang-debug and set `CMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer` (and thread respectively), plus matching test presets. The OreStudio libraries are prebuilt and uninstrumented, so TSan may need suppressions for them.
- Size: S

## UW-4 (med). Frame loop keeps drawing at full rate while the window is minimised
- File: projects/hedgr.imgui/src/app/application.cpp:680-838 (loop), 385 (swap interval)
- Defect: the loop never checks `GLFW_ICONIFIED` or a zero framebuffer size. When iconified it still builds the whole ImGui frame for every window, renders it and calls `glfwSwapBuffers`. The only pacing is vsync on swap.
- Failure: on X11 and several drivers, swap does not block while the window is iconified or occluded, so a minimised Hedgr with hundreds of windows spins a core at 100% and does all ImGui/ImPlot work for nothing. Upstream ImGui's GLFW example guards exactly this with `if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) { ImGui_ImplGlfw_Sleep(10); continue; }`. The code comment at 651-658 already admits that a swap can stop pacing on Wayland.
- Fix: when iconified, or when the framebuffer is 0x0, still call `manager_.pump()` and drain the signal bus so channels and rings do not back up, but skip NewFrame through swap and wait with `glfwWaitEventsTimeout(0.05)`. Optionally cap frame rate with a timed wait when the swap returns faster than the refresh interval.
- Size: S

## UW-5 (med) [ores]. --nats-wire-format is parsed and then silently dropped
- File: projects/hedgr.imgui/src/app/application.cpp:122-129
- OreStudio: /home/marco/Development/OreStudio/ores_dev_solid_dirac/projects/ores.nats/include/ores.nats/config/nats_options.hpp (`wire_format format`), ores.nats/src/config/nats_configuration.cpp:36,61-85 (`nats-wire-format`), ores.nats/include/ores.nats/domain/wire_format.hpp (`to_string`, `parse_wire_format`)
- Defect: the seed copies url, prefix and the three TLS paths out of `ores::nats::config::nats_options` but not `format`. `session_options::wire_format` keeps its hard-coded `"msgpack"` default.
- Failure: a first run against a deployment configured for JSON (`--nats-wire-format json` or the HEDGR_ env equivalent) seeds an instance row with msgpack. Every request is then encoded in a format the server does not decode, and the CLI flag the parser accepted has no effect.
- Fix: `seed.wire_format = ores::nats::to_string(cfg_.connection->format);`. Longer term, carry `ores::nats::wire_format` in `session_options` instead of a free string, and use `parse_wire_format` where the instances window edits it, so Hedgr cannot spell a format the server lacks.
- Size: S

## UW-6 (med). Untrusted tenant images are decoded synchronously and without bounds on the frame thread
- File: projects/hedgr.imgui/src/app/image_texture_cache.cpp:28-57 (ensure), called from get() during the draw
- Defect: `stbi_load_from_memory` and `lunasvg::Document::loadFromData` run inside `get()` on the first draw of each image. No dimension check happens first, and the whole decode is on the frame loop.
- Failure: a tenant image of 16000x16000 (1 GB RGBA), or a pathological SVG, stalls the frame for seconds or OOMs the client. Several new currencies appearing at once decode in one frame and hitch it. This breaks the ADR 2 rule that nothing blocking runs on the frame loop. The cache budget (64 MB) is checked only after decode, and a pinned oversized entry is never evicted.
- Fix: call `stbi_info_from_memory` first and reject images above a cap (for example 1024x1024). Better, decode in the runtime worker that fetched the bytes and hand finished RGBA to the frame thread, so the frame only uploads. Cap uploads per frame.
- Size: M

## UW-7 (med). Browser list builds and draws every row every frame, with no clipper
- File: projects/hedgr.ui/src/widget/browser.cpp:1001, 1029-1156
- Defect: `draw_list` iterates every kept leaf, calls `row_cells` and `row_labels` (both allocate strings and vectors) and submits every cell, even for rows scrolled far out of the ScrollY table. `row_labels.clear()` destroys the inner vectors, so they are reallocated per row. `visible_leaf_indices` allocates a fresh vector each frame.
- Failure: with a few hundred instances or accounts, each open browser costs hundreds of string allocations and full ImGui submission per frame, multiplied by every open Accounts window. The cost grows linearly with data while only about 20 rows are visible.
- Fix: wrap the row loop in `ImGuiListClipper` (rows have a fixed height, `GetFrameHeight()`). Call `row_cells` and `row_labels` only for clipped-in rows. Keep `kept` and the inner label vectors as reused scratch (clear per row, not destroy).
- Size: S

## UW-8 (med) [ores]. Four hand-rolled /proc/self/exe lookups; two of them break the non-Linux presets
- File: projects/hedgr.imgui/src/app/application.cpp:444-455; projects/hedgr.ui/src/icon/fonts.cpp:41-61; projects/hedgr.ui/src/sound/player.cpp:61-81; projects/hedgr.ui/src/render/menu_bar.cpp (logo_svg_path, since the splash lookup was removed)
- OreStudio: /home/marco/Development/OreStudio/ores_dev_solid_dirac/projects/ores.platform/include/ores.platform/process/executable.hpp (`ores::platform::process::executable_path()`, implemented for Linux, macOS and Windows in ores.platform/src/process/executable.cpp:41-52)
- Defect: `executable_directory()` plus `first_existing()` are duplicated verbatim in three files, and application.cpp has a fourth variant. That variant uses the throwing `fs::read_symlink` overload and passes `path.c_str()` to `stbi_load`.
- Failure: CMakePresets.json ships windows-msvc-* and macos-clang-* presets. On macOS `/proc/self/exe` does not exist, so `read_symlink` throws `filesystem_error` out of `initialise_graphics`, and the app exits before the first frame. On Windows `path::c_str()` is `const wchar_t*` and `stbi_load` does not compile. The asset candidate lists also drift between copies.
- Fix: link `ores::platform` into hedgr.ui (the per-library target already exists in FindOreStudio.cmake:150). Add one `hedgr::ui::asset_path(subdir, name)` helper on top of `executable_path()` with the shared candidate rule. Delete the copies, and route application.cpp's background lookup through it as well.
- Size: S

## UW-9 (med) [ores]. OreStudio headers and libraries come from two independently cached trees, with no consistency check
- File: build/cmake/modules/FindOreStudio.cmake:44-62, 73-86; CMakeLists.txt:173
- Defect: `ORES_SOURCE_DIR` (headers, globbed) and `ORES_BUILD_DIR` (shared libraries) are separate cache variables, and nothing verifies that the build dir was configured from the source dir or from the pinned submodule commit. The default `external/OreStudio` is an empty, uninitialised submodule here.
- Failure: Hedgr's own caches already point at different OreStudio trees per preset. build/output/linux-clang-debug/CMakeCache.txt has ORES_BUILD_DIR under ores_dev_solid_dirac, and build/output/linux-clang-release/CMakeCache.txt points at a different worktree. Setting one variable but not the other compiles Hedgr against headers whose struct layouts or protocol types differ from the linked .so. The result is an ODR/ABI mismatch that only fails at run time, for example as a message that decodes wrong.
- Fix: derive one from the other (`ORES_SOURCE_DIR` read from `${ORES_BUILD_DIR}/CMakeCache.txt` CMAKE_HOME_DIRECTORY), or fail at configure time when they disagree. Optionally compare the source tree's `git rev-parse HEAD` with the submodule pin and warn.
- Size: S

## UW-10 (med). hedgr.imgui has no unit tests, and several pure ui helpers are untested
- File: projects/hedgr.imgui/tests/CMakeLists.txt:18-21 (no sources, "verified by HEDGR_CAPTURE"); projects/hedgr.ui/tests/ (no label_pill, svg_raster, fonts, panels or password_meter tests)
- Defect: logic that can be tested without GL has no tests. In hedgr.imgui that is config parsing and database path resolution (parser.cpp, with XDG_DATA_HOME, HOME and HEDGR_SOURCE_DIR branches) and the LRU eviction and pinning in image_texture_cache::end_frame. In hedgr.ui it is `pills_that_fit`/`pill_fits`, `split_labels`/`join_labels`, `split_host_port`, `rasterize_svg` channel order and unpremultiply, and `read_icon_sheet` truncation handling.
- Failure: a regression in eviction (for example evicting a texture pinned this frame, which ImGui then samples after deletion) or in BGRA/RGBA order in svg_raster shows up only as a wrong pixel in a capture, if a capture happens to cover it.
- Fix: add hedgr.imgui.tests for the parser and for the cache's bookkeeping, with GL calls behind a seam: inject upload and delete functions, like the sound player's `sink_fn`. Add Catch2 cases in hedgr.ui for the pure helpers listed.
- Size: M

## UW-11 (low). label_pill_row allocates per pill per frame, even when nothing is hovered
- File: projects/hedgr.ui/src/widget/label_pill.cpp:239-242, 270-275, 283-290; 149-162 (pills_for)
- Defect: every call allocates the `widths` vector, a `std::format` tooltip string for each pill (built whether or not the pill is hovered), the `hidden` join and the `+N` string. Hosts build `pills_for` vectors that copy label names each frame.
- Failure: a list of 200 rows with three labels each does about 800 or more heap allocations per frame per browser, for tooltips nobody sees.
- Fix: build the tooltip only when `ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)` (move `SetItemTooltip` into a hover check in `label_pill_draw`, passing the label and severity rather than a preformatted string). Reuse a scratch widths buffer.
- Size: S

## UW-12 (low). image_texture_cache::get builds the tenant key twice per draw
- File: projects/hedgr.imgui/src/app/image_texture_cache.cpp:72-79
- Defect: on every cache hit, `make_tenant_key` (tenant_key.hpp:35) runs twice. The key is a UUID plus a separator plus a code, which is longer than SSO, so each call heap-allocates.
- Failure: every currency flag drawn costs two allocations per frame. A CRM or grid window showing 50 flags across 20 windows makes 2000 allocations per frame on the hot path.
- Fix: compute the key once into a reused member `std::string` scratch and `find` once, inserting via `ensure` on a miss. Or key the container on `(tenant_id, image_id)` with a compatible-key hash so a `string_view` pair can be looked up.
- Size: S

## UW-13 (low). render_input copies three std::string fields per frame
- File: projects/hedgr.imgui/src/app/application.cpp:755, 758-759; projects/hedgr.ui/include/hedgr.ui/render/renderer.hpp (render_input: lock_message, startup_failure, runtime_error)
- Defect: the getters return `const std::string&`, but `render_input` stores by value, so each frame copies them.
- Failure: while an error is shown (long sentences, so heap), every frame allocates three strings. This is small but on the per-frame path the ADRs keep allocation-free.
- Fix: make the three fields `std::string_view`, which is safe because `manager_` outlives the frame.
- Size: S

## UW-14 (low). Sound queue is unbounded and sounds are not coalesced
- File: projects/hedgr.ui/src/sound/player.cpp:255-282 (submit), 192-210 (run)
- Defect: `pending` grows without limit, and each entry becomes a separate overlapping `ma_engine_play_sound`.
- Failure: twenty instances flipping readiness on the same poll (a broker restart) queue 20 error sounds that play as one loud burst, and a fast pointer sweep across the shortcut bar queues a click per entry. ADR 8 calls out "a busy deployment can raise a signal on every poll" as the noise risk.
- Fix: in `submit`, de-duplicate by file within one drain (play a file at most once per frame) and cap `pending` (drop when more than N). Optionally rate-limit per file (for example 150 ms).
- Size: S

## UW-15 (low). player cannot be restarted after stop()
- File: projects/hedgr.ui/src/sound/player.cpp:285-298 (stop), 237-253 (start)
- Defect: `stop()` sets `stop_requested = true` and never resets it. A later `start()` launches a worker that returns at once, yet `started()` reports true.
- Failure: any future "mute and unmute" or device-reopen path calls stop then start, after which every `submit` queues into a dead worker. Sounds stay silent and the queue grows without bound (compounding UW-14).
- Fix: reset `stop_requested = false` (and clear `pending`) in `start()` before spawning, or make the player single-use and assert on a second start.
- Size: S

## UW-16 (low). pack.hpp documents behaviour the player does not implement
- File: projects/hedgr.ui/include/hedgr.ui/sound/pack.hpp:46-52; projects/hedgr.ui/src/sound/player.cpp:237-253
- Defect: the comment says "the player opens no output device when [the table] is empty, so this function is the one place sound is switched on or off". `player::start()` never consults `registrations()` and always calls `ma_engine_init`.
- Failure: someone silences Hedgr by emptying the table, as documented. The audio device is still opened, which takes an exclusive ALSA device on some hosts and keeps PulseAudio and PipeWire streams alive.
- Fix: in `start()`, return without opening the device when `registrations().empty()`, or delete the claim from the comment.
- Size: S

## UW-17 (low). HEDGR_CAPTURE_FRAME parsed with atoi; a bad value leaves a hidden window running forever
- File: projects/hedgr.imgui/src/app/host.cpp:94; projects/hedgr.imgui/src/app/application.cpp:825-826
- Defect: a non-numeric, zero or negative value becomes a frame number that `GetFrameCount()` never equals.
- Failure: `HEDGR_CAPTURE_FRAME=abc` in a CI capture starts an invisible window (GLFW_VISIBLE false) that never writes the frame and never exits, so the job hangs until it times out.
- Fix: parse with `std::from_chars` and reject values below 1 with an error and a non-zero exit. Optionally add a hard frame ceiling for capture runs.
- Size: S

## UW-18 (low). An exception in the frame loop skips saving the window wall and the market view
- File: projects/hedgr.imgui/src/app/application.cpp:267-275, 631-632
- Defect: `save_windows`, `save_market_view`, `manager_.stop()` and `shutdown_graphics` run only on normal loop exit. One example that throws: `fs::create_directories` (throwing overload) in `capture_frame`.
- Failure: any exception thrown mid-frame (a filesystem error, a bad_alloc from UW-6) unwinds to host.cpp and main and loses the session's window layout and chart preferences. GL and ImGui contexts are not torn down in order.
- Fix: put shutdown in a scope guard (BOOST_SCOPE_EXIT is already used in main.cpp) or a small RAII type around `render_loop`. Use the `error_code` overload in `capture_frame`.
- Size: S

## UW-19 (low). imgui.ini lives in the current working directory
- File: projects/hedgr.imgui/src/app/application.cpp:407-411; assets/applications/org.hedgr.Hedgr.desktop (Exec with no Path)
- Defect: `IniFilename = "imgui.ini"` resolves against the CWD, while the store and market-view.ini live under the XDG data dir.
- Failure: launched from the desktop entry (CWD is $HOME), Hedgr writes ~/imgui.ini. Launched from a terminal in the repo, it uses a different file. Window layout then changes depending on how the app was started, and stray imgui.ini files appear in whatever directory it was run from.
- Fix: default to `cfg_.database_path.parent_path() / "imgui.ini"` (beside market-view.ini) and keep the HEDGR_IMGUI_INI override.
- Size: S

## UW-20 (low). Icons are baked at one size and nothing handles content scale
- File: projects/hedgr.ui/src/icon/fonts.cpp:302-333; projects/hedgr.imgui/src/app/application.cpp:424
- Defect: icon glyphs are custom rects registered only for `base_size_px` (15) at a fixed 16 px (`glyph_pixel_size`). No code reads `glfwGetWindowContentScale` or sets `style.FontScaleDpi` or `FontScaleMain` (grep: zero hits in hedgr.ui and hedgr.imgui).
- Failure: on a 4K X11 display the whole UI renders at 1x, so text is tiny. If any font size other than 15 draws an icon string (any `PushFont(nullptr, size)`, or a later DPI change), ImGui 1.92 bakes that size fresh, the custom glyphs are absent, and icons draw as missing-glyph boxes. The label_pill comment (label_pill.cpp:51-53) assumes DPI factors are applied, but none are.
- Fix: set `style.FontScaleDpi` from the primary monitor's content scale at startup (and from the content-scale callback). Register icons through a font loader (an ImFontLoader, or a merged icon TTF) so they bake at any size instead of as a fixed-size custom rect.
- Size: M

## UW-21 (low). FindOreStudio gives every per-library target the whole OreStudio include set
- File: build/cmake/modules/FindOreStudio.cmake:134-180
- Defect: each `ores::<lib>` target sets `INTERFACE_INCLUDE_DIRECTORIES` to `_ores_include_dirs`, which is every OreStudio header. Libraries are linked by bare name through `INTERFACE_LINK_DIRECTORIES`.
- Failure: hedgr.ui links only `ores::logging`, yet it can `#include "ores.database/..."` and compile cleanly. It then fails only at link time, or not at all if the symbol is inline. The comment's promise that "hedgr.ui does not drag in the database" is not enforced. The headers are also not SYSTEM, so turning on warnings (UW-2) would fail on OreStudio's headers.
- Fix: map each library to its own `projects/<pkg>/include` (or the nested component dir), add the true inter-library dependencies, mark them `SYSTEM`, and use `IMPORTED_LOCATION` with full paths instead of link directories.
- Size: M

## UW-22 (low). hedgr.imgui over-links: hedgr.store.lib and all of OreStudio
- File: projects/hedgr.imgui/src/CMakeLists.txt:43-56
- Defect: the executable links `hedgr.store.lib` directly, although no hedgr.imgui source includes a hedgr.store header (grep: none). It also links `ores::client`, which is every OreStudio library including ores.database and telemetry.
- Failure: the ADR 1 graph (imgui depends on runtime and ui) is not what CMake states. A store type can leak into the executable without anyone noticing, and the bundle carries libraries the binary does not need.
- Fix: drop `hedgr.store.lib`. Let hedgr.runtime and hedgr.session carry their dependencies PUBLIC where their headers expose them. Replace `ores::client` with the narrow `ores::nats` and `ores::logging` targets parser.cpp actually uses, plus `ores::utility` for environment_mapper_factory.
- Size: S

## UW-23 (low). stb is used but never found or declared as a dependency
- File: projects/hedgr.imgui/src/app/application.cpp:41; projects/hedgr.imgui/src/app/image_texture_cache.cpp:24; projects/hedgr.imgui/src/CMakeLists.txt:43-56
- Defect: `<stb_image.h>` is reached only because the vcpkg include dir rides along on other imported targets. No `find_path(STB_INCLUDE_DIRS stb_image.h)` exists, and the include path is not attached to the target.
- Failure: a change that narrows another target's includes (UW-21) or a non-vcpkg build loses stb with a confusing missing-header error.
- Fix: `find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)` and `target_include_directories(hedgr.imgui.exe SYSTEM PRIVATE ${STB_INCLUDE_DIRS})`.
- Size: S

## UW-24 (low). Texture cache never refreshes a changed image and keeps failures forever
- File: projects/hedgr.imgui/src/app/image_texture_cache.cpp:22-26, 50-53
- Defect: entries and `failed_` are keyed by (tenant, image id) only, not by content. `failed_` is cleared only in `clear()` at shutdown.
- Failure: if a tenant replaces a currency image under the same id, the old texture stays for the life of the process. An image whose first bytes failed to decode (for example a truncated fetch later re-fetched correctly) is never retried.
- Fix: key on (tenant, id, byte-size or hash), or have the runtime bump a version in `image_cache` that the texture cache compares. Drop the `failed_` entry when the bytes change.
- Size: S

## UW-25 (low). status_foot copies its strings every frame for every window
- File: projects/hedgr.ui/src/widget/status_foot.cpp:60-69, 283
- Defect: `build_status_text` copies `state.as_of` and `state.word` into a new `status_text` on each draw and appends " · updating".
- Failure: every market window draws a foot each frame, so hundreds of windows mean hundreds of small allocations per frame for text that changes about once a second.
- Fix: draw `as_of`, the updating suffix and `word` as separate `TextUnformatted` and `SameLine` pieces from `string_view`s, measuring each with `CalcTextSize(begin, end)`. No concatenation is needed.
- Size: S

## UW-26 (low). Version string is hard-coded and drifts from CMake
- File: projects/hedgr.imgui/src/config/parser.cpp:38; CMakeLists.txt (project VERSION 0.0.1)
- Defect: `"Hedgr ImGui Client v0.0.1"` is a literal and not `PROJECT_VERSION`.
- Failure: the next version bump in CMake leaves `--version` and the help banner reporting 0.0.1, and the package name (`hedgr-${CMAKE_PROJECT_VERSION}-...`) disagrees with the binary.
- Fix: add `target_compile_definitions(hedgr.imgui.exe PRIVATE HEDGR_VERSION="${PROJECT_VERSION}")` and use it in parser.cpp.
- Size: S

---

Checked and not reported: the event drain order (pump runs after NewFrame and before draw, and the signal bus is drained after draw, which is correct); GL resource release order in shutdown_graphics (textures before contexts); sound player join before device teardown (correct); capture hooks inert without a script (they return early on `!active`); `card_columns` clamps to at least 1, so `card_slot`'s modulo cannot divide by zero; config parsing already reuses ores.logging and ores.nats option descriptions and the environment mapper.
