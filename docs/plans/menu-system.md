# Menu system

A menu system in the look of the reference
(`docs/samples/572312177-2d9a6bd9-b2d3-4322-b650-aa904833b128.png`), replacing
the ImCoolBar shortcut bar over several small PRs. Step 1 is done; the rest is
plan.

## The reference

- A top bar. A round logo button at the far left, then upper-case monospace
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

- **Shortcut bar.** `render/shortcut_bar.cpp` draws an ImCoolBar window anchored
  to the bottom centre of the viewport. It holds one tile per catalogue
  shortcut (`input.shortcuts`, a `domain::shortcut` with a window kind and
  target id) and, while anything is docked, an Undock all tile. Its resting
  height is `shortcut_bar_height`; `workspace::draw_dock_space` shrinks the dock
  space by that height. The tile click goes through `activate_entry`, which
  handles two kinds only: Instances (`request_instances_window` plus
  `shortcut_bar_result::focus_instances`) and accounts
  (`request_accounts_window`). Any other kind is an inert click. Hover raises a tick signal. The bar is raised over every window
  by `raise_bar_to_front` and draws nothing while the store is locked.
- **Mini plots actions.** There are none on the bar in this checkout. The mini
  plot is opened from a chart or from the series browser. The Windows menu below
  is where "tidy mini plots" and "close all mini plots" would live if wanted.
- **Menu bars.** Only the command centre and the workspace window use
  `ImGui::BeginMenuBar`, inside their own windows. There is no main menu bar.
- **Status.** There is no application-wide status bar. Each window draws its own
  foot with `widget::draw_status_foot` (`widget/status_foot.hpp`: identity,
  state dot, optional combos), and `widget/status_bar.hpp` holds a one-line
  message ladder (notice, problem, error) that a window renders itself. A
  bottom bar like the reference's would be new.
- **The logo.** `render/splash.cpp` draws a splash from
  `assets/branding/ore-studio-splash.png`. Nothing draws a small logo.
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
2. **The top bar and the system menu.** A main menu bar window drawn from
   `workspace` before the dock space, fixed height, our own window (decision 1).
   The ORE swirl logo button at the left (`assets/branding/ore-logo-white.svg`,
   rasterised through `image::rasterize_svg`, the black variant for a light
   theme) opens the system menu with Instances as its first row, then Lock
   store, Undock all and About, and upper-case top-level labels follow. The
   dock space and `floating_size_for` already read `WorkPos`, so the bar must
   be a viewport work area inset, not a window over the desk. The bar stays
   empty of other menus in this PR.
3. **The toolbar.** A second fixed-height strip under the menu bar, styled like
   the reference's schematic toolbar: a rounded capsule with a purple outline
   holding a row of flat icon buttons, a small grip handle at its left, and the
   selected tool filled purple (`chart_tag_colour`). Each button is a real
   ImGui item with a tooltip, built from the icon tile in `chart_window.cpp`
   (`tile_icon`) and the pill drawing in `widget/segmented.hpp`, so nav and
   hover work as elsewhere. In Hedgr it holds the actions that today have no
   quick home: open chart, open mini plot, CRM, accounts, undock all, and later
   a tool mode (pointer, pan, measure) for the chart if one is wanted. Opening
   actions are momentary (no selected fill); only a real mode stays filled.
   The toolbar takes its height from the same one place as the menu bar, so
   the work area is inset once. It draws nothing while the store is locked.
4. **The Instances panel.** An Instances menu opens a large panel: a filter box
   and a tile grid of instances with their photos from the image cache. A click
   opens or raises the Instances window on that instance. The catalogue
   shortcuts become a pinned row at the top of the panel (decision 5). Reuses
   the `entry_tooltip` text for hover.
5. **Accounts.** An Accounts menu opens a panel with tenant headings and the
   accounts under each, from `tenant_row` and the accounts table. A click opens
   the accounts window for that instance and account.
6. **The Windows list.** A Windows menu lists open windows from `input.windows`
   grouped by `window_kind`. Click raises (reusing the `focus_instances`
   mechanism, which is a request read before the walk), the x closes through the
   close command, as the window's own close control does. Mini plot actions go
   here. It comes before the status bar (decision 9).
7. **The status bar.** One viewport-wide bottom strip in the reference's look,
   carrying the message ladder from `status_bar.hpp` and the connection state
   once PR 94 lands. The dock space already subtracts a bar height; this swaps
   the shortcut bar's height for the status bar's and, with the toolbar, keeps
   one inset for top and bottom.
8. **Retire the cool bar.** Delete `shortcut_bar.cpp`, `shortcut_bar_result`,
   `shortcut_bar_height` and `shortcut_bar_tests.cpp`, move Undock all to the
   system menu and Windows menu, and drop the ImCoolBar dependency. The pinned
   shortcut row from step 4 already holds the catalogue shortcuts.

## The logo

Decided: the swirl only, as `assets/branding/ore-logo-white.svg`, with
`ore-logo-black.svg` beside it. They are vectors, so the earlier plan to crop a
PNG with a script is dropped: rasterise at the button's size through
`image::rasterize_svg` (lunasvg is already linked) and upload one texture per
size. The button opens the system menu, not the Instances window.

## Decisions

Decided by the user: 1, 4, 5 and 9. The rest are recommendations to confirm.

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
4. **Logo button action.** Decided: the ORE swirl opens the system menu, whose
   first row is Instances. A click that jumps straight to the Instances dialog
   hides the rest of the menu.
5. **Shortcuts.** Decided: the catalogue shortcuts become a pinned row at the
   top of the Instances panel. The ImCoolBar tile bar is retired in step 8.
6. **Label font.** Recommend the proportional sans first, with upper case and
   letter spacing (done in step 1), and a bundled monospace later, as the tab
   look does.
7. **Status bar contents.** Recommend the message ladder at the left and the
   connection state at the right. The reference's licence, build, ping and FPS
   rows are copied only where Hedgr has the data.
8. **Where the status bar sits relative to window foots.** Recommend keeping
   the window foots; they carry per-window identity the strip cannot.
9. **Order of the Windows list and the status bar.** Decided: the Windows list
   (step 6) before the status bar (step 7), because it replaces what the cool
   bar did for raising windows.
10. **Toolbar contents and behaviour.** New. Recommend open chart, open mini
    plot, CRM, accounts and undock all as momentary buttons, and no mode
    buttons until the chart has modes to choose between. Open question: whether
    the toolbar floats over the desk as a draggable capsule (the grip handle
    suggests it) or is docked in a fixed strip under the menu bar. The plan
    assumes the fixed strip, since one work-area inset is simpler than a
    movable window that must not cover the tab bars.

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
- **The store lock.** The cool bar draws nothing while the store is locked.
  The menus and the toolbar must do the same, or show only Lock and Quit, since the catalogue
  is behind the master password.
- **The mode pill group** in the reference (SCHEMATIC and its icons) has no
  equivalent here and is not in scope; the toolbar step is the nearest thing.
- **Keyboard and gamepad nav.** Nav is enabled; custom rows must be
  focusable, or arrow keys skip them.
