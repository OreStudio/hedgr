# Interest rates as term structures

## What changed

The series browser now has an asset-class selector. Choosing `IR` switches the
list from points to term structures. Leaf series that share a curve identity
(the series URI without its `tenor`) collapse into one row, and a composite
curve whose observations carry `term` is one row of its own.

Double-clicking a curve, or choosing it in the combo, opens a term-structure
window like the FX chart window. It has the same chrome: a currency flag, the
curve name, a refresh control, a term count and the update time with its age.
The metadata row above the plot matches FX: the observation date and time, the
term count and a `live` or `stale N s` label. Both IRR and FX rows now carry the
time of the last update. The area under the line is a vertical gradient in the
line's colour, fading to the floor of the plot.

The curve window overlays recent history, as the web simulator does: the
History control selects up to twenty curves, the newest complete curve is bold
blue with the gradient fill, and each earlier curve is a distinct, darker shade
of blue, oldest darkest. The worker groups observations by timestamp and tenor and keeps the last
twenty curves, so a curve that arrives one pillar at a time does not replace a
complete one with a partial snapshot. The plot uses the FX plot flags, grid,
padding and numeric font, with bright axis text and no legend; tenor is the
categorical x-axis. Its refresh button reads the curve again. The read runs on a worker
and its own connection; the frame loop only draws the result.

## Data path

- `series_term` reads the `term` from an observation URI, falling back to
  `tenor`. The synthetic feed writes both on an IR observation -- the index
  tenor and the curve term -- and the curve term is the ladder.
- `series_curve_key` is the URI with those fields removed, so the tenors of one
  curve group together; `series_curve_label` names the curve without a term.
- One IR series is one curve: the ladder lives in its observations, not in the
  series URI. The worker reads the series' observations and takes the newest
  value per term from their URIs.
- `curve_table` holds the latest curve per session, columnar: the pillars once,
  in drawing order, and a row of values per instant. The draw reads a row; it
  sorts and copies nothing per frame.

## Curves the deployment publishes

A deployment also calibrates curves and publishes them as one object each
(`marketdata.v1.ops.list_published_curves` for the names,
`get_published_curve_sets` for one curve's instants). They are not series: each
has a name, the series it is published under and a pillar/value pair per point.

Those curves travel with the catalogue the browser already reads, so one
refresh fills the series list and the curve list together. A failed curve read
leaves the series list standing and records why the curve list is short.

A window holds the curve's series and **no tenor members**, which is what tells
the read to ask the deployment for the curve itself rather than a tail per
tenor. Both sources answer in the same columnar `curve_sets` event, so the
window that draws a published curve and the window that draws a ladder
assembled from ticks share one store, one read path and one plot. A published
curve's pillars are put in the same tenor order, and a pillar a later instant
omits carries the earlier value forward.

The published curves are listed in the browser's IR view, under the curve
table: the tick curves first, then the deployment's own, marked `published` in
the provider column. Double-clicking one opens its term structure.

Against `brave_hopper` on 2026-11-12 the deployment published 20 curves (one per
currency: `TRY-TLREF`, `CHF-SARON`, `AUD-AONIA`, …), seven pillars each, and
`get_published_curve_sets` answered one instant per curve with `discount`
values. `hedgr.session.tests "[.live-curves]"` is the probe.

## The currency flag

The flag on a curve window is read from the series' identity, not from its name.
`identity.scope` is the schema's subject field - the currency of a rate, the base
of a spot - so `IR AUD DISCOUNT AUD-AONIA` shows the Australian flag because the
deployment says the subject is `AUD`. A window that holds only a name and a series
key (a chart, a term structure, a card, a browser row) looks the key up in the
session's catalogue and reads the identity from that row. A published curve is not
a catalogue row and falls back to its name; the fallback is logged once, with the
reason.

Against `brave_hopper` on 2026-11-13 every IR row's identity carried its currency
as the subject (`subject='AUD'`, `subject='USD'`, …), and `OREData`'s fx schema
names `unit_ccy` the subject and `ccy` the quote. `hedgr.runtime.tests
"[.live-curve-shape]"` with `HEDGR_LIVE_ROW` prints both.

## The surface slices reuse this shape

A volatility surface is a curve in two dimensions: pillars become a tenor axis
and an expiry axis, and a row of values becomes a grid. The surface model is
therefore the same shape as `curve_table` - shared axes, interned per session,
a bounded set of instants, row-major values and a version stamp - and the plot
reads a grid rather than a row. Nothing about the curve's read path or its
plot changes to make room for it.

## Verification

Against `clever_dijkstra` with `tenant_admin@acme_corporation`: the asset-class
selector shows `IR`, the list shows curves, and choosing `IR INR IR_SWAP` opens
a term-structure window with the full twelve-point ladder. The capture is
[rates-term-structures.png](rates-term-structures.png).

The synthetic feed is producing rates: the observations for one IR series carry
`term=1M` through `term=30Y`. The bug was on the client, which read only the
newest observation and took the series' index `tenor=1D` as the curve term.

## Not in this slice

Live tick updates. The curve is read on selection and on refresh, as agreed;
watching every tenor and rebuilding the curve from ticks is the next slice.

The published curve's `as_of` is what the health foot ages: a window opened for
a curve the deployment published once shows that instant, and it reads `stale`
rather than pretending the curve is live. The deployment republishes on its own
schedule and the window reads it again on refresh.
