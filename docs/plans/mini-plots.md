# Mini plots and docking

How a chart becomes a small tile that sits in a grid with other tiles, and back
into a big floating window, in steps that each land on their own with the suite
green.

The request is to be able to build a desk like
`docs/samples/multi-chart-and-tools-e1732517002809.png`: a 2x2 grid of compact
charts, each with a one-line header (name, interval, venue, OHLC, last price), a
price axis on the right with a last-price tag, minimal chrome, the active tile
outlined, and a crosshair shared across tiles. `DXcharts-Tools-Multichart-and-Compare-Charts-Features.png`
shows the same idea at 4x2, and `Trading-Screen-Setup-for-Scalping.png` shows a
chart docked beside a depth ladder and a time-and-sales list, which are not
charts.

Line references are to `main` at `e030995`. A claim marked "inferred" was read
off the code or a header and not observed running. A claim marked "verified"
was checked against a source file named beside it. Step 1 has since been built
and its suite run; nothing here was run with a display.

The user decided D1 to D10 on 2026-10-22 (see "Decisions"). All went as
recommended except D9: tearing a window out into its own OS window is in.

## Why

Hedgr's windows float. Every window is placed and sized by hand, and a desk of
eight charts is eight windows the operator lines up by eye. There is no way to
say "these four belong together, split the space between them". The chart
window is also drawn for one size only: a header of pills, a plot, and a foot of
three combos and a status line. At a quarter of a screen the chrome takes a
large share of the height.

Two things are missing. A container that tiles windows (the "dock control"),
and a chart presentation that reads well at tile size (the "small plot").

## What exists today

### How a window is modelled

- `window_kind` is a closed enum of ten kinds
  (`hedgr.model/domain/window.hpp:44-75`). The comment at `:40-42` states the
  rule: adding a kind adds a variant alternative, a renderer table entry and a
  menu entry.
- `chart_state` carries a label, a series id and a dead `max_points`
  (`window.hpp:86-95`). It does not carry the period, the style or the EMA.
- `window_spec` is the persisted shape: id, kind, session, cadence, state
  (`window.hpp:206-221`). It has no position, size or layout field.
- `window_table` is a structure of arrays with a hot block (kind, open, session,
  refresh deadline) and a cold block (id, cadence, state, saved session)
  (`hedgr.model/state/tables.hpp:550-579`). Each row holds a *list* of series
  handles, written only through `set_series` (`tables.hpp:572-578`, `:645-649`).
- The store writes one row per window: kind, session columns, cadence and the
  state as JSON (`hedgr.store/repository/window_repository.cpp`, `to_entity`
  and `to_domain`). State JSON is read with `rfl::DefaultIfMissing`
  (`hedgr.model/src/domain/window_json_io.cpp:45`), so a field added to
  `chart_state` reads as its default from an old row (verified by reading).

### How a chart window is identified and fed

- A chart's id is derived from what it shows,
  `chart:<instance>:<account>:<series>` (`hedgr.runtime/src/service/window_service.cpp:156-165`).
  Opening the same series again focuses the existing window
  (`window_service.cpp:309-317`). So today a desk cannot hold two charts of one
  series in one session, for example a 1m and a 1h tile of EUR/USD.
- `hold_series` interns the series, acquires before it releases, and stores one
  handle on the window (`window_service.cpp:185-202`). The series table counts
  watchers per row (`hedgr.model/state/series_table.hpp:222-231`), and the
  runtime subscribes once however many windows watch (architecture principle
  5).

### How the desk is drawn and laid out

- `workspace::render` walks the window slab, culls closed windows and windows
  the lock hides, and calls `ImGui::Begin` for each survivor
  (`hedgr.ui/src/render/workspace.cpp:253-402`). A collapsed or clipped window
  returns false from `Begin` and submits nothing (`:402-411`).
- The ImGui id is pinned to the stable window id after `###`
  (`workspace.cpp:336-349`), so a retitled window keeps its place.
- Every window has a minimum size of 360x220 (`workspace.cpp:351-352`) and a
  first-use size per kind, 1000x620 for a chart (`workspace.cpp:363-372`,
  `ImGuiCond_FirstUseEver`).
- Dispatch is the renderer table indexed by kind (`workspace.cpp:447-449`,
  `renderer.hpp:661-672`), as ADR 0002 requires.

