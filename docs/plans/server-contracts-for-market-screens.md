# Server contracts for the market-data screens

**Status.** Written before any screen is built, at the operator's instruction:
the same screens will be built on `ores.web`, so no derivation may live in a
client. This page is the gap between what the signed-off prototypes need and what
the deployment answers today, with the operation names taken from
`ores.marketdata.api/messaging/operations_protocol.hpp` in the OreStudio checkout
(2026-11-13).

## The rule

A client may decide **how** something is drawn. The server decides **what it
is**: what an instrument is, which series make it up, what its axes are and in
what order, what its values mean, what its conventions are, and whether it is
stale. Every one of those is currently derived in `hedgr`, and every one would
have to be written again in `ores.web` — which is the duplication this page
exists to stop.

## What the deployment already states, and we ignore

| Operation | What it gives | What we do instead |
|---|---|---|
| `marketdata.v1.ops.list_series_identities` | every series the caller can see, with its **typed identity** and its **declared axes** | we read `market_series.list` and parse the `oresmd` URI in the client (`oresmd_uri_codec`), build labels, and group curves by identity ourselves |
| `marketdata.v1.ops.resolve_series_identity` | a series/object named by its typed identity | we build URIs for tick reads |
| `marketdata.v1.ops.parse_oresmd_uri`, `compose_oresmd_uri`, `describe_oresmd_schema` | the schema and the identity grammar, from the server | we carry a second copy of that grammar in `series_label.cpp` |
| `marketdata.v1.ops.get_series_slice` | one component of a composite object over time, with **`coordinate_field` + `coordinates`** stated once for the whole response | for a curve we use `get_series_evolution`; for a surface smile we have nothing |
| `marketdata.v1.ops.get_series_evolution` | the whole object as instants × nodes, each node carrying its coordinate labels | used for curves — the one read we already take as the server states it |
| `marketdata.v1.ops.get_published_fx_forward` | published FX forwards | unused; the montage would need it |
| `marketdata.v1.ops.get_fixing`, `get_fixing_window`, `get_observation_window` | fixings and observation windows | unused |

**The axes are the important one.** The server states an object's axes and their
order in both slice and evolution reads. `hedgr` still re-derives that order
(`curve_table::pillar_order`) and re-invents labels. That is the first thing to
delete, not the first thing to port.

## What is missing, screen by screen

### Search (instruments)

`list_series_identities` lists **series**, not instruments: one entry per series,
with axes, but no grouping into a curve or a surface, no display name, no
provider facet, no currency/pair facet, no kind, no freshness. Today `hedgr`
groups them (`render::curve_of`) and names them (`series_curve_label`).

Needed — one operation, the one both clients will call:

```
marketdata.v1.ops.list_instruments
  request:  party_id, query (currency/pair/name), kinds[], provider, limit, offset
  response: instruments[] {
      instrument_id              stable, not a URI the client builds
      kind                       pair | curve | surface | ladder | fixing
      name                       display name, server-built, unique within kind
      currency / base_currency / quote_currency
      instrument_type, quote_type, forward_start
      axes[] { field, values[] }  the shape, in the order to draw
      sources[] { provider_code, provider_kind, series_id, as_of }
      party_id
      stale                     verdict, not a threshold for the client to apply
    }
```

That single contract replaces: URI parsing, label building, curve grouping,
provider joining, and the party filter — in both clients.

### Rates ladder

Nothing states a ladder. `get_series_evolution` is per series, and a provider's
ladder is several series (one per forward start and tenor). The **maturity dates**
the prototype shows cannot be derived without the calendar and day count, which
are server facts.

```
marketdata.v1.ops.get_rate_ladder
  request:  party_id, currency, instrument_type, forward_start, as_of,
            providers[], staleness_window
  response: rungs[] {
      tenor, maturity_date, year_fraction,
      quotes[] { provider_code, value, as_of, stale, source_series_id }
    }
```

The server decides the rung order and which provider wins where a rung is quoted
twice; the client draws columns.

### Curve inspector

