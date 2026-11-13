# Server contracts: the implementable specification

**Status.** Specification, 2026-11-13, written to be handed to an agent that will
implement it in OreStudio. It is the companion to
`server-contracts-for-market-screens.md`, which says *why* each operation is
needed; this page says *what* to build, field by field, with acceptance criteria.

Nothing here is implemented. Where a rule is a decision rather than a fact, it is
marked **DECIDE** so it is not mistaken for something read off the deployment.

## Ground rules for every operation

These are the conventions the existing operations already follow; a new operation
that breaks one is a defect, not a variation.

- **Where it lives.** Request/response structs go in
  `projects/ores.marketdata/api/include/ores.marketdata.api/messaging/operations_protocol.hpp`,
  beside the others, with `using response_type = struct ..._response;`,
  `static constexpr std::string_view nats_subject = "marketdata.v1.ops....";` and
  `static constexpr bool requires_session = true;`. The implementation belongs in
  the marketdata service project that already serves `operations_protocol.hpp`;
  follow the nearest existing operation as the template rather than inventing a
  layout.
- **Permissions.** Each operation needs its own permission code, as the existing
  operations have (the protocol notes that `compute_curve` and
  `republish_curve` are split precisely because their permission codes differ).
  A read gets a read permission; nothing here writes.
- **Party.** `party_id` is **optional** on every request. Empty means "the
  session's own party", resolved by the server from the token. A client never
  decides which party's data it sees.
- **Times.** `std::chrono::system_clock::time_point`, as the existing operations
  use. `as_of` empty/zero means "now", resolved server-side.
- **Numbers.** Rates and volatilities are `double`, in the unit the field names
  (`rate` = decimal, not percent; `vol` = decimal). A client formats; the server
  never sends a formatted number.
- **Coordinates.** Axis values are `std::string` in the spelling the shape stores
  (`"1W"`, `"ATM"`, `"DEL/Spot/Put/25"`), never a display name and never a
  number the client must parse.
- **Order.** Every array that a reader sees in order has a stated order that the
  server guarantees. The client may re-sort for its own convenience but must not
  need to.
- **Pagination.** `offset`/`limit` with `total` in the response, as
  `list_currencies` and `list_published_curves` already do. `limit = 0` means the
  server's default, not "everything".
- **Errors.** `bool success` and `std::string message`, with `message` being the
  sentence a person reads. An operation that cannot answer returns
  `success = false` and a message that names what is missing — never an empty
  list standing in for a failure.
- **Staleness.** Every value block carries `as_of`, `age_seconds` and
  `stale`. `stale` is the server's verdict against its configured threshold; a
  client may display it and must not recompute it.

## 1. `marketdata.v1.ops.list_instruments`

The instrument catalogue. **This is the definition of an instrument**, and the
one both `hedgr` and `ores.web` will read.

Request:

| Field | Type | Meaning |
|---|---|---|
| `party_id` | string | optional; empty = the session's party |
| `query` | string | optional; free text matched against currency, base/quote, name, provider |
| `kinds` | vector\<string\> | optional; `pair`, `curve`, `surface`, `ladder`, `fixing` |
| `provider_code` | string | optional; only instruments with a source by this provider |
| `offset` | uint32 | pagination |
| `limit` | uint32 | pagination |

Response: `success`, `message`, `total`, `truncated` (as `list_series_identities`
already has), `instruments[]`.

`instrument`:

| Field | Type | Meaning |
|---|---|---|
| `instrument_id` | string | **stable identity**, unique per party; see ID rule below |
| `kind` | string | one of the five above |
| `name` | string | display name, unique within `kind` for the party; see naming rule |
| `currency` | string | the single currency of a rate instrument, or empty |
| `base_currency`, `quote_currency` | string | an FX pair's two sides, or empty |
| `instrument_type`, `quote_type` | string | the schema's spelling (`ir_swap`, `rate`) |
| `forward_start` | string | a rate ladder's start (`0D`), or empty |
| `axes` | vector\<axis\> | the shape; see below |
| `sources` | vector\<source\> | who carries it |
| `party_id` | string | the owning party |
| `stale` | bool | every source is stale, or there are none |
| `as_of` | time | the newest source's instant, or zero |

`axis { field: string, values: vector<string> }` — `field` is the oresmd field
code (`tenor`, `term`, `expiry`, `strike_label`, `fwd_start`), `values` is the
declared values **in the order to draw**.

`source { provider_code, provider_kind, series_id, as_of, age_seconds, stale }` —
`provider_kind` from the provider table (`reference`, `data_vendor`, …), so a
client can hide reference definitions without a second read.

**Grouping rule (DECIDE, but this is the intended one).** Series are grouped into
one instrument when they share asset, subject, instrument type, quote type, party
and provider, and differ in exactly one identity field; that field is the
instrument's axis and its values are the axis values. A group that differs in no
field is a single-value instrument (a pair). A shape whose axes come from the
schema (`list_series_identities` already returns `axes`) is one instrument, not
one per node.

