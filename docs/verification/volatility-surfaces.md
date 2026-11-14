# Volatility surfaces

## What changed

A FX volatility surface is one calibrated object a deployment publishes under a
blueprint. Hedgr now lists the blueprints with the rest of the market catalogue,
opens one as a window, and draws it as a surface.

The window draws the grid as a **carpet**: strikes run left to right along the
front edge, expiries recede up and to the right, and the volatility is both the
height of the mesh and its colour (dark blue at the low end, light violet at the
high end). The projection is axonometric rather than perspective, because a
reader comparing two heights is comparing two columns, and perspective would
shorten the far corner for no reason a desk cares about. The farthest expiry is
drawn first and the nearest last, so a cell that stands in front of another
covers it; the depth order is fixed by the projection, so nothing is sorted per
frame.

## The model

`model::state::surface_table` is the twin of `curve_table`:

- **two shared axes**, both of them `curve_axis` - an ordered set of names,
  positions and C string labels - so the surface reuses the curve data model
  rather than growing a second one;
- **a grid**, row-major with one row per expiry, exactly as a curve stores one
  row per instant.

An axis is shared by every surface quoted on it, and both are interned on their
names. The **expiries** are put in tenor order once, when the grid is stored, and
the values are permuted with them, so a plot never sorts. The **strikes** keep
the deployment's order: a delta ladder runs put to ATM to call, and sorting its
names alphabetically would draw the smile inside out.

A grid whose value count does not match the shape its axes state is stored as a
shape with no grid rather than as a grid with the wrong shape, and a failed read
keeps the previous grid and records the sentence - a refresh that fails does not
erase a good surface.

## The data path

1. `list_fx_vol_surface_blueprints` travels with the market catalogue, beside the
   published curves and for the same reason: one refresh fills every list the
   browser offers. A deployment that publishes no blueprints answers with an
   empty list, which is a fact and not a failure.
2. Opening a surface pushes `load_vol_surface` with the blueprint. The window's
   state names the blueprint, its name and the pair.
3. `run_surface_read` makes one call: `get_published_fx_vol_surface` at the
   clock's now, which answers with the newest surface published at or before that
   instant.
4. The answer's axes, grid and instant go into `surface_table`; a refusal goes in
   as the row's error and the window draws the deployment's own sentence.

## Verified

- `hedgr.model.tests`: a surface interns once and is found again; a grid whose
  expiries arrive out of order comes back in tenor order with the values moved
  with them; the strikes keep their order; a short answer is a shape with no
  grid; a failed read keeps the grid and bumps the version; two surfaces on one
  grid share both axes.
- `hedgr.ui.tests`: a surface window is "reading" and draws no mesh before an
  answer, draws a mesh of hundreds of vertices once the grid is there, and draws
  ERROR with the deployment's sentence when the read was refused. The painted
  window (`[.gallery]`, `vol-surface.ppm`) was read to check the projection: the
  mesh, both axes' names and the range legend.
- `hedgr.runtime.tests`: every window kind still has its row in the traits table -
  a volatility surface reads on open and re-reads on refresh, like a curve.
- `hedgr.session.tests "[.live-surfaces]"` against `brave_hopper`: 10 SSVI
  blueprints (`USDJPY_SSVI`, `GBPUSD_SSVI`, `EURUSD_SSVI`, …) over 7 expiries by
  5 strikes, and **none published**: the read answers "No surface of
  USDJPY_SSVI is published at or before …". The blueprint list is tenant-scoped,
  so `super_admin` sees the 10 and `tenant_admin@acme_corporation` sees none.

## What is not here

- No history: the window draws the newest surface only. A curve keeps 32
  instants because a desk overlays them; a surface has no such gesture yet.
- No calibration: publishing a surface is a write on the deployment, and the
  window only reads one.
- No cross-section: a reader cannot slice the surface at one expiry as a smile
  curve, which is the obvious next gesture on this screen.

## 2026-11-14: "vol surfaces give errors"

The operator reported that every surface window shows an error. The log said:

```
W The surface of blueprint 'de2f…' (EURUSD_SSVI) could not be read:
  the deployment refused the tick read
```

Two separate things were wrong with that line, and one thing was right.

**What was right:** nothing is published. The read was repeated at three
instants, so a publish stamped in the future — a skewed seed clock, a synthetic
feed — could not be mistaken for a missing one:

```
at now:             No surface of USDJPY_SSVI is published at or before 2026-11-14T01:22:15Z
at ten years ago:   No surface of USDJPY_SSVI is published at or before 2016-11-16T01:22:15Z
at ten years ahead: No surface of USDJPY_SSVI is published at or before 2036-11-11T01:22:15Z
```

Ten blueprints exist on `prime_origin` and on `brave_hopper`; not one of them
has ever been published. So the screen has nothing to draw, which is a fact
about the deployment.

**What was wrong:** the client hid the deployment's own sentence in both places
a person looks.

- The worker logged `error.message` ("the deployment refused the tick read")
  and dropped `error.detail`, which is the sentence that says *why*. The same
  mistake was in seven other workers: each chose between the two strings instead
  of reporting both.
- `read_refused`'s canonical sentence named a tick read for every read: a
  surface, a curve, a ladder and an instrument catalogue all reported "refused
  the tick read", which sends a reader to the wrong place.

Both are fixed: `session_error::describe()` joins the code's sentence and the
deployment's, `read_refused` now says "the deployment refused the read", and
every read worker reports through it. The window and the log now say:

```
the deployment refused the read: No surface of EURUSD_SSVI is published at or
before 2026-11-14T01:22:15Z.
```

**What is still wrong, and belongs on the server:** "nothing is published" is a
state of the deployment, not a failure, so painting it red is the wrong end
state for a screen. The write shape of a fix is a typed distinction rather than
a message a client would have to parse:

- `get_published_fx_vol_surface` answers `success = true` with an empty grid and
  a message, so the window draws its NO DATA state with the deployment's
  sentence; or
- the response gains a field such as `published = false`, so the client can tell
  "this blueprint has never been published" from "this read was refused".

Until then the window shows the deployment's sentence in its error state, which
is honest and actionable, and the chooser keeps offering the ten blueprints
because the blueprint list is the only list there is.
