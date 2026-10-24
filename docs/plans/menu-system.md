# Menu system

A menu system in the look of the reference
(`docs/samples/572312177-2d9a6bd9-b2d3-4322-b650-aa904833b128.png`), replacing
the ImCoolBar shortcut bar over several small PRs. All nine steps are done; the
notes after each record what the implementation found.

## The reference

- A top bar. A small plain logo at the far left (a brand mark, not a menu), then upper-case monospace
  labels (FILE, VIEW, HELP, TOOLS, DEBUG), then a pill group of mode buttons.
- A popup is a flat dark rounded panel with plain rows. The hovered or selected
  row is a purple rounded bar across the panel. A row with a submenu ends in a
  right chevron, and the submenu opens beside it.
- A floating toolbar in the viewport, the schematic toolbar: a rounded capsule
  with a purple outline, a small grip handle at the left, then flat icon buttons
  in a row. The selected tool is a purple rounded square behind its icon.
- A status bar along the bottom: licence and build at the left, ping, FPS and
  frame time at the right, dim text on a dark strip.

## What exists

- **Shortcut bar (retired).** `render/shortcut_bar.cpp` drew an ImCoolBar window
  at the bottom centre, one tile per catalogue shortcut plus Undock all, and the
  dock space stopped short of it. Step 9 deleted it, with the vendored ImCoolBar.
  The catalogue (`input.shortcuts`) stays: the Instances panel's pinned row
  lists it. The tile photos and the hover tick went with the bar.
- **Mini plots actions.** There are none on the bar in this checkout. The mini
  plot is opened from a chart or from the series browser. The Windows menu below
  is where "tidy mini plots" and "close all mini plots" would live if wanted.
- **Menu bars.** Only the command centre and the workspace window use
  `ImGui::BeginMenuBar`, inside their own windows. There is no main menu bar.
- **Status.** There is an application-wide status strip along the bottom
  (step 8). Each window still draws its own foot with
  `widget::draw_status_foot` (`widget/status_foot.hpp`: identity, state dot,
  optional combos), and `widget/status_bar.hpp` holds a one-line message
  ladder (notice, problem, error) that the strip and the window foots both
  read.
- **The logo.** `render/intro.cpp` draws the launch intro
  (the splash and its artwork are gone). Nothing draws a small logo.
- **Docking and viewports.** `hedgr.imgui/src/app/application.cpp` enables
  docking and viewports. The desk is a full-work-area window with a dock space
  (`###hedgr-desk`), placed from `viewport->WorkPos` and `WorkSize`.
- **Images.** `render_input::images` is an image cache that serves instance
  photos (`assets/instances/*.png`) as textures through one seam.
- **Window kinds.** `domain::window_kind` has eleven kinds, from chart to
  mini_plot. `input.windows` is the table of open windows.

## Look decisions in `theme.hpp`

Colours and sizes are constants there, none in widgets. The popup fill and
outline, the selected-row purple (`chart_tag_colour`, `#7c59e9`, with a lower
alpha for hover), the chevron and the label sizes are added next to the tab
colours. `PopupRounding` goes from 6 to 8 and `PopupBorderSize` stays 1.

## Steps

Each step is one PR, with tests, and leaves the app working.

1. [x] **Menu primitives and theme.** `widget/menu.hpp`: `menu_popup` (the flat
   panel as a scope over `BeginPopup`), `menu_item_row` (label, optional icon,
   shortcut, chevron, disabled), `menu_separator`, `menu_heading` and
   `menu_label`. Theme constants and the `ImGuiCol_Header*` colours are pushed
   for the popup only, so list selection elsewhere keeps its navy wash.
   Headless frame tests for row colour on hover, selected and disabled rows,
   chevron presence, popup rounding and fill, keyboard nav and steady-frame
   allocations, mutation-checked as the tab tests are.
   - Notes. A submenu row only draws the chevron. It does not use
     `ImGui::BeginMenu`, whose arrow and hover logic cannot be restyled; the
     caller opens the child panel beside `GetItemRectMax` on hover or press,
     which is what the large Instances and Accounts panels do anyway
     (decision 2). Closing a submenu when the pointer moves to another row is
     step 2's to build and check in a real window. The popup is wired to
     nothing yet; the hidden `component gallery` test draws one for a look.