### Where geometry is saved

Window position and size are not in the store. ImGui writes them to `imgui.ini`
keyed by the `###` id, and Hedgr points `io.IniFilename` at that file
(`hedgr.imgui/src/app/application.cpp:430-441`). The default path is relative
to the current directory, which review finding UW-19 calls out
(`docs/review/ui-widgets-app.md:141-145`) and which is still the code at
`application.cpp:437`. So the precedent already exists: the store says which
windows exist and what they show, and ImGui's own file says where they are.

### The chart window

- `render_chart` (`hedgr.ui/src/render/chart_window.cpp:217`) reads the held
  series, asks for the level `market_level_for_span(span)`
  (`chart_window.cpp:255`), and rebuilds its candle and EMA cache only when the
  key of version, span, style and EMA changes (`chart_window.cpp:261-266`,
  `chart_plot.hpp` `chart_plot_key`).
- The header is one row: a name pill, a headline price, a change pill in the
  data-health tone, a points caption, history notes and flags
  (`chart_window.cpp:343-385`).
- The foot holds three combos, period, style and EMA (`chart_window.cpp:324-341`),
  and the status line.
- The plot is one ImPlot with the price axis on the right
  (`ImPlotAxisFlags_Opposite`, `chart_window.cpp:422`), its own crosshair
  (`ImPlotFlags_Crosshairs`, `:418`), and limits set with `ImPlotCond_Always`
  (`:451-455`, `:469-475`), so it cannot be panned or zoomed. The last price is
  a `TagY` (`:575`).
- Period, style and EMA live in the interface's per-window scratch
  (`window_ui_state::market_view`, seeded from the last window,
  `workspace.cpp:432-441`) and are saved as one global choice to
  `market-view.ini` (`application.cpp:162-164`, `:301`). A restored desk opens
  every chart on the last choice made, not on the choice each chart had.

### The ImGui build

- The vcpkg checkout pins `imgui` 1.92.9 (`external/vcpkg/ports/imgui/vcpkg.json`).
  The installed header reports `1.92.9b` and does not define `IMGUI_HAS_DOCK`
  (`build/output/linux-clang-release/vcpkg_installed/x64-linux/include/imgui.h:32-35`).
- The port has a `docking-experimental` feature that fetches the tag
  `v1.92.9b-docking` instead of `v1.92.9b`
  (`external/vcpkg/ports/imgui/portfile.cmake:4-18`). Same version, other
  branch.
- `vcpkg.json` has no `builtin-baseline` and no `overrides`, so the version is
  whatever the submodule's port says. Builds run with
  `VCPKG_MANIFEST_INSTALL=OFF` against a shared installed tree.
- `build/vcpkg-overlays` holds only `glew`. Docking needs no overlay.
- ImPlot is 1.0 (`external/vcpkg/ports/implot/vcpkg.json`). Its source has no
  docking code and gates only on `IMGUI_VERSION_NUM`, which is 19291 on both
  branches (verified against `implot.cpp` at `v1.0` and `imgui.h` at
  `v1.92.9b-docking`). So ImPlot compiles the same path on the docking build
  (inferred, not built).
- The vendored ImCoolBar already sets `ImGuiWindowFlags_NoDocking` when
  `IMGUI_HAS_DOCK` is defined (`hedgr.ui/third_party/imcoolbar/ImCoolBar.cpp:119-121`),
  so the shortcut bar stays out of docking with no change.
- The docking header at `v1.92.9b-docking` declares `DockSpaceOverViewport`,
  `SetNextWindowDockID`, `SetNextWindowClass`, `GetWindowDockID`,
  `IsWindowDocked`, `ImGuiWindowFlags_NoDocking`,
  `ImGuiDockNodeFlags_PassthruCentralNode`, `ImGuiDockNodeFlags_AutoHideTabBar`
  and `io.ConfigDockingWithShift` (verified, `imgui.h:977-997`, `:1238`,
  `:1557-1560`, `:2550`). `imgui_internal.h` there has `DockBuilderAddNode`,
  `DockBuilderSplitNode`, `DockBuilderDockWindow` and `DockBuilderFinish`
  (verified, `:3924-3937`).