**ID rule.** `instrument_id` must be stable across restarts and publishes, and
must not be a URI the client builds. Suggested form: a hash or a UUID minted when
the instrument is first derived, stored server-side. A client may cache by it.

**Naming rule.** The name must be unique within the party and kind, because it is
what a person types into the search. The failure this fixes: three curves called
`IR AUD IR_SWAP` and five called `AUD-AONIA`, differing only by forward start or
provider. The name must therefore carry whatever field distinguishes peers —
`IR AUD IR_SWAP 0D`, `IR AUD IR_SWAP 3M` — and the source stays out of the name
(it is a facet, not an identity).

**Party scoping.** Only the party's own instruments are returned. The five
`AUD-AONIA` copies on `brave_hopper` are five parties' instruments, and a session
sees one. This is a server decision; the client cannot be trusted with it.

**Acceptance.** Against `brave_hopper`, as `tenant_admin@acme_corporation` acting
as ACME Corporation UK plc:

- `query = "aud"` returns exactly one `ladder` for AUD swaps, one `curve` for
  `AUD-AONIA`, one `pair` for `AUD/USD`, one `pair` for `GBP/AUD` (found from the
  quote side), one `surface` for `AUDUSD_SSVI`;
- no two instruments of the same kind share a `name`;
- every `axes.values` the response states is the order a plot can draw without
  sorting;
- no instrument carries another party's `party_id`.

## 2. `marketdata.v1.ops.get_rate_ladder`

Request: `party_id`, `currency`, `instrument_type`, `forward_start`, `as_of`,
`providers` (vector\<string\>, optional), `limit` (optional).

Response: `success`, `message`, `currency`, `instrument_type`, `forward_start`,
`as_of`, `rungs[]`.

`rung { tenor, maturity_date, year_fraction, quotes[], consensus }`

- `tenor` — the ladder rung, in the shape's spelling.
- `maturity_date` — `std::chrono::year_month_day`. **The calendar is the server's**;
  the client cannot compute this.
- `year_fraction` — the day count's own fraction to maturity.
- `quotes[] { provider_code, value, as_of, age_seconds, stale, series_id }` — one
  per provider that quotes this rung. A provider that does not quote it is
  **absent**, not zero.
- `consensus { value, method, providers_used }` — the value the prototype's
  Consensus column shows. `method` is a word the server chooses and states
  (`median`), so the column can say what it is. **DECIDE**: whether consensus is
  wanted at all; if not, drop the field and the column.

Order: ascending by maturity date. A rung the deployment quotes with a convention
(`ON`, `TN`, `SN`) sorts before `1W` and carries its own maturity date.

**Acceptance.** AUD swaps, `forward_start = 0D`: every rung's `maturity_date` is
after the as-of; rungs are strictly ascending; a provider present in a quote names
a `series_id` that `get_series_ticks` can read; `age_seconds` matches
`as_of - quote.as_of`.

## 3. `marketdata.v1.ops.get_curve_analytics`

Request: `party_id`, `instrument_id`, `as_of`.

Response: `success`, `message`, `instrument_id`, `name`, `as_of`, `stale`,
`conventions`, `market_pillars[]`, `bootstrapped[]`, `published_at`, `publish_id`.

`conventions { index_name, day_count, compounding, interpolation, spot_lag_days, calendar, pillar_count }`

- `day_count` — `ACT/365F`, `ACT/360`, `30/360`, as the curve config states it.
- `compounding` — `continuous`, `annual`, `simple`.
- `interpolation` — `log_linear_df`, `linear_zero`, `piecewise_linear`.
- `calendar`, `spot_lag_days` — what the maturity dates were built with.

`market_pillar { tenor, maturity_date, year_fraction, rate, as_of, provider_code, series_id }`
— the inputs, as the market quoted them.

`bootstrapped { tenor, maturity_date, year_fraction, discount_factor, zero_rate, implied_forward, shift_bps }`
— the outputs, one row per pillar.

**The mathematics, stated so two clients cannot disagree.** For pillar `i` with
year fraction `t_i` and discount factor `DF_i`:

- `zero_rate_i = -ln(DF_i) / t_i` when `compounding = continuous`;
  `-ln(DF_i) / t_i` is also the answer for annual and simple compounding **only**
  if the server converts — the rule is that `zero_rate` is always the continuously
  compounded rate and `compounding` describes the curve's own convention, not the
  column's. **DECIDE**: if the column should follow the curve's convention
  instead, the formula changes and must be written here.
- `implied_forward_i = (DF_i / DF_{i+1} - 1) / (t_{i+1} - t_i)` for
  `i < n-1`; the last pillar's forward is absent (empty optional), not zero.
- `shift_bps = (zero_rate_i - market_pillar_i.rate) * 10000` — the distance
  between the bootstrapped curve and the market at that pillar. **DECIDE**: the
  prototype shows a shift, and this is the only definition that needs no
  calibration history; if the shift is meant as "applied parallel shift", the
  field must be renamed and redefined.

The market pillar and the bootstrapped row for the same tenor are a **pair**, and
the response must make that checkable: same `tenor`, same `maturity_date`.

