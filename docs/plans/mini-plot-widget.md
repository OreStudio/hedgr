# Mini plot widget

A mini plot is a small card for one series, a window of its own that docks beside
other cards into a grid. It is not a chart in a small size.

The reference is `docs/samples/9864e339b564bd6ab45c2f4c10b1633585b4a76e-686x370.png`,
a grid of cards. Each card shows the pair with its currency flags, the last
price, a thin line of the recent past, and a period label with the change. There
is no axis, no foot, no controls and no legend. The first mini-plots plan
(`mini-plots.md`) read the request as "a docked chart draws compact", which was
wrong, and this plan replaces its steps 3 and 4 in part.

## Why

A watchlist is read at a glance, many at once. A full chart answers "what is this
doing" and costs a whole window of chrome. A card answers "how are these doing"
in a few hundred pixels, so a desk can hold twenty of them beside one big chart.

## What stays

- A chart is always a full chart: header, plot, foot. It floats or docks as one,
  with ImGui's own tab bar and drag to undock.
- Docking, the dock space, X11 tear-out (#74, #79) and Undock all (#81) are
  generic and serve cards as they serve charts.
- Per-chart period, style and EMA (#75) stay.
- Flags come from the server only (#82).

## What goes

The compact chart layout from #76 and #77: `chart_layout::compact`, the compact
header, its Pop out and close icons, the right-click view menu, the docked path
in `draw_focus_outline` use, and the tests that pin them. The Dock icon on a
floating chart stays.

## Data shape

A new window kind, `mini_plot`, alongside `chart`.

```
mini_plot_state { label, series_id }
```

It follows the chart's persistence: stored with the window row and saved by
`window_service`, so a desk of cards comes back as it was. The period is fixed
for now (D2) so the state is the series alone. `window_kind_count` becomes 11 and
the renderer table gets one entry.

The card reads the series table, not the network, as a chart does. The line
comes from the level `market_level_for_span` picks for the card's span. The
change is the last value against the first value in that span.

## Steps

Each lands on its own with the suite green.

### 1. Remove the compact layout (done)

A docked chart draws as a full chart. Delete the compact path and its tests, keep
the Dock icon on floating charts. Check: the docked-chart tests now assert a full
header, foot and plot, and the frame cost of a docked chart equals a floating one.

### 2. The mini plot window kind (done)

`window_kind::mini_plot`, its state, JSON and persistence, `open` and `close`
through the existing window commands, and an empty card renderer that draws the
name. Check: a round trip through the store, and a headless frame that draws the
card for a held series.

What differed. The state is `{label, series_id}` as planned, the session lives
on the window spec as for a chart. The kind is stored as text, so no schema
change was needed and an old database loads unchanged. The window id is
`mini:instance:account:series`, one card per series per session (D4). The
runtime holds the series through the same `hold_series` a chart uses. Removing
an account or instance releases a card's series and leaves the window, as it
does for charts, so a card does not close with its session. Nothing in the UI
opens one yet, so step 4 owns that. The placeholder card draws the tile, the
label and a dim "Reading the series..." line until the row exists.

### 3. The card (done)

Draw the card: rounded tile on the card surface, the flags and pair chip, the
price in the bold face, a line of the span drawn straight into the window's draw
list (no ImPlot, no axes), the period label, and the change in the good or bad
tone. The line is one polyline over a fixed point budget, so a card costs a few
hundred vertices and no allocations on a steady frame. Check: headless frames
assert the green and red cases, the empty and reading states, and zero
allocations per steady card.

What differed. The line is one polyline over at most 96 points
(`theme::mini_plot_point_budget`) with a vertex-colour fill under it, drawn by
`draw_sparkline` into the window draw list, so a card costs about 600 vertices
whatever the series holds. The span is 24 hours, read from the level
`market_level_for_span` picks, and the change is the last value against the
first in that span. The tone is the chart's `change_tone`, so a stale series
is neutral and a delayed one is amber. The price formatter and `change_tone`
moved from `chart_window.cpp` to `render/series_text.hpp` so both windows share
them. A card too short for the stacked foot puts the period and the pill on
one row. A card's window has
no padding, border or background of its own (`workspace.cpp` sets that for
`mini_plot` only), so the tile is the whole content region, floating or docked.
A floating card keeps its title bar, which is how it is moved, closed and
docked, and a docked one keeps ImGui's tab. A floating card also opens at
240x180 and may shrink to 64x64, where charts open at 1000x620 and stop at
360x220. The line always ends above the foot row, rising behind the price when
the rows leave little room. The tests read the
draw list vertices (line and fill by colour and the white pixel uv), not text.

### 4. Make one, open one

A "Mini plot" action on a full chart's header and in the series browser opens a
card for that series. Clicking a card focuses or opens the full chart for it.
Check: a click test for each action, and that one series gives one card unless
asked twice (D4).

### 5. A grid from docking

Cards dock by dragging like any window. A node of cards hides its tab bar and
gets a small default size so a first dock lands as a tile. Check: five cards
docked into a split tree draw without overlap and keep their positions after a
restart through `imgui.ini`. A "Tidy" action that lays cards out in a grid is
not in this plan (D5).

## Decisions

- **D1. A card is a window of its own.** Recommended. The alternative is one
  "Market favorites" window that holds cards inside it, which is a grid by
  construction and gives up docking a single card beside a chart.
- **D2. Fixed period, 24 hours.** Recommended, so a card has nothing to choose.
  A per-card period needs a control, which the reference does not have.
- **D3. Contents.** Pair with flags, price, line, period, change. No star, no
  volume, no venue. Recommended.
- **D4. One card per series.** Making a second for the same series focuses the
  first. Recommended. The alternative lets you make several.
- **D5. No auto-tidy.** Recommended for now. Docking is the grid.
- **D6. Click opens the full chart.** Recommended. Double click and a menu are
  the alternatives.
