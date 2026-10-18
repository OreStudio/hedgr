# Interest rates as term structures

## What changed

The series browser now has an asset-class selector. Choosing `IR` switches the
list from points to term structures. Leaf series that share a curve identity
(the series URI without its `tenor`) collapse into one row, and a composite
curve whose observations carry `term` is one row of its own.

Selecting a curve reads the latest observation of each member series and draws
the curve inline below the list: tenor on the x-axis, rate on the y-axis. The
refresh button reads the curve again. The read runs on a worker and its own
connection; the frame loop only draws the result.

## Data path

- `series_term` reads the `tenor` or `term` from an oresmd URI.
- `series_curve_key` is the URI with those fields removed, so the tenors of one
  curve group together; `series_curve_label` names the curve without a term.
- The browser groups `IR` catalogue rows by that key. A row with no key is a
  composite curve; the worker reads its observations and takes the newest value
  per term from their URIs.
- `term_structure_table` holds the latest curve per session; the draw copies the
  nodes, sorts them by tenor, and plots them.

## Verification

Against `clever_dijkstra` with `tenant_admin@acme_corporation`: the asset-class
selector shows `IR`, the list shows one row per curve instead of per tenor, and
selecting a curve issues `load_term_structure` and draws the returned node.

That deployment's Acme curves hold one tenor each, so the live capture shows a
one-node curve rather than a ladder. The grouping, term ordering, composite
term extraction and table are covered by the session and model tests; a
multi-tenor curve is the remaining live check when such data is provisioned.

## Not in this slice

Live tick updates. The curve is read on selection and on refresh, as agreed;
watching every tenor and rebuilding the curve from ticks is the next slice.
