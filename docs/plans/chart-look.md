# Chart look

The reference is `docs/samples/3dc3688ce58c780167ddf6310cf6953df12318ce-1600x938.png`,
a grid of dark plots. Each has a title at the top left, a rounded group of
periods at the top right, and a plot with no frame.

## Copied from the reference

- The period selector is in the header, at the right end. It is a rounded group
  of segments with the chosen one lifted.
- The plot has no frame, no border, no axis line and no tick marks.
- The grid is faint dashed horizontal rules and nothing vertical.
- Y labels sit on the right in the dim text colour, with room between them and
  the plot. X labels are few and read as clock times or dates.
- The last price is a filled chip on the right edge, with a dotted line across
  the plot at that level.
- The series is a thin line with the soft gradient fill under it.

## Where it differs

- The reference has 1m 15m 1h 4h 1D and a dropdown holding 5m. Hedgr's spans are
  1m 5m 15m 1h 1d All, with no 4h. The header has room for all six, so the strip
  shows every one as a segment and has no dropdown. The span list and its saved
  tokens are unchanged.
- The plot type (Candles, Line) and the EMA choice (EMA off, EMA 20) are two
  more strips of the same kind, beside the period strip, where the reference has
  no such controls. They left the foot, which keeps only the identity and the
  data health. The flag pair sits to their right and the Mini plot and Dock
  icons stay at the far right.
- The line, its fill and the last price chip are the reference's purple, sampled
  from it: the line is `#8068d6` (the Price line's commonest saturated pixel)
  and the chip is `#7c59e9` (the Price chip at x 470 y 272). The chosen segment
  is the reference's `#313040` and the grid rules are a purple grey at a low
  alpha. This is a chart palette in `theme.hpp` (`chart_line_colour`,
  `chart_tag_colour`) and not the app accent, which stays the navy palette's
  blue for tabs, ticks and scrollbars. The window chrome, plot background and
  text stay navy: the reference's whole page is tinted violet (`#161220`), and
  matching that is a retheme of every window, not a chart change. A rising or
  falling series still shows its direction in the header's change pill. Candles
  keep green and red, and the EMA keeps its amber.
- The chip is ImPlot's tag, which has a small arrow on its left, not a plain
  rounded box.
- The title is the series pill, with the price and change beside it, because a
  chart here is one series with a live price. The reference has a text title.
- A narrow window loses the end of its header text before it loses a control,
  as before. The "Points" caption goes first.
- The reference's chart windows hold no foot. Hedgr's keep theirs for the
  identity and the data health.

## How it is built

- `widget/segmented.hpp` is a generic segmented control, one segment for each
  option. It draws into the window draw list and allocates nothing.
- The chart header draws its three strips through `begin_header_row`, from the
  right: the icons, the room for the flags, then the strips. A change goes
  through `chart_view_changed`, so the period, the type and the EMA are saved per
  chart as the foot combos' choices were. The flags are drawn at a fixed place
  short of the icons, so a long header text is clipped before it reaches them.
- The plot style is ImPlot's: axis flags `NoGridLines`, `NoTickMarks` and
  `NoHighlight`, plot flag `NoFrame`, and pushed colours and padding. ImPlot has
  no dashed grid, so the dashed rules and the dotted last price line are drawn
  into the plot draw list at the Y ticks ImPlot chose.
- Every colour, length and count is a constant in `theme.hpp`.

## Not applied

The period selector is for a chart. The term structure window's only control is
how many curves to overlay, which is not a period, and the CRM has none. Neither
changes.
