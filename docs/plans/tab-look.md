# Tab look

The reference is `docs/samples/572312177-2d9a6bd9-b2d3-4322-b650-aa904833b128.png`,
a dark app with folder tabs (Statistics, Viewport, Signal Analyzer, and a nested
ImPlot Demo / Dear ImGui Demo strip), a menu bar and a status bar.

## Copied from the reference

- Folder tabs: a rounded top (`theme::tab_rounding`, 6 px), square bottom, one
  pixel outline (`TabBorderSize`).
- The chosen tab is lighter than an idle one and tinted violet; an idle tab is
  dim; a hovered tab tints toward the violet.
- A tab bar whose window lost focus uses the dimmed pair, so the focused strip
  stands out. ImGui only sets that state for dock node tab bars.
- The chosen tab has a violet overline, the chart purple `chart_line`, and the
  bar sits on a one pixel rule that takes the chosen tab's colour. This is the
  docking tab bar's own drawing (`TabBarBorderSize`, `TabBarOverlineSize`, 1 and
  2 px). Tab bars in widgets get the same through the style, and the count
  badge and `underline_selected_tab` now draw in the violet too, since both read
  `ImGuiCol_TabSelectedOverline`.
- The close x is ImGui's flat one, shown on the chosen or hovered tab.

## Where it differs

- The fills are set by eye, not sampled: `tab_idle` `#10131a`, `tab_hover`
  `#2a2548`, `tab_active` `#211e3a`, `tab_dimmed` `#0b0e14`,
  `tab_dimmed_active` `#181a28`. The rest of the window chrome stays navy, as
  in the chart look, so the active tab is not the same colour as the content
  under it. The reference joins the two; here the rule under the strip ties them
  and no more. A fully joined tab needs the content's surface to be violet too,
  which is the whole-theme retheme the chart look declines.
- The reference puts an icon before each tab label. ImGui draws a tab from its
  label alone. A glyph can be put in the label text (the icon font is merged
  into the base font), but the dock tab labels are window titles, so adding
  icons there is a separate change to each window's title.
- The reference's labels are monospace. The base font here is a proportional
  sans (Inter, falling back to DejaVu Sans) and the repo ships no font file:
  `src/icon/fonts.cpp` looks for system or user fonts. A monospace tab label
  would need a bundled font such as JetBrains Mono (OFL) under `assets/fonts`,
  an install rule, a loader entry and a push of that font around tab bars,
  which ImGui does not do for dock node tab bars without a hook. Not done.

## Not verified in a window

The tests draw frames headlessly and check vertex colours and corners. How the
violet reads on a real monitor, and the dock node strip under a drag, are not
checked.
