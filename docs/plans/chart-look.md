# Chart look

The reference is `docs/samples/3dc3688ce58c780167ddf6310cf6953df12318ce-1600x938.png`,
a grid of dark plots. Each has a title at the top left, a rounded group of
periods at the top right, and a plot with no frame.

## Copied from the reference

- The period selector is in the header, at the right end. It is a rounded group
  of segments with the chosen one lifted, and a small dropdown at its end.
- The plot has no frame, no border, no axis line and no tick marks.
- The grid is faint dashed horizontal rules and nothing vertical.
- Y labels sit on the right in the dim text colour, with room between them and
  the plot. X labels are few and read as clock times or dates.
- The last price is a filled chip on the right edge, with a dotted line across
  the plot at that level.
- The series is a thin line with the soft gradient fill under it.

## Where it differs

- The reference has 1m 15m 1h 4h 1D and a dropdown holding 5m. Hedgr's spans are
  1m 5m 15m 1h 1d All, with no 4h. The segments are 1m 15m 1h 1d and the
  dropdown holds 5m and All, so the span list and its saved tokens are
  unchanged. The new default span (5m) is in the dropdown and shows there as the
  chosen one.
- The line is the theme's accent blue, not the reference's purple, so the plot
  agrees with the rest of the navy palette. A rising or falling series still
  shows its direction in the header's change pill. Candles keep green and red.
- The chip is ImPlot's tag, which has a small arrow on its left, not a plain
  rounded box.
- The title is the series pill, with the price and change beside it, because a
  chart here is one series with a live price. The reference has a text title.
- A narrow window loses the end of its header text before it loses a control,
  as before. The "Points" caption goes first.
- The reference's chart windows hold no foot. Hedgr's keep theirs for style, EMA
  and the data health, and lose only the period combo.

## How it is built

- `widget/segmented.hpp` is a generic segmented control with an overflow
  dropdown. It draws into the window draw list and allocates nothing while the
  dropdown is shut.
- `chart_span_shown` in `market_view.hpp` names the spans that get a segment.
- The chart header draws the selector through `begin_header_row`, and a change
  goes through `chart_view_changed`, so the period is saved per chart as the
  foot combo's choice was.
- The plot style is ImPlot's: axis flags `NoGridLines`, `NoTickMarks` and
  `NoHighlight`, plot flag `NoFrame`, and pushed colours and padding. ImPlot has
  no dashed grid, so the dashed rules and the dotted last price line are drawn
  into the plot draw list at the Y ticks ImPlot chose.
- Every colour, length and count is a constant in `theme.hpp`.

## Not applied

The period selector is for a chart. The term structure window's only control is
how many curves to overlay, which is not a period, and the CRM has none. Neither
changes.