2. [x] **The top bar and the system menu.** A fixed-height menu bar window
   (`render/menu_bar.cpp`), our own window (decision 1), drawn by `workspace`
   before the dock space. The ORE swirl at the left
   (`assets/branding/ore-logo-white.svg`, rasterised through
   `image::rasterize_svg` at the mark's pixel size). This step first made the
   logo open a system menu; step 3 replaced that.
   - Notes. The bar goes through ImGui's `BeginViewportSideBar`, which is the one
     place the top is inset: the viewport's `WorkPos` and `WorkSize` shrink by
     `theme::menu_bar_height` a frame after the bar first draws, so the dock
     space, `floating_size_for`, the card grid and Undock all read the inset
     without knowing about it. Click opens, and while a popup is open the pointer on
     another entry moves it there (`widget::menu_after_entry` is the pure rule,
     `menu_bar_entry` applies it to ImGui). A click on an open entry closes it
     because ImGui closes the popup at the press, which the rule accounts for.
     The logo's SVG is read once from `branding/` beside the library (the cmake
     copy rule ships it beside the intro) and reaches the GL cache through
     `image_texture_source::svg`. The black variant is unused, as the theme is
     dark only. Not checked in a real window.
3. [x] **Traditional menus.** Decision change (user feedback on the reference):
   the logo is a plain brand mark, not a menu, and the top-level menus lead
   instead, as in the reference. The logo keeps an "ORE Studio" hover tooltip.
   The bar is a translucent, rounded plate with a left to right gradient
   (decision 11) and holds, right of the
   logo, FILE, VIEW, TOOLS and HELP, each one `widget::menu_bar_label` call.
   - FILE: Instances, a separator, Lock store, Unlock store or Create store by
     the lock state, a separator, Quit. Instances moves here from the system
     menu.
   - VIEW: Undock all, a disabled row until something is docked. The Windows
     submenu is step 7's, and was added there.
   - TOOLS: Series browser, Accounts (a submenu, see step 9) and, for a system
     administration session, Command centre. They act on the first signed-in session and are disabled
     when there is none. The CRM matrix has no row: it needs a party and a
     matrix name, which only the series browser's picker can supply.
   - HELP: About, a small window with the mark, the version from the CMake
     project version (`HEDGR_VERSION`, `render::version_text`) and the licence
     line.
   - While the store is shut only FILE is drawn (the way in and Quit).
   - Quit sets `menu_bar_result::quit_requested`; `workspace::exit_requested`
     carries it to the frame loop, which sets its existing `exit_requested_`.
   - Notes. The labels are as tall as the bar, so their popups open flush under
     its edge (`menu_label` takes a height). Not checked in a real window. The label
     face was the proportional sans at first; it is now the bundled monospace
     (decisions 6 and 11).