- The GLFW backend disables multi-viewports under Wayland since 2025-04-26
  (verified, `backends/imgui_impl_glfw.cpp:50` at that tag). Hedgr builds GLFW
  with Wayland (`vcpkg.json`, `glfw3` feature `wayland`) and asks GLFW for
  Wayland whenever `WAYLAND_DISPLAY` is set (`application.cpp:319-323`). See
  "Multiple OS windows" for what that means for D9.

## What a small plot is

A small plot is a chart window, not a new kind. Everything a tile needs a big
chart already has: one series, one session, a period, a style, the price tag,
the data-health tone and the flags. Nothing a big chart has is wrong for a tile
either. The difference is presentation and placement, and both follow from where
the window sits.

The rule proposed is one sentence. **A chart docked into another window or into
the desk is drawn compact; a floating chart is drawn full.** The renderer reads
`ImGui::IsWindowDocked()` once and picks a layout. That is a fact of ImGui's
layout, in the same category as the window's size, so rendering stays a pure
function of the tables plus ImGui's own geometry, as it is today.

What a compact chart draws:

- One header row. Name pill, period as a small clickable caption, last price in
  the numeric font at body size (not the headline size), change pill in the
  health tone, flags. The points caption and the history notes move into the
  header's tooltip.
- No foot. Period, style and EMA move to a right-click menu on the plot and to
  the period caption. The status line's health word shows as the change pill's
  tone, which it already drives (`chart_window.cpp:183-193`).
- The plot with the right price axis and the last-price tag, as today.
- An accent outline when the window is focused, the active tile of the
  screenshot.
- No tab bar when the tile is alone in its dock node
  (`ImGuiDockNodeFlags_AutoHideTabBar` through `SetNextWindowClass`).

What a tile needs that a chart lacks today is persistence of its own period and
style. Four tiles at four periods all come back on one period after a restart,
because the choice is global (`application.cpp:162-164`). Step 2 moves
period, style and EMA into `chart_state`. The `DefaultIfMissing` read makes old
rows load with defaults.

What a big chart has that a tile drops is the foot and the points caption. They
remain one undock away.

## Designs compared

### (a) ImGui docking

Turn on the docking branch and `ImGuiConfigFlags_DockingEnable`. Any window can
be dragged onto another: ImGui shows drop targets, and dropping splits the
target left, right, up or down, or adds a tab. Splitters between docked windows
resize them. `DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode)`
adds the screen edges as targets while leaving the centre transparent, so the
background image and floating windows behave as now.

Cost.

- One feature in `vcpkg.json`, a reinstall of `imgui` and `implot` into the
  shared installed tree, one config flag, and `NoDocking` on the two
  viewport-anchored windows the workspace draws (startup notice
  `workspace.cpp:611`, lock prompt `:700`).
- ImGui's public API is unchanged except for additions. `ImGuiIO` grows, so
  everything must rebuild against the new header together;
  `IMGUI_CHECKVERSION` (`application.cpp:424`) catches a mix (inferred).
- Window size constraints do not bind a docked window (inferred from how dock
  nodes size their windows, not checked).

Persistence. The dock tree goes where geometry already goes, `imgui.ini`, as a
`[Docking]` section keyed by the same `###` ids. The store keeps saying which
windows exist. That matches today's split and adds nothing to ADR 0002: no
table changes shape, no thread touches anything new, and the frame thread
remains the only writer of the tables. A window in the ini whose row the store
dropped is ignored by ImGui; a row with no ini entry opens floating at its
first-use size (inferred, the same as today). UW-19 becomes more important,
since a lost ini now loses the arrangement as well as the positions.

The alternative is to keep the dock tree in the store: read
`SaveIniSettingsToMemory` when `io.WantSaveIniSettings` is set, push a command
carrying the text, and let the runtime write one row. The frame-cost tests
already load ImGui settings from memory (`frame_cost_tests.cpp:105`). It buys a
layout that travels with the store file. It costs a new command, a new table, a
blob nobody can query, and a second place window geometry is written. Not
recommended for the first slice.

What it gives beyond charts. A term structure, a CRM matrix, a grid or the
series browser can dock beside a chart, which is the scalping layout. That
comes free.

