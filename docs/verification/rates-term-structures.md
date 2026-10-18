# Interest rates as term structures

## What changed

The series browser now has an asset-class selector. Choosing `IR` switches the
list from points to term structures. Leaf series that share a curve identity
(the series URI without its `tenor`) collapse into one row, and a composite
curve whose observations carry `term` is one row of its own.

Double-clicking a curve, or choosing it in the combo, opens a term-structure
window like the FX chart window. It has the same chrome: a currency flag, the
curve name, a refresh control, a term count and the update time with its age.
The plot uses the FX plot flags, grid, padding and numeric font, with bright
axis text and no legend; tenor is the categorical x-axis. Its refresh button reads the curve again. The read runs on a worker
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
- `term_structure_table` holds the latest curve per session; the draw copies the
  nodes, sorts them by tenor, and plots them.

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