4. [x] **The toolbar.** A floating capsule over the desk, not a fixed strip,
   styled like the reference's schematic toolbar: a rounded capsule with a purple
   outline holding a row of flat icon buttons, a small grip handle at its left
   that drags it, and the selected tool filled purple (`chart_tag_colour`). It
   is a window of its own that is never dockable, is clamped inside the work
   area (below the menu bar, since the work area is already inset) so it can
   never cover the menu bar or leave the desk, and remembers its position in the
   ini or the store. Each button is a real ImGui item with a tooltip, built from
   the icon tile in `chart_window.cpp` (`tile_icon`) and the pill drawing in
   `widget/segmented.hpp`, so nav and hover work as elsewhere. In Hedgr it holds
   the actions that today have no quick home: open chart, open mini plot, CRM,
   accounts, undock all, and later a tool mode (pointer, pan, measure) for the
   chart if one is wanted. Opening actions are momentary (no selected fill); only
   a real mode stays filled. Because it floats it takes no height from the
   work-area inset. It draws nothing while the store is locked.
   - Notes. `widget/toolbar.{hpp,cpp}` is the pure layout (capsule size, grip and
     button rectangles, `toolbar_clamp`, `toolbar_default_pos`,
     `toolbar_enabled`) and `render/toolbar.cpp` draws it, one ImGui window
     (`###hedgr-toolbar`) with NoDocking, no title bar, no resize, no scrollbar
     and auto-fit, so its position is the window's ini entry and survives a
     restart. The window is NoMove; the grip drags it by hand (the grab offset is
     kept in the window's state storage) so the body never moves it by accident.
     It is clamped to the main viewport's `WorkPos` and `WorkSize` after `Begin`
     every frame, so a restored or resized position is corrected in the frame it
     is read. The pill is `AddRectFilled` and `AddRect` at half the height, the
     outline 1.5 px of `chart_tag_colour()`, the fill `menu_surface()`. The
     buttons are `widget::icon_tile`, the look `tile_icon` in `chart_window.cpp`
     had, which now calls it. Buttons: Series browser and Accounts (the first
     signed-in session, as the TOOLS rows; `current_session` and
     `request_session_window` moved to `panels` to be shared) and Undock all.
     VIEW gains a Toolbar row (lit while shown), stored as `toolbar=` in the
     market view preferences file. It is submitted after the desk's windows and
     brought to the display front each frame unless a popup is open, which is
     what keeps it above windows and below menus. Not checked in a real window.
   - Left out. CRM has no button: opening a CRM needs a party and a matrix name,
     which only the series browser's picker can supply (see step 3). Mini plot of
     the focused chart is omitted, as nothing here tracks which chart has focus.
     No button is a mode, so none is drawn as the selected purple fill.
5. [x] **The Instances panel.** An Instances menu opens a large panel: a filter box
   and a tile grid of instances with their photos from the image cache. A click
   opens or raises the Instances window on that instance. The catalogue
   shortcuts become a pinned row at the top of the panel (decision 5). Reuses
   the `entry_tooltip` text for hover.
   - Notes. INSTANCES is the third label, after FILE and VIEW and before TOOLS,
     drawn only while the store is unlocked. Its popup is `menu_popup` at a fixed
     size (`theme::instances_panel_width` and `_height`, 720 by 520), capped to
     the work area below the label and slid left to stay inside it
     (`widget::place_menu_panel`, `menu_panel_left`), with the usual fill and
     caret. `render/instances_panel.cpp` draws the body: a pinned row of small
     tiles, a filter box (hint "Filter instances", cleared and focused each time
     the panel opens) with a right-aligned shown/total count, and a scrolling
     grid with one tile per instance (photo from `instance_icons`, or the
     instance glyph on the surface colour; the name under it; a dot in the
     probe's colour; the name, id, address and status word in the tooltip).
     Layout is pure in `widget/instances_panel.{hpp,cpp}` (`instance_matches`,
     `tile_columns`, `tile_position`, `tile_grid_height`). The filter is a fixed
     64 byte buffer in `menu_bar_state`; the filtered slot list is rebuilt only
     when the text or `instance_table::version()` (new, bumped by every write)
     differs, and the grid draws only the rows in view, so a steady frame
     allocates nothing. A pin does what the removed bar did (the Instances
     window through `request_instances_window` and `focus_instances`, or
     `request_accounts_window`, then `raise_shortcut_activated`, which moved to
     `panels` so TOOLS > Accounts shares it). A tile click raises the Instances
     window and the same signal; there is no selection to drive, so the window
     does not move to that instance yet. Ctrl keeps the panel open for several
     picks. An empty store and a filter that matches nothing both draw the no
     data mark, the words in its tooltip. TOOLS > Accounts stayed until step 6
     deleted it.
     Not checked in a real window.
6. [x] **Accounts.** An ACCOUNTS menu opens a panel with tenant headings and the
   accounts under each, from `tenant_row` and the accounts table. A click opens
   the accounts window for that instance and account.
   - Notes. ACCOUNTS is the fourth label, after INSTANCES and before TOOLS, and
     it is drawn only while the store is unlocked. The panel
     (`render/accounts_panel.cpp`, `widget/accounts_panel.hpp`) is the Instances
     panel's size (720 by 520, capped to the work area) with a filter box, the
     shown/total count and a scrolling list. Each row carries the account's
     name, its principal and the instance it belongs to, because one tenant can
     exist on more than one deployment. A pick opens that instance's Accounts
     window, selects the account through `request_account_selection` (the
     window's selection is its own scratch), raises the shortcut signal and
     closes the panel unless Ctrl is held. TOOLS > Accounts is deleted, and the
     shortcut `pin_accounts` tests with it.
   - Where the heading comes from. There is no tenant table on the render input
     and `domain::account` has no tenant field: IAM answers the tenant at
     sign-in and `session_table` keeps the last answer. The panel groups by
     that answer, and an account that has not signed in is listed last under
     "No tenant yet". A future change that stores a tenant on the account would
     change the source, not the panel.
   - The list is rebuilt every frame into a vector kept across frames: the
     session table has no version to cache against. Both lookups are slab walks
     rather than `find`, whose key joins a string and would allocate once per
     account per frame. A steady frame allocates nothing, pinned by a frame-cost
     test. Tenant and instance names are folded for ordering, not for display.
   - Not checked in a real window.
7. [x] **The Windows list.** A Windows menu lists open windows from
   `input.windows` grouped by `window_kind`. Click raises (reusing the
   `focus_instances` mechanism, which is a request read before the walk), the x
   closes through the close command, as the window's own close control does.
   Mini plot actions go here. It comes before the status bar (decision 9).
   - Notes. Windows is the fourth row of VIEW, a chevron row with an anchored
     panel beside it (`theme::windows_panel_width` and `_height`, 380 by 420).
     The panel walks the kinds in enum order and draws a heading only for a kind
     that has an open window, then one row per window. A row is two items: a
     selectable title that sets `menu_bar_result::focus_window`, and a small x
     that pushes `close_window` for that id. `workspace::render` folds the id
     into the focus request it already takes from the window table before the
     walk, so the raise happens where a window can still be focused. A pick
     closes the panel and VIEW with `ClosePopupToLevel(0, true)`. The last row,
     "Close all mini plots", pushes one close per open mini plot and is left out
     when there is none. Tidy has no action to call, so it is not there.
   - Not checked in a real window.
8. [x] **The status bar.** One viewport-wide bottom strip in the reference's look,
   carrying the message ladder from `status_bar.hpp` and the connection state.
   It is a bottom side bar like the top bar, so the work area is inset by the
   bars alone (the dock space already fills the work area).
   - Notes. `render/status_bar.cpp` submits `###hedgr-status-bar` through
     `BeginViewportSideBar(ImGuiDir_Down)` at `theme::status_bar_height` (24 px),
     so the dock space and every window read the smaller work area without
     knowing about it. The left end is `status_bar::message()` in the severity's
     colour, dimming on the event's tail; the right end is a dot and one word
     from the sessions: failed, connecting, connected, partly connected,
     disconnected, or "no session". Failure outranks a connection in flight,
     which outranks quiet connectivity. The strip advances the message clock
     itself, so an event ages out even with no window open.
   - Kept: the window foots (decision 8). A consequence is that a runtime error
     is currently drawn both in the strip and in the foot of each open window,
     because the workspace's one `status_bar` is passed to them too. Folding the
     foot's message line into the strip is follow-up work, not this step.
   - Not checked in a real window.
9. [x] **Retire the cool bar.** Brought forward. Deleted `shortcut_bar.cpp`,
   `shortcut_bar_result`, `shortcut_bar_height`, `shortcut_bar_tests.cpp` and the
   vendored ImCoolBar (nothing else used it). The dock space now spans the whole
   work area, so the only inset is the top bar's. Undock all already lived in
   VIEW. The `focus_instances` request stays: FILE > Instances uses it.
   - The one thing reachable only through the bar was the per-instance accounts
     tile. TOOLS > Accounts is now a submenu row (chevron) whose panel opens
     beside it, on click or hover. It lists the signed-in session's instance
     first, then one row per catalogue shortcut of kind accounts, each instance
     once, labelled with the instance name. A pick calls
     `request_accounts_window` and raises `shortcut.activated` (so the sound pack
     still plays, ADR 0008), then closes the menu. This folds the old single
     Accounts row in: its session instance is the first entry. The row is
     disabled when there is neither a session nor a shortcut.
   - Stop-gap. The submenu is text only and lasts until step 5 (the Instances
     panel, with the photo tiles and the pinned row) and step 6 (the Accounts
     panel) replace it. The `shortcut.hovered` signal has no raiser now; its name
     and sound mapping stay for step 5's tiles.
   - Not checked in a real window.

## The logo

Decided: the swirl only, as `assets/branding/ore-logo-white.svg`, with
`ore-logo-black.svg` beside it. They are vectors, so the earlier plan to crop a
PNG with a script is dropped: rasterise at the button's size through
`image::rasterize_svg` (lunasvg is already linked) and upload one texture per
size. It is a plain mark with no popup; the system menu is gone.

## Decisions

Decided by the user: 1, 4, 5, 9, 10 and 11. The rest are recommendations to confirm.

1. **Menu bar built on ImGui's main menu bar or on our own window.** Decided:
   our own fixed-height window, reusing `theme.hpp`, `widget/`, `cells.hpp`,
   `cards.hpp`, `segmented.hpp`, `tile_icon` and the shortcut bar's drawing.
   `BeginMainMenuBar` forces its own height and styling, and our logo button,
   toolbar and status bar want custom drawing. We set the work area inset
   ourselves, in one place.
2. **Popups as `BeginPopup` or as plain windows.** Recommend `BeginPopup` for
   Instances and Accounts, since it gives click-outside close and nesting. The
   large panels are `BeginPopup` with a fixed size.
3. **Opens on click or on hover.** Recommend click to open, then hover to move
   between top-level labels, as the reference does.
4. **Logo button action.** Decided, and revised: the ORE swirl is a plain brand
   mark that opens nothing. It first opened a system menu with Instances at
   the top; the user then chose the reference's layout, with traditional menus
   (FILE, VIEW, TOOLS, HELP) and Instances under FILE.
5. **Shortcuts.** Decided: the catalogue shortcuts become a pinned row at the
   top of the Instances panel. The ImCoolBar tile bar is retired (step 9), and TOOLS > Accounts lists the shortcuts until then.
6. **Label font.** Decided (user feedback): the menu chrome is set in the
   reference's monospace, JetBrains Mono (SIL OFL 1.1), bundled under
   `assets/fonts` with `JetBrainsMono-OFL.txt`. It covers the bar's labels,
   every menu popup's rows and headings, and the desk's dock tab labels. The
   body font is unchanged. See decision 11.
7. **Status bar contents.** Recommend the message ladder at the left and the
   connection state at the right. The reference's licence, build, ping and FPS
   rows are copied only where Hedgr has the data.
8. **Where the status bar sits relative to window foots.** Recommend keeping
   the window foots; they carry per-window identity the strip cannot.
9. **Order of the Windows list and the status bar.** Decided: the Windows list
   (step 7) before the status bar (step 8), because it replaces what the cool
   bar did for raising windows.
10. **Toolbar contents and behaviour.** Decided: it floats over the desk as a
    draggable capsule (the grip handle is its drag), not a fixed strip. It must
    not be dockable, stays inside the work area, remembers its position in the
    ini or the store, and must not cover the menu bar. Contents are a
    recommendation: open chart, open mini plot, CRM, accounts and undock all as
    momentary buttons, and no mode buttons until the chart has modes to choose
    between.
11. **The bar's look.** Decided from measurements of the reference
    (`572312177-...png`, 1600 by 900), then revised after the user saw PR #100.
    - Gradient. Sampled along the bar's row at twenty places, the colour is
      #3D364C at the far left, falls in a straight line to #1A1A24 at about a
      quarter of the width (x 400), and stays flat to #1C1C26 at the right edge.
      Down the bar it does not change at any x, so there is no vertical wash.
      The three stops are `theme::menu_bar_stops`; `theme::fill_stops` draws
      them (a triangle fan cannot bend at a stop, so it draws one clipped span
      per pair of stops, each vertex coloured from `stops_colour`).
    - Opacity. Every stop and the hairline carry `theme::menu_bar_alpha`
      (0.85), so the wallpaper shows faintly through the plate. The wallpaper
      is on `GetBackgroundDrawList`, behind every window, and the bar window
      sets `NoBackground`, so nothing opaque sits between them.
    - Shape. The reference's bar is not rounded or inset: it runs edge to edge,
      about 50 px tall, with no outline. Its rounded pieces are the SCHEMATIC
      pill and the window-control box at the right. The user asked for rounded
      corners, so the plate is a rounded rectangle (`menu_bar_rounding`, 10 px)
      inset by `menu_bar_margin` (6 px) on all four sides. The bar window is
      `menu_bar_extent` (plate height plus a margin above and below) high, so
      the work-area inset counts the margin once and the dock space starts at
      the window's bottom. Setting the margin to 0 gives the reference's
      flush bar back.
    - Font. The letterforms of FPS: 127.5, Cursor World and the tab labels
      read as JetBrains Mono (tall x-height, squared curves, wide even
      advance), judged by eye from the picture; no other family was compared.
    - Not checked in a real window.
12. **Glass panels and the caret.** Decided from the reference's render-mode
    menu (`267987113-...png`), after the user asked for "a bit of transparency
    and an arrow pointing to the menu bar".
    - Fill. `theme::menu_surface()` carries `theme::menu_popup_alpha` (0.80),
      so the scene shows faintly through every menu panel, the TOOLS > Accounts
      child panel included. Text and the outline stay opaque.
    - Caret. A 12 by 5 px triangle (`menu_caret_width`, `menu_caret_height`)
      on the panel's top edge, pointing up, centred on the label that opened the
      popup (`menu_bar_label` passes the label's centre to `menu_popup`). The
      gap under the plate is 4 px, so the caret is one pixel taller: its tip
      lands 1 px under the plate's bottom edge, over the hairline, as the
      reference's caret meets its button. Its centre is clamped
      `menu_caret_margin` (the panel's corner radius) plus half its width clear
      of the panel's ends, so a popup shifted to stay on screen still gets a
      caret on the straight part of its edge. The fill is the panel's, the
      outline is stroked on the two slanted sides only, and the base sits one
      border width inside the panel so the fill hides the panel's outline there.
    - Where it is drawn. The tip is outside the popup window, which clips its
      own draw list, so the caret goes on the foreground list of the popup's
      viewport. A popup on another viewport (its own OS window on X11) gets no
      caret, because it would lie outside that window.
    - Left out. The Accounts child panel has no caret: it opens beside a row, and
      a left-pointing caret was optional. The About window is an ordinary window
      with the window look, not a menu panel, and is unchanged.
    - Not checked in a real window, nor the other-viewport case.

## Risks

- **Popups anchored to bar items.** `OpenPopup` and `BeginPopup` anchor at the
  last item when drawn right after it, but the large panels need a position of
  their own below the label and must be clamped to the work area. Test with the
  bar at the edge of a narrow viewport.
- **Main menu bar height against the dock space.** The desk window is placed at
  `WorkPos` and shrinks by the bar height. A top bar and a bottom bar both
  change that, and the dock node settings saved in the ini may hold sizes from
  before. Insetting the work area once, in one place, avoids two writers.
- **Popups over viewports on X11.** With viewports enabled a popup that leaves
  the main window becomes its own OS window, and some window managers show it
  with a frame or place it late. Large panels near the edge are the risk.
  Keep panels inside the main viewport (`ImGuiWindowFlags_NoViewport` is not
  per popup; use `SetNextWindowViewport` with the main viewport) and check on
  the target window manager, which cannot be done here.
- **The store lock.** The cool bar drew nothing while the store was locked.
  The menus, which hide everything but FILE, and the toolbar must do the same, or show only Lock and Quit, since the catalogue
  is behind the master password.
- **The mode pill group** in the reference (SCHEMATIC and its icons) has no
  equivalent here and is not in scope; the toolbar step is the nearest thing.
- **Keyboard and gamepad nav.** Nav is enabled; custom rows must be
  focusable, or arrow keys skip them.