What it does not give. A crosshair or a time axis shared across windows. Each
docked chart is its own ImPlot. Both can be added on top (see "Shared
concerns"). Two tiles of one series still need the chart id rule to change.

### (b) A Hedgr-owned board

A new kind, `board`, whose state is a list of tiles (series id, period, style)
and a split tree, stored as JSON in the window row. The board window draws its
tiles in a grid with its own splitters, its own drag to rearrange, its own drop
targets, and its own pop-out into a chart window.

What fits well. The window row already holds a list of series handles
(`tables.hpp:572-578`), so a board holds N series with no table change, and the
layout lives in the store. Tiles inside one window can share a crosshair and an
X axis with `ImPlot::BeginSubplots` and `ImPlotSubplotFlags_LinkAllX` (verified
present, `implot.h:209`, `:823`). The same series can appear twice in one board.

What it costs.

- A new kind touches the enum, the variant, the JSON io, the renderer table, the
  traits table (`window_traits.hpp:127-131`), `window_key_for`, the menu, and
  `hold_series`, which handles charts only (`window_service.cpp:185-190`).
- Splitters, drag to rearrange, drop targets and hit testing are written and
  tested by Hedgr. That is the part of docking that took ImGui years to settle.
- Only charts can be tiles. The scalping layout stays out of reach.
- A chart pulled out of a board is a different window from the tile it was, with
  a different id, so its ImGui geometry starts fresh.

### (c) A fixed multi-chart window with ImPlot subplots

One window kind holding a fixed rows-by-columns grid drawn with
`BeginSubplots`. No drag, no splitters. It gives the screenshot's look and the
linked crosshair cheaply, but it is not dockable and does not answer the
request. It is worth keeping as a later "linked group" inside design (a) if
traders want tiles that pan and zoom together.

### Recommendation

Design (a), ImGui docking, with the compact presentation derived from being
docked. The evidence is the cost asymmetry. Docking is one vcpkg feature on the
same ImGui version, ImPlot is unaffected (verified at source), the shortcut bar
already opts out, and layout persistence follows a precedent the code already
has. Design (b) rebuilds the hardest part of docking inside one kind and still
cannot dock a ladder beside a chart.

## The dock control

What the operator sees and does.

1. **Dock by dragging.** Drag a chart's title bar over another window. Drop
   targets appear. Drop on an edge to split, on the centre to tab. Drop on a
   screen edge to dock against the desk. The docked chart turns compact.
2. **Resize by the splitter** between two tiles.
3. **Pop out.** A small icon in the compact header undocks the tile into a
   floating window at its last floating size. It turns full.
4. **Dock from a big chart.** A "Dock" icon in the foot of a floating chart
   docks it into the desk's dock space, splitting the most recently focused
   tile. This is the same move as dragging, for an operator who does not know
   the drag.
5. **Close.** The close icon on a tile closes the window, through the existing
   `close_window` command (`workspace.cpp:412-433`). With the tab bar hidden,
   the close control sits in the compact header.
6. **Active tile.** The focused tile carries an accent outline.

Minimal first slice. Items 1, 2 and the compact presentation. That alone lets
the operator build the 2x2 grid of the screenshot by dragging, and the grid
comes back on restart. Items 3 to 6 are step 4. Items 3 and 4 use
`SetNextWindowDockID` from interface scratch on the next frame, which is how
ImGui's public API docks or undocks a window by code (verified declared,
`imgui.h:994`; that dock id 0 undocks is inferred, not checked).

## Shared concerns

### Crosshair across tiles

ImPlot draws each plot's crosshair on its own. To share it, the workspace keeps
one scratch value, the time under the cursor in whichever chart plot is hovered
(`ImPlot::IsPlotHovered`, `ImPlot::GetPlotMousePos`, verified present,
`implot.h:1123`, `:1128`). Every other compact chart draws a thin vertical line
and a time tag (`ImPlot::TagX`, `implot.h:1092`) at that time. The value is
double buffered: charts draw last frame's value and write this frame's, so the
order of the window walk does not change what a frame shows. This is interface
scratch like ImGui's own hover state, not a table.

Scope is a decision (D6). The recommendation is every docked chart in the same
session.

### Shared time axis

Each tile keeps its own X axis, as in the reference screenshot. Linking axes
(`ImPlot::SetupAxisLinks`, `implot.h:869`) only matters once a plot can be
panned or zoomed, and today none can, because limits are set with
`ImPlotCond_Always` (`chart_window.cpp:451-455`). Tiles on one period already
end at their newest point and span the same width, so they line up by
construction for live series (inferred). Out of scope until pan and zoom exist.

