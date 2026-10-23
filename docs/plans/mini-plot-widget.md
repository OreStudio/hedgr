# Mini plot widget

A mini plot is a small card for one series, a window of its own that floats at a
fixed size and snaps to a grid beside other cards. It is not a chart in a small
size.

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
- Docking, the dock space, X11 tear-out (#74, #79) and Undock all (#81) serve
  charts. Cards do not dock (step 5).
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
A floating card keeps its title bar, which is how it is moved and closed,
and opens at 240x180 (step 5 fixes that size), where charts open at 1000x620
and stop at 360x220. The line always ends above the foot row, rising behind the price when
the rows leave little room. The tests read the
draw list vertices (line and fill by colour and the white pixel uv), not text.

### 4. Make one, open one (done)

A "Mini plot" action on a full chart's header and in the series browser opens a
card for that series. Clicking a card focuses or opens the full chart for it.
Check: a click test for each action, and that one series gives one card unless
asked twice (D4).

What differed. All three opens go through one `request_series_window`, which
the browser's chart open now uses too. A chart's header gets one flat icon,
Apps (`icon::dashboard`), left of Dock, and a docked chart keeps it though it
has no Dock. A browser row gets a narrow fourth column holding the same icon,
flat and dim, over a row selectable that now allows overlap. A card's tile is
one invisible button, so a press on it never moves the window. A click is a
release on the tile with no drag past ImGui's threshold, which leaves the
floating title and the docked tab as the only handles for moving it. One card
per series and focus on a second ask are the runtime's, by the window id from
step 2. The UI tests check the command each click pushes, and that two clicks
push the same one. The runtime half is `market_feed_tests` (watchers stay at
one on a second open), which could not build here for the `producer_kind`
break in the OreStudio tree.

### 5. A grid from snapping (done, replaces docking)

Cards never dock. They float at one fixed size (240x180) and snap to a grid.
Check: a card dragged onto a dock target stays floating, its size survives a
resize attempt, a drop lands on the grid, and a restored ini that docked a card
brings it back floating. Frame cost on a steady frame stays at zero allocations.

What differed. The first version of this step docked cards and hid their tab
bar, which failed in a live run: dock nodes share one rectangle, so every split
shrinks the tiles it touches and docking cannot hold a fixed size. A card is
now `NoDocking` and `NoResize`, which also undocks a card whose saved settings
say docked, and its size is set every frame from `theme::mini_plot_width` and
`mini_plot_height`. The grid anchors at the main viewport's work position with a
pitch of the card plus `mini_plot_grid_gap` (4px). When a drag of a card ends it
moves to the cell nearest where it was dropped, or the nearest free cell when
another card holds that one. A card with no saved position opens in the first
free cell, scanning left to right then top to bottom. `cell_for`, `snap_cell`,
`snap_position` and `first_free_cell` are pure and take the occupied cells, so
the rule is tested without ImGui. The workspace reads the other cards' cells
from their windows, or their saved settings before they are first drawn, only
when a card opens or a drag ends. Because the tab and the title bar are gone, a
card closes from a small X in its top-right corner, drawn while the pointer is
over the card, or from the right-click menu. The X is a button, so a press on it
neither drags the card nor counts as a click that opens the chart.

### Follow-up: header strip and tile (done)

Three changes from a live run. The card has no title bar
(`ImGuiWindowFlags_NoTitleBar`): its body is the handle, so a drag on it moves the
card (it no longer docks it, step 5), and the right-click Close closes it. The tile is no longer an invisible button, because
that button took the press the window needed to move; a click is a release over
the window that never passed ImGui's drag threshold. The card draws its line and
fill in the chart's colour (`chart_line_colour`) and only the change pill keeps
the good and bad tones. The period is no longer fixed (D2 changes): the state
holds `span_ns`, the span the card was opened with, and the card shows the span
of the open chart of the same series and session when there is one
(`mini_plot_span`), else its own. The Mini plot icon on a chart opens the card
on the chart's span, and the browser opens it on the default chart span. The
label is the chart's ("5m", "1d", "All"). A card does not ask for history, so
its line is what the series already holds for that span.

## Decisions

- **D1. A card is a window of its own.** Recommended. The alternative is one
  "Market favorites" window that holds cards inside it, which is a grid by
  construction and gives up docking a single card beside a chart.
- **D2. The period follows the chart.** Was a fixed 24 hours. A card has no
  control for it: it takes the span of the open chart of its series, else the
  span it was opened with.
- **D3. Contents.** Pair with flags, price, line, period, change. No star, no
  volume, no venue. Recommended.
- **D4. One card per series.** Making a second for the same series focuses the
  first. Recommended. The alternative lets you make several.
- **D5. No auto-tidy.** Recommended for now. The snap grid keeps cards tidy as
  they are dropped, and there is no action that rearranges them.
- **D7. Cards snap, they do not dock.** Dock nodes share space, so docking
  cannot hold a fixed size. A card floats at 240x180 and snaps to a grid. The
  alternative was a custom grid container window, which reimplements docking.
- **D6. Click opens the full chart.** Recommended. Double click and a menu are
  the alternatives.
