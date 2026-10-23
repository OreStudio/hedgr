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
