# Tab look

The reference is `docs/samples/572312177-2d9a6bd9-b2d3-4322-b650-aa904833b128.png`,
a dark app with folder tabs (Statistics, Viewport, Signal Analyzer, and a nested
ImPlot Demo / Dear ImGui Demo strip), a menu bar and a status bar. The first
pass (#96) set the colours by eye. This pass samples them.

## Copied from the reference

Every number below was read off the screenshot pixel by pixel.

- Chosen tab fill `#73608D` (`tab_active`), idle tab `#2B2D38` (`tab_idle`), the
  strip behind the tabs `#191923` (`tab_bar`), labels and close x `#CACDE0`
  (`tab_label`). The Signal Analyzer tab is not lighter: it is `#73608E` like
  Statistics and Viewport. The lighter `#7E6F9A` seen at the ImPlot Demo tab's
  left edge is the icon's anti-aliasing, not a fill.
- Folder tabs: rounded top (6 px, `tab_rounding`, read off the corner
  profile), square bottom, no outline (`TabBorderSize` 0), no overline
  (`TabBarOverlineSize` 0; the reference's chosen tab has no line on top).
- A one pixel rule under the strip in the chosen tab's colour, so the chosen tab
  joins it (`TabBarBorderSize` 1, which ImGui colours from the chosen tab).
- Tabs are 28 px tall including the rule, and four px apart. ImGui reads the
  height from `FramePadding.y` and the gap from `ItemInnerSpacing.x`, so the desk
  pushes `tab_padding_y` 6.5 (with the 15 px font) and `tab_spacing` 4 around the
  dock space only. Nothing else changes. The same push sets the label colour,
  the monospace font and the strip's backing (`TitleBg`).
- A strip that lost focus is drawn as a focused one: the reference's three
  strips show the same fill, so the dimmed pair equals the plain pair.
- The close x is ImGui's flat one, about 9 px wide and one pixel thick, as in
  the reference, on the chosen or hovered tab, now in the label colour.
- The icon before each label: the window titles already begin with the icon
  glyph, but the icons were glyphs of the base font only and the tabs use the
  monospace, so the tab drew the missing-glyph box or nothing. The loader now
  adds the icon glyphs to the monospace too.

## The fade

ImGui paints a tab with one flat colour. Each tab fill fades vertically, from a
lighter head to the sampled colour at its foot, like the bar. The sampled
colours (`#73608D` chosen, `#2B2D38` idle, and the derived `#84719F` hover) are
the bottom stop, so a tab still reads as the reference's fill. The head is a
derived constant, about twelve points of lightness above each (`tab_active_top`
`#9281A9`, `tab_idle_top` `#46495B`, `tab_hover_top` `#A496B8`), all in
`theme.hpp`. The 1 px rule and the close x are unchanged.

Dock tabs are drawn by `TabItemBackground` into the dock node's host window,
which for a dock space is a child window of the desk, and for a floating node
its own. `shade_dock_tabs` runs after the dock space is submitted, walks every
visible dock node's tab bar, and calls `shade_tab` per tab rectangle. That
recolours the vertices that have one of the three fills by their height. The
shapes are not replaced, so the rounded top corners and the antialiased fringe
are ImGui's. The fade runs between the solid vertices, which sit half a pixel
inside the shape, so the head stop lands on the top edge and the sampled fill
on the bottom edge. It adds no vertices and allocates nothing.

## Docked panes as cards

Floating windows round at 8 px through `WindowRounding`; docked ones were square.
ImGui (v1.92.9b docking) draws a docked window's fill into its dock node's host
window and rounds it only at the corners it shares with the dock space
(`CalcRoundingFlagsForRectInRect(bg_rect, host->Rect(), 0)`), so a pane beside
another stays square. With `PassthruCentralNode` the space also paints its whole
rect in `WindowBg` behind the panes, rules each splitter in the border colour,
and fills the tab strip with `TitleBg` at square corners. None of that has a
style knob, and the vendored sources are not patched.

What the desk does instead:

- `DockingSeparatorSize` is `theme::dock_gap` (6 px), the room between panes.
- Around `DockSpace` the desk pushes a clear `TitleBg`, `TitleBgActive`,
  `WindowBg` and `Border`, so ImGui paints no strip, space fill or splitter rule.
  A splitter still shows under the pointer, in the resize grip colours.
- `paint_dock_cards` then paints each docked leaf node of the desk's space into
  its host window's background channel: the strip (`tab_bar`) rounded at the top,
  the body rounded at the bottom, or at all four corners when the
  node shows no strip. The body is filled by `fill_gradient` with
  `theme::window_gradient`, the colours a floating window's fill gets, over the
  body's own rect (below the strip), so each card has its own top and foot and
  the rounded corners stay ImGui's rounded path. It adds no steady-frame
  allocation beyond the fill's vertices. The radius is `theme::dock_rounding`, the tab's 6 px, so
  the first tab's corner sits in the strip's.
- A window docked in the desk is begun with a zero background alpha and a clear
  `ScrollbarBg` (read from last frame's dock state), so its own square fill and
  scrollbar track do not show over the card.
- A window docked in a floating node, and every floating window, is unchanged.

Not verified in a window: how the gap and the 6 px corners read on a monitor, the
splitter highlight, and a drag over the desk. How the gradient reads on a card
next to a floating window is likewise unchecked.

## Where it differs

- The hover fill `#84719F` is derived: the reference shows no hovered tab. It is
  a step lighter than the chosen fill.
- The content under a tab is not `#191A23`. The reference's panels share the
  strip's colour, so the tab and the content are one surface. Here the strip is
  `#191923` but a docked window keeps its card fill (navy, `bg_window`), so the
  join is the rule alone. Matching it needs every window's surface retinted,
  the whole-theme retheme the chart look declines.
- Tab height depends on the 6.5 px padding being right for the 15 px font. A
  different font size gives a different height.
- The close x has no hover disc in the reference to sample, so ImGui's default
  disc (button hover colour) stays.

## Not verified in a window

The tests draw frames headlessly and check vertex colours, corners, tab bar
height and gaps. How the tabs read on a real monitor, whether the icon sits at
the reference's offset in the mono face, and the dock node strip under a drag,
are not checked.