`compute_curve` answers `{point_id, date, discount_factor}` and nothing else, and
only for a bootstrap config. The prototype's columns — year fraction, zero rate,
implied forward, discount factor, shift, maturity, market pillars vs bootstrapped
points, and the conventions bar — are all derivable server-side from the curve's
own conventions, and none of them are stated.

```
marketdata.v1.ops.get_curve_analytics        (or compute_curve extended)
  request:  party_id, curve_id (or instrument_id), as_of
  response: conventions { index_name, day_count, compounding, interpolation,
                          spot_lag_days, calendar, pillars }
            market_pillars[] { tenor, maturity_date, year_fraction, rate, as_of,
                               source_series_id, provider_code }
            bootstrapped[]   { tenor, maturity_date, year_fraction,
                               discount_factor, zero_rate, implied_forward,
                               shift_bps }
            published_at, publish_id
```

The zero rate, forward and discount factor are **curve mathematics**: two clients
implementing them would drift, and one of them would be wrong about compounding.

### FX montage

Per-provider spot is `get_series_ticks` once per series, joined client-side, with
the change and the day's high/low computed client-side. `get_published_fx_forward`
exists but is not a montage.

```
marketdata.v1.ops.get_fx_montage
  request:  party_id, base_currency, providers[], as_of
  response: pairs[] {
      pair, spot_by_provider[] { provider_code, value, as_of, stale },
      change_pct, day_high, day_low
    }
```

### Vol surface

`get_published_fx_vol_surface` gives `expiries`, `strikes` and a row-major grid —
enough to draw the carpet, and not enough for the rest of the prototype: no
conventions or model parameters, no statement of what a strike *is* (delta
convention, premium-adjusted or not), no smile slice, and its axes carry no order
guarantee the client can rely on.

Needed: the surface response carries `model`, `strike_convention`,
`delta_type` (spot / forward / premium-adjusted), `conventions`, and the axes as
`axes[] { field, values[] }` like every other instrument. The smile is then
`get_series_slice` with `component_field = strike_label` — the operation that
already exists and already states its coordinates once.

### Staleness

`fx_health_thresholds` and `ir_health_thresholds` are **client constants**
(`hedgr.model/domain/health.hpp`), and every screen computes its own verdict from
`as_of`. Two clients, two thresholds, two verdicts on the same value. The
deployment should state `stale` per value (as above), with the threshold in
configuration, so a desk sees one answer.

## What stays in the client

- Layout, columns, wording, colour, window management, and which pane is open.
- Sorting *within* a stated order (a reader may sort a ladder by age; the ladder
  it sorts is the server's order).
- Caching and the tick stream, which is a transport concern and already shared.

Nothing else. In particular, no URI assembly, no identity parsing, no tenor
parsing, no curve grouping, no analytics.

## The order to do it in

1. **`list_instruments`** — unblocks Search and puts the instrument identity on
   the server. Both clients then share one definition of what a curve is.
2. **`get_rate_ladder`** — unblocks the Ladder, and its maturity dates are the
   calendar the client cannot know.
3. **`get_curve_analytics`** — unblocks the inspector's columns and its
   conventions, which are the numbers a desk argues about.
4. **`get_fx_montage`**, then the surface metadata and the smile through
   `get_series_slice`.
5. **Staleness as a server verdict**, last, because it touches every screen.

## What this means for the plan

Steps 1–5 of `market-data-ux.md` assume a client that groups and derives. They
become: call the operation, draw the answer. The work in `hedgr` shrinks to
windows and drawing; the work in OreStudio grows to the five contracts above.

## Decisions I need

1. **Who writes the server operations** — the OreStudio side, or me in the local
   OreStudio checkout (`/home/marco/Development/OreStudio/ores_dev_brave_hopper`)?
2. **Do we wait** for `list_instruments` before touching the browser, or ship the
   Search on today's `list_series_identities` and move the grouping when the
   operation lands? (My preference: wait for the one operation, because it is the
   definition of the thing, and porting the grouping to `ores.web` afterwards is
   the duplication we are trying to avoid.)
3. **`compute_curve` extended, or a new `get_curve_analytics`?** Extending keeps
   one operation and breaks its purity — it currently writes nothing and returns
   DFs only; a new one keeps "preview a publish" and "analyse a curve" apart.