### Which level a tile asks for

The same level a big chart asks for at that period,
`market_level_for_span(span)` (`hedgr.model/domain/market.hpp:148-157`). The
level is chosen by time span, not by pixels, and the rings are shared by every
window on the series, so a smaller tile does not save memory by asking for a
coarser level. It would save draw cost on a fine ring (up to 7200 points,
`market.hpp`) drawn into 500 pixels. ImPlot's line drawing at that count is
cheap (inferred). Revisit only if the frame-cost check in step 3 says so.

### Many tiles on one series

Cheap on the data side. Each chart window holds one handle, `acquire` counts
it (`series_table.hpp:222`), and the runtime subscribes once. On the derived
side each window owns its own `chart_plot` cache, rebuilt when the series
version changes (`chart_window.cpp:261-266`). N tiles on one series rebuild N
times per bucket, which is once a second per tile. The series-table plan
already left incremental candles open behind a benchmark (D7 there). Today the
chart id rule allows only one chart per series per session anyway (D8 here).

### Per-frame allocation

A floating chart builds a `widget::foot_state` every frame, with strings and
three pushed combos (`chart_window.cpp:225`, `:324-341`; the struct holds
`std::string` members, `status_foot.hpp:73-94`). The compact path draws no
foot, so it should allocate nothing on a steady frame. The price and change
text already go through fixed buffers (`format_price(...).view()`,
`chart_window.cpp:354-357`, inferred from the `.view()` call). The check is an
allocation-counting test in the shape of "a collapsed window costs a frame no
allocation" (`frame_cost_tests.cpp:123`).

### The look

The compact header uses the widgets of the new look: `value_pill` for the name
and the change (with `theme::tone`), `caption` for the period, and the numeric
font for the price (`hedgr.ui/widget/cells.hpp:63-92`). The theme has no named
accent token. The nearest is the selection blue, `rail_selection_border`
(`theme.hpp:310`), and the outline should reuse it or promote it to a named
accent. No new widget is needed for the first slice (inferred from the widget
list).

### Multiple OS windows (viewports)

D9 is in. A docked tile or a floating window can be dragged outside the main
window and becomes its own OS window. ImGui calls this multi-viewports
(`ImGuiConfigFlags_ViewportsEnable`). What the source at `v1.92.9b-docking`
says about this machine, a GNOME 50 Wayland session:

- The GLFW backend sets `ImGuiBackendFlags_PlatformHasViewports` only when
  `glfwGetPlatform()` is not Wayland (verified, `imgui_impl_glfw.cpp:740-749`).
  The reason is upstream issue #8587: Wayland gives a client no global window
  position, and viewports place windows by position.
- When the flag is asked for and the backend lacks support, `NewFrame` clears
  it and carries on (verified, `imgui.cpp:11789-11809`, called unconditionally
  from `NewFrame` at `:5792`). So asking for viewports on Wayland is harmless,
  and every window stays inside the main one.
- No other backend in that tag does better. The SDL3 backend grants viewports
  only for the `windows`, `cocoa`, `x11`, `DIVE` and `VMAN` video drivers, not
  `wayland` (verified, `imgui_impl_sdl3.cpp:584-596`). Switching backend would
  buy nothing.
- Secondary windows share the main window's GL context, so Hedgr's textures
  (background, icons, currency images) draw in them unchanged (verified,
  `imgui_impl_glfw.cpp:1393-1394`). Each secondary window sets swap interval 0,
  so only the main window paces the frame (verified, `:1422-1423`). The window
  identity hints set before the main window persist in GLFW and apply to the
  secondary windows too (inferred, GLFW keeps hints until reset).
- The frame loop must call `UpdatePlatformWindows` and
  `RenderPlatformWindowsDefault` after rendering and restore the main context.
  `UpdatePlatformWindows` returns at once when the flag is off (verified,
  `imgui.cpp:17689-17696`).

The options:

