# The instrument catalogue

## What it is

`marketdata.v1.ops.list_instruments` answers with **instruments**, not series.
An instrument is the deployment's own grouping of the series it holds into the
thing a person reads: a curve is one row and not one row per pillar, a pair is
one row with no axes, a surface is one row whose two axes are its expiries and
its strikes. Measured against `prime_origin` on 2026-11-13:

```
catalogue: 46 instruments
  kind='curve' count=20
  kind='pair' count=26
kind='curve' name='AUD-AONIA' type=discount quote=rate ccy=AUD stale=0
  axis 'term' holds 7 values: 3M 6M 12M 2Y 5Y 10Y 30Y
  source provider='ore' kind='reference' stale=0 age=3s
kind='pair' name='AUD/USD' type=fx_spot quote=rate ccy=USD pair=AUD/USD stale=1
  source provider='bloomberg' kind='data_vendor' stale=1 age=0s
```

This is the whole reason the screen was redesigned. The old browser grouped
series by URI on the client, so one curve appeared once per party that held it
and a swap ladder appeared once per forward start; every attempt to explain the
extra rows lived in client code that the next read contradicted. Here the
deployment answers 20 curves because there are 20 curves, and `AUD-AONIA`
carries its seven pillars as an axis rather than as seven rows.

## What the client does with it

`session_client::list_instruments` reads the pages until the deployment's own
`total` is reached, and returns one `market_instrument` per row:

- `kind` — `pair`, `ladder`, `curve` or `surface`, the server's word, drawn as a
  badge and never derived here;
- `name` — unique within the party and the kind, which is what a person types
  into a search;
- `axes` — the shape's axes in draw order, values as the server ordered them, so
  a plot draws them without sorting;
- `sources` — one per provider behind the instrument, each with its own `as_of`,
  `age_seconds` and `stale`, judged against the threshold for its kind;
- `stale` — true when every source is, or when there are none.

Two instants are converted at the boundary, so no window handles a `time_point`
for a market fact.

## The filter is the deployment's

`query`, `kinds` and `provider_code` are applied by the server, and the read
proves it rather than assuming it:

```
query='AUD/USD' -> 1 instruments
kinds={curve} -> 20 instruments
```

That matters for the screens: a client that filtered a cached list would need
its own rule for what a search matches, and that rule would disagree with the
one `ores.web` uses and with the deployment's own. The client has no matching
rule at all.

## Two things worth knowing before drawing the screens

**The kind's only source may be a reference provider.** `AUD-AONIA` is quoted
through `ore`, whose provider kind is `reference`, and that source is *current*
(`stale=0`, age 3s). So a screen that hid reference providers by kind would hide
every curve in the deployment while claiming they were definitions with no data.
The rule has to be about whether a source has data, not about what kind of
provider it is — or the deployment has to stop seeding curves under `ore`.

**Staleness is per source and already decided.** `AUD/USD` is stale because its
only source is a vendor with no observations; the instrument is stale because
every source is. Whether that is worth a badge is a design question, but the
verdict is not one this client may recompute.

## How to run the probes

Both need the live environment: URL, prefix, CA, client certificate, key,
principal and password.

```
build/scripts/build-target.sh hedgr.session.tests
hedgr.session.tests "[.live-instruments]"
```

The suite is built against the OreStudio checkout that carries these operations;
see the note in `build/scripts/build-target.sh`.