**Acceptance.** For `AUD-AONIA` (published against `brave_hopper`): the response
names its conventions; `DF` is monotonically decreasing in `t`; every
`market_pillar` has a matching `bootstrapped` row; the newest `market_pillar.as_of`
equals the response `as_of`; recomputing `zero_rate` from `DF` and `t` reproduces
the column to 1e-10.

## 4. `marketdata.v1.ops.get_fx_montage`

Request: `party_id`, `base_currency`, `providers` (optional), `as_of`.

Response: `success`, `message`, `base_currency`, `as_of`, `change_since`, `pairs[]`.

`pair { base_currency, quote_currency, pair_name, spot_by_provider[], consensus,
change_pct, day_high, day_low }`

- `spot_by_provider[] { provider_code, value, as_of, age_seconds, stale, series_id }`.
- `change_since` — **the instant the change is measured from**, stated in the
  response. A client must not decide what "today" means: a trading day starts when
  the server's session says it does.
- `change_pct` — measured from that instant. Absent when there is no price at it.
- `day_high`, `day_low` — over the same window, absent when there is no window.

Order: by pair name.

**Acceptance.** Base `AUD`: `AUD/USD` and `GBP/AUD` both appear (the second is the
base on the quote side, which the montage is for); every `value` names a
`series_id`; `change_pct` is consistent with `change_since`.

## 5. Vol surfaces

**5a. Extend `get_published_fx_vol_surface`.** Its response already carries
`expiries`, `strikes` and a row-major grid. Add:

| Field | Type | Meaning |
|---|---|---|
| `model` | string | `ssvi`, `sabr`, `spline` |
| `strike_convention` | string | what a column name means (`delta`) |
| `delta_type` | string | `spot`, `forward`, `spot_premium_adjusted` |
| `conventions` | `conventions` | as in §3 |
| `axes` | vector\<axis\> | the two axes in draw order, carrying the values the grid is indexed by |
| `published_at` | time | when it was published |
| `stale` | bool | per the server's threshold |

**5b. The smile needs no new operation**: `get_series_slice` with
`component_field = "strike_label"` on the surface's instrument returns one expiry's
strikes with `coordinates` stated once. The gap is that the surface's
`instrument_id` and its axis field names are not exposed today; §1 and §5a close
that.

**Acceptance.** For any blueprint with a published surface: `axes[0].values` and
`axes[1].values` have the same lengths as the grid's rows and columns; the slice
for one expiry returns the same numbers as that row of the grid.

## 6. Staleness

No new operation. Every value block carries `as_of`, `age_seconds` and `stale`,
and the threshold is server configuration. The rule is stated once in the
protocol header so every operation refers to it:

- `age_seconds = max(0, server_now - as_of)` in whole seconds;
- `stale = age_seconds > threshold` for the instrument kind's threshold;
- the thresholds are configuration, with the current `hedgr` values
  (`fx_health_thresholds`, `ir_health_thresholds`) as the starting point.

**Acceptance.** No client threshold remains: `health.hpp` in `hedgr` is deleted
when this lands, and every screen reads `stale` and `age_seconds`.

## What the agent must take from OreStudio itself

This page states the contract, not the house style. An implementing agent must
read, before writing code:

- `projects/ores.marketdata/api/include/ores.marketdata.api/messaging/operations_protocol.hpp`
  — the struct conventions, and the nearest operation to copy;
- the service that serves it — how a handler is registered and where a request is
  dispatched;
- the permission registry — how an existing read operation's code is declared, so
  a new one matches;
- `projects/ores.marketdata/AGENTS.md` (and the repo root `AGENTS.md`) for the
  build, test and commit rules of that repository.

## What `hedgr` deletes when each lands

This is the purpose of the exercise: not a port, a deletion.

| Operation | `hedgr` code it removes |
|---|---|
| `list_instruments` | `series_label.cpp` (URI parsing, label building), `series_browser_view.cpp::curve_of` and the grouping in `update_series_browser_view`, the party filter in the browser, `oresmd_uri_codec` use on the catalogue path |
| `get_rate_ladder` | the ladder's client-side join across member series, `curve_ladder.cpp`'s tick hunting, the tenor ordering in `curve_table::pillar_order` |
| `get_curve_analytics` | nothing yet (the inspector is unbuilt) — and the inspector is built *without* the analytics code it would otherwise need |
| `get_fx_montage` | nothing yet; the montage is built on the operation |
| surface metadata + slice | `surface_table`'s axis ordering and `draw_mesh`'s grid assumption |
| staleness | `health.hpp`, `assess_data_health`, every per-screen threshold |

## Open decisions, collected

1. Grouping rule for `list_instruments` — the rule above, or a server-side
   derivation table? (A table is better for auditability; the rule is faster to
   ship.)
2. `consensus` on the ladder — wanted, and by what method?
3. `zero_rate` compounding — always continuous, or the curve's own convention?
4. `shift_bps` — market-to-curve distance, or an applied shift?
5. `change_since` for the montage — the trading session's open, or midnight UTC?
6. Who implements: OreStudio, or an agent working in the local OreStudio
   checkout?