1. **X11 through XWayland**, `glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11)`.
   Viewports work. The costs are the general costs of an X11 client on a
   Wayland desktop: GNOME upscales X11 windows under fractional or HiDPI
   scaling, which blurs them, unless mutter's `xwayland-native-scaling` is on.
   This machine has one 1920x1080 output, no `monitors.xml` and no mutter
   experimental features, so it runs at scale 1 and the blur does not apply
   (inferred from `/sys/class/drm` and `gsettings`). On X11 the backend uses the
   content scale and reports a framebuffer scale of 1 (verified,
   `imgui_impl_glfw.cpp:1184-1188`).
2. **Native Wayland, viewports off.** Today's behaviour. Docking works inside
   the main window. No tear-out.
3. **A backend with Wayland viewports.** None exists at this tag (see above).

Chosen. Step 1 asks for viewports everywhere and adds the frame-loop calls.
`HEDGR_PLATFORM=x11` makes GLFW use X11, which turns tear-out on. The default
stays native Wayland until a person has run the X11 path once (step 6), because
nobody has yet watched a torn-out window on this desktop. The log names the
platform and whether windows can be torn out, on every launch.

### Headless tests

The headless desk sets `io.IniFilename = nullptr`
(`hedgr.ui/tests/headless_desk.hpp:65`), so tests start from no layout. A test
docks two charts with `DockBuilderAddNode`, `DockBuilderSplitNode` and
`DockBuilderDockWindow`, draws a frame, and asserts what the look tests already
assert for a chart (`window_look_tests.cpp:312-371`): which pills, which tones,
and here also that the docked chart has no foot. That covers the compact rule
without a display.

## Steps

Each step ends in a check, and each lands on its own with the suite green.

### 0. Prerequisite: `imgui.ini` beside the store (UW-19)

Default `ini_path_` to `database_path.parent_path() / "imgui.ini"`, beside
`market-view.ini`, and keep `HEDGR_IMGUI_INI`. A dock layout that moves with the
launch directory is a layout the operator loses.

Check: a unit test of the path choice, and one launch from `$HOME` and one from
the repo that open the same arrangement.

Delivered in #72, with the `vcpkg.json` version fix.

### 1. The docking build

Add `docking-experimental` to the `imgui` features in `vcpkg.json`. Reinstall
`imgui` and `implot` into the shared installed tree. Set
`ImGuiConfigFlags_DockingEnable` beside the nav flags (`application.cpp:428`).
Add `ImGuiWindowFlags_NoDocking` to the startup notice and the lock prompt.
Ask for `ImGuiConfigFlags_ViewportsEnable`, add the platform-window calls to the
frame loop, and add the `HEDGR_PLATFORM=x11` switch (see "Multiple OS
windows"). On the default Wayland platform this changes nothing visible.

Check: build and full `ctest` green. A `static_assert` that `IMGUI_HAS_DOCK` is
defined, so a tree built against the wrong install fails at compile time. A
headless test that docks two windows with the dock builder and draws both. A
manual drag of one chart onto another.

Delivered in #73, stacked on #72. The release tree built with no warnings and
`ctest` passed 6 of 6. ImPlot and ImCoolBar needed no change. The check is an
`#error`, since only the preprocessor can test whether a macro is defined. The
manual drag is still to do.

### 2. Each chart keeps its own period and style

Add the period, style and EMA to `chart_state`. The window's scratch reads them
on first draw and pushes a command when the operator changes one, and the
runtime writes the row. New windows still seed from the last choice.
`market-view.ini` stays only as that seed, or goes (D7).

Check: a store round trip test, an old row without the fields loading with
defaults, and a restart that brings four charts back on four periods.

Delivered in #75. The fields are one optional `chart_view` (span in
nanoseconds, style, EMA). A chart without one, new or saved before the change,
draws with the `market-view.ini` defaults and saves them as its own on its
first frame. The `configure_chart` command writes one window row. The restart
check is a runtime test of two charts, not four.

### 3. The compact chart

`render_chart` branches on `ImGui::IsWindowDocked()`. The compact branch draws
the one-row header, no foot, the right-click menu for period, style and EMA,
and the focus outline. The workspace sets `AutoHideTabBar` through
`SetNextWindowClass` for charts.

Check: look tests on a docked chart (pills, tones, no foot), and an allocation
test showing a steady docked chart frame allocates nothing. A capture of a 2x2
docked desk for the gallery.

