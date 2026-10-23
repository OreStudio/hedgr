# Menu system

A menu system in the look of the reference
(`docs/samples/572312177-2d9a6bd9-b2d3-4322-b650-aa904833b128.png`), replacing
the ImCoolBar shortcut bar over several small PRs. This is a plan; no code
changes with it.

## The reference

- A top bar. A round logo button at the far left, then upper-case monospace
  labels (FILE, VIEW, HELP, TOOLS, DEBUG), then a pill group of mode buttons.
- A popup is a flat dark rounded panel with plain rows. The hovered or selected
  row is a purple rounded bar across the panel. A row with a submenu ends in a
  right chevron, and the submenu opens beside it.
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

Colours and sizes are constants there, none in widgets. The popup fill, the
selected-row purple (`chart_tag_colour`, `#7c59e9`, with a lower alpha for
hover), the chevron and the label colours are added next to the tab colours.
`PopupRounding` goes from 6 to 8 and `PopupBorderSize` stays 1.

## Steps

Each step is one PR, with tests, and leaves the app working.

1. **Menu primitives and theme.** `widget/menu.hpp`: `menu_item_row`,
   `submenu_row` and `menu_label`, drawn over `BeginPopup`/`BeginMenu` with the
   flat panel, the purple rounded selected row and the chevron. Theme constants
   and `ImGuiCol_Header*` pushed for popups only, so list selection elsewhere
   keeps its navy wash. Headless frame tests for row colour on hover, chevron
   presence and popup rounding, mutation-checked as the tab tests are.
2. **The top bar and the system menu.** A main menu bar window drawn from
   `workspace` before the dock space, with the logo button at the left opening a
   system menu (Instances, Lock store, Undock all, About) and upper-case
   labels. The dock space and `floating_size_for` already read `WorkPos`, so the
   bar must be a viewport work area inset, not a window over the desk. The bar
   stays empty of other menus in this PR. Ships the cropped logo asset.
3. **The Instances panel.** An Instances menu opens a large panel: a filter box
   and a tile grid of instances with their photos from the image cache. A click
   opens or raises the Instances window on that instance. Reuses the
   `entry_tooltip` text for hover.
4. **Accounts.** An Accounts menu opens a panel with tenant headings and the
   accounts under each, from `tenant_row` and the accounts table. A click opens
   the accounts window for that instance and account.
5. **The Windows list.** A Windows menu lists open windows from `input.windows`
   grouped by `window_kind`. Click raises (reusing the `focus_instances`
   mechanism, which is a request read before the walk), the x closes through the
   close command, as the window's own close control does. Mini plot actions go
   here.
6. **The status bar.** One viewport-wide bottom strip in the reference's look,
   carrying the message ladder from `status_bar.hpp` and the connection state
   once PR 94 lands. The dock space already subtracts a bar height; this swaps
   the shortcut bar's height for the status bar's.
7. **Retire the cool bar.** Delete `shortcut_bar.cpp`, `shortcut_bar_result`,
   `shortcut_bar_height` and `shortcut_bar_tests.cpp`, move Undock all to the
   system menu and Windows menu, and drop the ImCoolBar dependency. Shortcuts in
   the catalogue become the Instances panel's pinned row, or are dropped; see
   decision 5.

## The logo

Crop the swirl only from
`/home/marco/Development/OreStudio/ores_dev_brave_hopper/assets/images/ore-studio-logo.png`
(1024 x 1024, RGB). The swirl lies about x 215 to 485, y 385 to 655. The "ai"
badge is at the bottom right, outside that box, so the crop removes it. The
background is a flat dark grey with no alpha, so the crop needs a mask: keying
on the background colour leaves a dark fringe on the antialiased edge, so make
the alpha from the distance to the background and un-premultiply. Ship it as a
small PNG (64 and 128 px) under `assets/branding`, and install it with the other
assets. This is a script checked in under `tools`, not a hand edit.

## Decisions

1. **Menu bar built on ImGui's main menu bar or on our own window.**
   Recommend our own window. `BeginMainMenuBar` grows the viewport's work area,
   which is right for the desk, but it forces its own height and styling, and
   our logo button, pill group and the status bar want custom drawing. Use a
   fixed-height window and set the work area inset ourselves.
2. **Popups as `BeginPopup` or as plain windows.** Recommend `BeginPopup` for
   Instances and Accounts, since it gives click-outside close and nesting. The
   large panels are `BeginPopup` with a fixed size.
3. **Opens on click or on hover.** Recommend click to open, then hover to move
   between top-level labels, as the reference does.
4. **Logo button action.** Recommend the system menu, which holds Instances as
   its first row. A click that jumps straight to the Instances dialog hides the
   rest of the menu.
5. **Shortcuts.** The catalogue shortcuts have no home in the reference.
   Recommend a pinned row at the top of the Instances panel, and drop the rest.
6. **Label font.** Recommend the proportional sans first, with upper case and
   letter spacing, and a bundled monospace later, as the tab look does.
7. **Status bar contents.** Recommend the message ladder at the left and the
   connection state at the right. The reference's licence, build, ping and FPS
   rows are copied only where Hedgr has the data.
8. **Where the status bar sits relative to window foots.** Recommend keeping
   the window foots; they carry per-window identity the strip cannot.
9. **Order of steps 5 and 6.** Recommend the Windows list before the status
   bar, because it replaces what the cool bar did for raising windows.

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
  The menus must do the same, or show only Lock and Quit, since the catalogue
  is behind the master password.
- **The mode pill group** in the reference has no equivalent here and is not in
  scope.
- **Keyboard and gamepad nav.** Nav is enabled; custom rows must be
  focusable, or arrow keys skip them.