Delivered in #76. A steady docked chart frame allocates
nothing (it allocated 7 per chart before, the same as a floating one, which is
unchanged at 7). With the 15 px UI font the compact header is 24 px against
31 px for the full one, and the full chart's foot takes another 36 px. The
compact row is as tall as the 22 px flags. The period caption in the header
also opens the menu. Window padding stays as it is, because ImGui applies it
in `Begin`, before the chart can know it is docked. The gallery's
`docked-charts` capture shows four tiles.

### 4. Dock and pop out by click

The "Dock" icon on a floating chart and the "Pop out" and close icons on a
compact one. Add `DockSpaceOverViewport` with the passthrough central node so
there is a desk space to dock into.

Check: a headless test that a pop-out request leaves the window floating on the
next frame and a dock request leaves it docked.

Delivered in #N. What differed from the plan:

- The Dock icon sits at the right end of the floating chart's header, not in
  the foot. Pop out and close sit at the right end of the compact header. The
  icons are drawn first and the header text is clipped short of them, so a
  narrow tile loses text before it loses a control.
- The rule for where Dock puts a chart. While the desk's free centre is empty,
  the chart fills it. After that, it takes half of the tile focused last,
  split along that tile's longer side. Docking four charts one after another
  gives a grid, not four tabs. The split uses `DockBuilderSplitNode` from
  `imgui_internal.h`, the one use of the dock builder outside tests.
- The dock space is ImGui's `DockSpaceOverViewport` written out in
  `workspace::draw_dock_space`, because the stock call fills the whole work
  area. The shortcut bar is raised over every window, so the space stops at
  the bar's resting height and a tile docked at the bottom edge is never under
  it. The bar still grows over the desk while the pointer is on it. There is
  no viewport-wide status bar to avoid: each window draws its own.
- The icons reuse the existing sheet, so no glyph was added. Dock is `panel`
  (Column Triple), pop out is `move` (Arrow Upload) and close is `cancel`
  (Dismiss). Each has a tooltip that names the action.
- The requests are interface scratch (`window_ui_state::tile`), not commands.
  Where a window sits is ImGui's state in `imgui.ini` (D4), so nothing reaches
  the runtime. Close still goes through `close_window`.
- `SetNextWindowDockID(0)` undocks a window (verified by the pop-out test).
  A popped-out window floats over the tile it left, at the size it last
  floated at, or at its opening size when it has not floated this run, kept
  inside the work area.

### 5. Shared crosshair

The double-buffered cursor time in the workspace and the line and tag in
compact charts.

Check: a headless test that feeds a mouse position over one plot and asserts the
other draws its line at that time on the next frame. A frame-cost check that an
idle desk is unchanged.

### 6. Tear-out windows by default

Run `HEDGR_PLATFORM=x11` by hand: tear a chart out, dock it back, restart and
see it return, minimise the main window. Then fix what the run shows. The known
candidates are all inferred. Torn-out windows keep the theme's rounded corners and
show the clear colour behind them (ImGui's examples set `WindowRounding` to 0
and an opaque window background when viewports are on); the minimised wait
(`application.cpp:723-728`) stops drawing torn-out windows while the main one
is iconified; window positions in `imgui.ini` are desktop coordinates on X11
and main-window coordinates on Wayland, so switching platform moves windows.
Then make X11 the default when `DISPLAY` is set and keep
`HEDGR_PLATFORM=wayland` as the way back, or keep Wayland the default if the
run says XWayland costs more than it gives. Captures stay on whichever platform
they run on today.

Check: the manual run above, and a capture on the default platform unchanged.

## Decisions

Decided on 2026-10-22: D1 ImGui docking, D2 plain drag, D3 docked means small,
D4 layout in `imgui.ini` beside the store, D5 every desk window except the
startup notice, the lock prompt and the shortcut bar, D6 a shared crosshair
across docked charts in one session, D7 per-chart period and style with
`market-view.ini` as the seed for new charts, D8 not in the first slice, D9 in
(changed from the recommendation), D10 later. The list below is kept as asked.

- **D1. How docking is built.** ImGui's docking (recommended): drag a window
  onto another to split or tab, works for every window kind, small build
  change. The alternative is a Hedgr-made "board" window that holds only
  charts, with layout in the database, more code and no ladder beside a chart.
- **D2. Docking on plain drag or with Shift held.** With plain drag (ImGui's
  default, recommended) moving any window near another shows drop targets. With
  Shift, windows move freely as today and docking needs Shift. Pick Shift if
  drop targets during ordinary moves feel noisy.
- **D3. What makes a chart small.** Docked means small (recommended): one rule,
  the operator controls it by docking. The alternatives are "small below a
  size", which changes look while resizing, or a stored per-chart toggle, which
  is one more setting.
- **D4. Where the layout is saved.** In `imgui.ini` beside the database
  (recommended, after step 0), the same place positions are saved today. The
  alternative is inside the database, so the layout travels with the database
  file, at the cost of a new table and command.
- **D5. Which windows may dock.** Every desk window except the startup notice,
  the lock prompt and the shortcut bar (recommended). The alternative is charts
  only, which rules out a ladder or a matrix beside a chart.
- **D6. Which charts share the crosshair.** Every docked chart in the same
  session (recommended). Alternatives: every chart on the desk, or off unless
  turned on.
- **D7. Per-chart period and style saved.** Yes (recommended), so a grid of
  tiles on different periods comes back as it was. Then decide whether
  `market-view.ini` stays as the default for new charts (recommended) or goes.
- **D8. Two tiles of one series** (for example 1m and 1h of EUR/USD). Not in the
  first slice (recommended). It needs the chart id rule
  (`window_service.cpp:156-165`) to change, which also changes what "open this
  series again" does.
- **D9. Tearing a tile out into its own OS window** (multi-viewports). The
  plan recommended out. **Decided in.** ImGui's GLFW backend turns it off under
  Wayland, so it works through XWayland, behind `HEDGR_PLATFORM=x11` until
  step 6.
- **D10. Linked pan and zoom across tiles.** Later (recommended). Charts cannot
  pan or zoom today.

## Out of scope

Native Wayland multi-viewports (no backend supports them), drawing tools, indicators beyond the EMA, volume bars,
compare overlays, saved named layouts or layout presets, two tiles of one
series, linked pan and zoom, and a Hedgr-owned tiling container.

## Risks

- **Shared install tree.** Every worktree builds against one
  `vcpkg_installed` directory. Reinstalling ImGui with docking changes
  `ImGuiIO` for all of them at once, so every tree needs a full rebuild, and a
  branch without step 1 then compiles against a docking header. The
  `static_assert` in step 1 makes the mismatch loud in one direction only.
  Observed in step 1: vcpkg installs headers with the tarball's file times, so
  ninja does not see them change and an incremental build links stale objects
  (`undefined reference to ImGui::GetForegroundDrawList()`). Run
  `ninja -t clean` in each tree after the reinstall.
- **"Experimental".** The feature is named experimental in vcpkg. The docking
  branch is tagged at each release and is what most ImGui applications ship on
  (inferred from the tag scheme, not measured).
- **Internal API in tests.** The dock builder lives in `imgui_internal.h`, which
  can change between releases. It is confined to tests and to step 4 at most.
- **ImCoolBar** reaches into `imgui_internal` (`shortcut_bar.cpp:21`). Its
  docking guard suggests it was written against the docking branch (inferred),
  but it has not been built there.
- **Minimum size.** The 360x220 floor may not apply to docked windows
  (inferred), so a tile can be squeezed until the header no longer fits. Step
  3 should clip the header rather than wrap it.
- **Viewports untested with a display.** Step 1 wires them and the source says
  Wayland ignores the request, but no torn-out window has been seen. Step 6
  owns that run.
- **Focus and capture.** `SetNextWindowFocus` on a docked tab selects the tab
  (inferred). The capture raise at `workspace.cpp:312-317` should still work.

## What cannot be verified without building

- That the project builds and the suite passes on the docking install. Done
  in step 1.
- What a torn-out window looks like and how it behaves on GNOME through
  XWayland (step 6).
- That ImPlot 1.0 behaves the same inside docked windows. Its source has no
  docking code, which is evidence but not a run.
- That `SetNextWindowDockID(0)` undocks a window. Verified in step 4.
- How `imgui.ini` behaves when the store and the ini disagree about which
  windows exist.
- How the compact header reads at the smallest tile sizes, which needs eyes on
  a capture.

## A defect noticed

`vcpkg.json:5` declared `"version-semver": "0.0.1"` while `CMakeLists.txt`
declares the project version 0.2.0. Fixed in #72.
