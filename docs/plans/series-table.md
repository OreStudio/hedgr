# The series table

How market data stops being a map of strings to deques of fat points and becomes
a table of handles, contiguous rings and version stamps, in steps that each land
on their own with the suite green.

This is Part D step 6 of `docs/review/architecture.md` (AR-7, AR-8, AR-1 part
one, AR-6 second half), plus the reader migrations that bundle 11 deferred
(UR-8, UR-10, SR-11, MS-5, MS-6). It is Prerequisite E of
`docs/plans/session-actor.md`, and step 8 of that plan cannot start without it.

Line references are to `origin/main` at `661bde4`. A claim marked "inferred" was
read off the code or a header and not observed running. Sizes are computed from
the structs with libstdc++'s 32-byte `std::string`, not taken from a compiler.
Nothing here was built or run.

## Why

`docs/architecture.md` principle 3 says a window rebuilds derived data only when
the version stamp of its source changes, and principle 8 says rows are addressed
by handle and hot columns are contiguous so a plot can read them without a copy.
Market data meets neither. It is the one hot table the frame loop reads that is
still keyed by strings, stored in deques and addressed by no handle.

The session actor needs three things from it, and Prerequisite E names them. A
stable key for a watched series, so a watch and the bucket that comes back name
it without strings. A place where a term structure's member rings live, so the
curve is derived on the frame thread from N watches and no poll. A version stamp
on each ring, so a bucket that changes nothing costs nothing.

An honest note before the detail. On a desk of 200 charts the string keys cost
the frame thread little today, because the merged bundles already made lookups
allocation free and cached the chart arrays. This plan is not justified by a CPU
number on today's desk. It is justified by what the actor cannot be built
without, by a memory bound that does not exist, and by the term structure, which
today polls on a new connection every 30 s per window.

## What is already fixed at 661bde4

Several review findings were closed by merged bundles. They are listed so the
plan does not redo them.

| Finding | State | Evidence |
|---|---|---|
| MS-6, a heap allocation per lookup | Fixed in #30. `composite_map` is searched with borrowed `string_view`s | `composite_key.hpp` (`composite_map`, `find_in`), pinned by `lookup_cost_tests.cpp:53-69` |
| AR-1 for charts, arrays rebuilt every frame | Fixed in #30. `chart_plot` is kept in the window and rebuilt when `chart_plot_key{version, span, candles, ema}` changes | `chart_plot.cpp:95-98`, `chart_window.cpp:208-214`, pinned by `chart_plot_tests.cpp:90-117` |
| UR-9, the series browser submits every row | Fixed in #30, clipped | `frame_cost_tests.cpp:77-83` |
| C-3, one tick subscription per session | Fixed in #26 | `tick_stream.cpp:147-151`, `tick_sink.cpp:60-75` |
| `market_series_snapshot::version` exists | Yes, one table-wide counter, so a stamp is never reused by another chart | `market_table.cpp:153,174,183`, `market.hpp:196-203`, pinned by `state_tables_tests.cpp:906-945` |

What this means for the plan. The version stamp the finding asked for already
exists on the snapshot. The work left is the key, the storage, the thread
crossing, eviction, the term structure and three readers.

## What exists today, measured

### How market data is keyed and stored

- The catalogue is `composite_map<2, market_catalogue>` keyed by
  `(instance_id, account_id)` (`market_table.hpp:117`). A catalogue is a
  `std::vector<market_series_row>` of nine strings per row (`market.hpp:38-65`).
  It has no version, no index by series id and no generation.
- Points are `composite_map<3, market_series_snapshot>` keyed by
  `(instance_id, account_id, series_id)` (`market_table.hpp:118`). A key is three
  strings, two UUIDs and a series UUID, about 108 bytes of heap per key, stored
  once.
- A snapshot is three `std::deque<market_point>` plus `observed_at_ns`, `failure`
  and `version` (`market.hpp:180-203`). Capacities are 7,200 fine, 2,880 medium
  and 720 coarse points (`market.hpp:105-111`), 10,800 in all.
- A `market_point` is `at_ns`, `value`, `std::string term`, `open`, `high`,
  `low` (`market.hpp:142-171`). That is 8 + 8 + 32 + 24 = 72 bytes, so a full
  series is 777,600 bytes of points before deque block slack. The review's
  "56 bytes" (AR-7) is out of date. MS-5 says the same 72 byte point.
- `term` is written by the backfill (`market_worker.cpp:111`) and by nothing
  else. The term structure worker reads it from the answer of its own read
  (`term_structure_worker.cpp:73`), not from the table. No reader of
  `market_table` reads `.term`. It is dead weight in every ring point.
- The chart identity a window persists is a string. `chart_state::series_id`
  and `term_structure_state::member_ids` are saved as JSON in the
  `window_spec` row's `state_json` (`window_json_io.cpp:57-58,72-73`,
  `window_repository.cpp:67-78`). `chart_state::max_points` is saved too and
  nothing in the runtime reads it (grep: only tests and the struct).

### The tick path, thread by thread

1. The cnats delivery thread of the one party subscription calls the handler
   (`tick_stream.cpp:151-161`). Per tick, for every tick of the party, watched
   or not, it decodes the wire message, parses the value with `strtod`, then
   canonicalises the URI with `oresmd_uri_codec::read`, `series_of` and `write`
   (`tick_stream.cpp:47-58`, called at `:78`). That is a parse and a string
   write per tick. It then calls `tick_sink::on_tick`.
2. `tick_sink::on_tick` takes `mu_`, looks the canonical URI up in
   `std::unordered_map<std::string, bucket_aggregator>`, and calls
   `aggregator.on_tick` (`tick_sink.cpp:60-75`). This lock is the one lock on
   the tick path, and the stream thread takes it for `watch` and `unwatch`
   (`tick_sink.cpp:36-58`).
3. The aggregator keeps one open bucket per level. When a tick starts a new
   bucket it publishes the previous one: `ring_.try_push(market_bucket{.series_id
   = series_id_, ...})` (`bucket_aggregator.cpp:53-57`). `series_id_` is a
   36 character UUID, so that copy is a heap allocation on the delivery
   thread, once per closed bucket. This is AR-8 and it is still true.
4. `bucket_ring::try_push` is single producer. It reads `released_` with
   acquire, writes `slots_[next_ % capacity]` and publishes with
   `published_.store(next_, release)` (`bucket_ring.cpp:25-36`). A full ring
   drops the newest bucket and counts it (`:27-30`). The slot is a
   `market_bucket`: a string, a level and a 72 byte point, 112 bytes, 4,096 of
   them inline (`bucket_ring.hpp:42-46,64,97`), 458,752 bytes plus the heap
   strings.
5. The frame thread consumes. `market_feed::pump` calls `stream->drain(buckets)`
   (`market_feed.cpp:242`), which loads `published_` with acquire, moves up to
   `limit` slots into the caller's vector, and stores `released_`
   (`bucket_ring.cpp:38-54`). The vector is a local of `pump`
   (`market_feed.cpp:195`), so it is rebuilt every frame that drains. The
   strings are freed on the frame thread, which allocated nothing: a cross
   thread free per bucket.
6. For each bucket the frame thread calls
   `market_.append_point(instance_id, account_id, bucket.series_id, level,
   point, now)` (`market_feed.cpp:243-246`). That hashes three strings, finds
   the snapshot with `slot_in`, pushes onto a deque, pops the front when full
   and stamps `++version_` (`market_table.cpp:122-135,156-175`). It stamps the
   version even when the point was dropped as late (`append_level` returns
   early at `:125-126`, the stamp at `:174` is unconditional), so a dropped
   late point still makes every chart of that series rebuild.

What changes, exactly. The crossing stays what it is, a single producer ring with
a release store and an acquire load, because it is already correct and
principle 1 says the table is frame thread only. The slot type changes from a
string and a fat point to a handle and a flat point. The delivery thread never
reads the table. It holds the handle as an opaque 8 bytes that the frame thread
gave it at `watch`. The frame thread checks the handle against the table's
generation when it applies the bucket, which is what lets a bucket for a series
released a moment ago be dropped and not written into the next occupant of its
slot. `bucket_ring.hpp:34-41` says the series is named by key and not by index
for exactly that reason, so the generation tag is what removes the stated reason
for the string.

### Every writer

All writers run on the frame thread. There are three paths into
`market_table`.

| Writer | Event or caller | Calls | Cite |
|---|---|---|---|
| `read_service::apply` | `series_catalogue` event from the market worker | `set_catalogue`, `fail_catalogue` | `read_service.cpp:468-477` |
| `read_service::start_market_read` | a catalogue read starting | `begin_catalogue` | `:238` |
| `read_service::apply` | `series_points` event, a backfill or a manual refresh | `set_points`, `fail_points` | `:535-544` |
| `market_feed::pump` | the drained ring, every frame | `append_point` | `market_feed.cpp:243-246` |
| `read_service::clear` | `stop()` | `clear()` | `:553-554` |

`term_structure_table` has its own writers, all in `read_service`:
`begin` at `:387` and `:409`, `set` and `fail` at `:497-506`.

There is no removal. `market_table` has `clear()` and nothing narrower
(`market_table.cpp:192-195`), so a series opened once stays for the life of the
process, at up to 760 KiB. This is MS-5 and it is still true. Data of a signed
out session also stays addressable by anyone who knows the three strings.

### Every reader, and what a stamp would save

"Per frame" is the steady state with nothing new. The costs are read from the
code. None was timed.

| Reader | What it reads | Per frame today | What a version stamp saves |
|---|---|---|---|
| Chart window, `chart_window.cpp:193-217` | `points(i, a, s)`, one level, `newest_at_ns` | one three-string hash and probe, no allocation (`lookup_cost_tests.cpp:53`), a key compare in `update_chart_plot` (`chart_plot.cpp:97`), health assessment from the wall clock | The rebuild is already skipped. On a stamp change it copies `points` into `xs` and `ys` through a deque and rebuilds candles and EMA, O(points in view) (`chart_plot.cpp:104-130`). A contiguous ring removes the copy. Health cannot be cached on the stamp because it depends on the clock |
| Chart draw, `chart_window.cpp:400-402,423-431` | `xs`, `ys` | `fill_gradient_under_curve` calls `PlotToPixels` twice per point per frame (`plot_fill.hpp`), O(points in view) every frame, 7,200 at the widest span | Nothing. The stamp does not help a draw that depends on the axis. See "Risks" |
| Term structure window, `term_structure_window.cpp:107-293` | `term_structures.get(i, a, curve_key)` | copies the primary curve's nodes and sorts them with `tenor_rank` (`:180-184`), builds a `std::map` (`:187-193`), one `ys` vector per overlay (`:236`) and a `std::format` per overlay (`:277`) | All of it. The curve changes on a read, every 30 s. Today the table has no stamp, so the window cannot know |
| Series browser, `series_browser.cpp:273-309,454-470,556-585` | `catalogue(i, a)` | copies two ids and the party (`:273-274,300-301`), parses the party list (`:309`), filters the catalogue with four case-insensitive substring scans per row and stable sorts with `tenor_rank` parsing (`:556-585`) | All of it. The catalogue changes on a 60 s read. It has no generation today |
| CRM window double click, `crm_window.cpp:290-293` | `catalogue(i, a)`, a scan | one scan on a click, not per frame | Nothing needed |
| `market_feed::pump`, `market_feed.cpp:132-255` | catalogue, windows | a vector of live windows (`support.hpp:76`), a `running_key` string per chart (73 characters, heap, `support.hpp:64`), a `find_if` over the whole catalogue per chart (`market_feed.cpp:176-179`), a `wanted` map and a `desired` map of strings per session (`:140,211`) | All of it. The set changes when a window opens or closes or a catalogue lands. This is SR-11 and AR-3 |

For scale, with 200 charts and a 5,000 row catalogue (AR-3's figure) the `find_if`
alone is up to 1,000,000 string compares per frame. The review's own text says up
to 500 rows in SR-11. The catalogue size of a real deployment is not known to me.

## The target

### One table, two kinds of row

`series_table` replaces `market_table` outright. It holds two things.

- A **catalogue** per session scope `(instance_id, account_id)`: the rows the
  deployment lists, a `generation` that changes whenever the catalogue is
  replaced, and an index from series id to row number built once when it lands.
  The series browser and the CRM read this.
- A **series row** per series a window or a read holds. It is addressed by a
  `series_handle` and owns the rings. A series row is created by `intern`, not
  by the catalogue arriving, so a restored chart has its handle before its
  session has signed in.

A catalogue arriving does not create series rows. It refreshes the cold metadata
of the rows that exist (`uri`, `party_id`, `tenor`, `curve_key`) by id through the
index, in O(held rows), and marks a held row whose id is absent `unlisted`. The
rings and their stamps do not change. A catalogue of 5,000 rows is not
duplicated into 5,000 rings.

### The handle

```cpp
struct series_tag {};
using series_handle = handle<series_tag>;   // handle.hpp, beside window_handle
```

It is the existing `handle<tag>`, index plus generation, issued from the same
`slab`, so `owns(layout, h)` and the "stale handle compares unequal" tests apply
unchanged (`handle.hpp:430-495`, `handle.cpp:24-47`). Releasing a slot bumps the
generation. The key a person or a window names it by does not change:

```
series_key = (instance_id, account_id, series_id)     // series_id is the catalogue's UUID
```

Looked up through `composite_map<3, series_handle>`, which is already
allocation free (`composite_key.hpp`). A handle is never persisted. Windows
persist the string and resolve it with `intern` at open and restore, a cold path.

**Why the server's series id and not the URI.** A tick names its series by
canonical URI. The URI is not a safe key. A series is party owned and the catalogue
row has a `party_id` (`market.hpp:54-55`). A chart may view another party's series
through the picker, and it then has the same URI as the session party's series
with a different id. Keyed by URI the foreign chart would be handed the session
party's live buckets, which the code today avoids because the watch is by id and
the ring is per id (`market_feed.cpp:183-189`). The URI is a cold column of the
row, used only to route ticks.

### What identifies a series, and the ORE codec

Hedgr does not parse the grammar. For routing, the row stores the canonical
series URI that `canonical_series_uri` already produces from ORE's
`oresmd_uri_codec::read`, `datum::series_of` and `oresmd_uri_codec::write`
(`tick_stream.cpp:47-58`; headers
`ores.marketdata.core/datum/oresmd_uri_codec.hpp` and
`ores.marketdata.api/datum/market_datum.hpp` under
`/home/marco/Development/OreStudio/ores_dev_brave_hopper`). The table calls it
once, at `intern` and at catalogue refresh for a held row, never for the whole
catalogue (5,000 parses would be a frame stall). It stores the result.

The hand-rolled grammar that remains is `series_label.cpp`
(`series_label`, `series_asset_class`, `series_term`, `series_curve_key`,
`series_curve_label`), which fills `asset_class`, `tenor` and `curve_key` of a
catalogue row. This plan leaves it alone and does not add a second parser.
Part D step 7 replaces it with datum fields. The table's cold columns take
whatever the catalogue row says, so that step needs no change here.

### Composite series, and why the key reserves a part

ORE's schema marks each field of a type as identity or coordinate
(`ores.marketdata.api/datum/schema.hpp:121,296-304`). For `ir_swap` the identity
is `ccy, index_name, fwd_start, tenor` and `term` is a coordinate
(`schema.hpp:319-320`); `zero`, `discount` and `mm` have `term` as their only
coordinate (`:309-312`). `series_of` drops the coordinates
(`market_datum.hpp:132-137`). So one series of a curve family holds many terms.

Today the live path discards the coordinate. `decode_tick` writes
`series_of(datum)` into `tick::series_uri` (`tick_stream.cpp:52-54,78`) and
`tick` has no coordinate field (`tick_stream.hpp:49-58`). The sink keys one
aggregator by that URI (`tick_sink.cpp:62`). Inferred: for a series with a
coordinate, the ticks of every term share one aggregator and each tick replaces
the bucket value, so its ring interleaves terms. Whether the catalogue lists such
series as chartable rows, and what a chart of one shows, I could not tell from the
code. This needs a live check (see "What cannot be verified").

This matters for exactly one consumer, the term structure. The browser opens a
term structure window with one member id per catalogue row and empty tenors
(`series_browser.cpp:236-239`), and the worker takes the tenor from each
observation's `term` (`term_structure_worker.cpp:73`). If the members are series
whose term is a coordinate, a live curve needs one ring per `(series, coordinate)`.

The decision, which is open (D4): the key is `(instance, account, series_id)` in
steps 1 to 6, as above, and a fourth part `point` (the canonical quote URI of the
datum, empty for the series itself, written by ORE's `write`) is added by the
term structure step of the actor plan, when composite curves are made live.
Adding a part to a `composite_map` is a mechanical change (`composite_map<4>`).
Handles are opaque, so no caller changes. Reserving the part now would add a
field no code reads.

### Ring layout, and what ImPlot's offset can and cannot do

Per series row, per level, six columns, struct of arrays, no `std::string`:

| Column | Type | Used by |
|---|---|---|
| `at_s` | `double` | ImPlot x, the time axis |
| `value` | `double` | ImPlot y, the close |
| `at_ns` | `int64` | merge, bucket id, as-of, health |
| `open`, `high`, `low` | `double` | candles |

That is 48 bytes per point against 72. The 16 bytes ImPlot reads are two
contiguous columns of 8 byte stride, the cold 32 bytes are touched on append,
merge and a candle rebuild. `term` is gone. `at_ns` stays the source of truth.
The derived `at_s` is a second column and not a conversion at plot time, because
converting is the copy this plan removes, and a seconds double cannot hold a
nanosecond bucket boundary exactly at epoch magnitude (the spacing of doubles
near 1.7e9 is about 240 ns).

**ImPlot's `Offset` does not do what the review assumed.** AR-1 says "ImPlot's
`offset` parameter plots it with zero copy". The indexer is
`data[(offset + idx) % count]` where `count` is the number of points passed
(`IndexData`, `IndexerIdx`, `implot_items.cpp:500-519` in the vcpkg ImPlot 1.0
the build uses; `ImPlotSpec::Offset` and `Stride` at `implot.h:530-531`). So an
offset plots a full wrapped ring of exactly `count` elements. It cannot plot the
newest `m < count` points of a wrapped ring, because the wrap happens at `m`.
Every chart plots a span: `live_window_start` picks the first point in the span
(`chart_plot.cpp:31-43`) and most spans are shorter than the ring. A pure ring
plus `Offset` therefore works for the span "All" on a full ring and for nothing
else.

The custom drawing has the same shape. `fill_gradient_under_curve(const double*
xs, const double* ys, int count, ...)`, the candle loop and `plot_dashed` take a
flat pointer and a count (`plot_fill.hpp`, `chart_window.cpp:57,362,428`). They
would need wrap-aware indexing too.

So the layout is a **sliding window over a contiguous buffer**, not a wrapped
ring. Each column is a buffer of `capacity + slack` elements. An append writes at
`end`. When `end` reaches the buffer's size the newest `capacity` elements are
moved to the front in one `memmove` per column and `end` is reset. Any span, the
newest `m <= capacity` points, is then one contiguous run, so ImPlot gets
`xs + lo, ys + lo, m` with `Offset = 0`, and the fill, candle and EMA code take
the same pointers they take now. Each element has an absolute sequence number, and
the ring reports `first_seq` (the sequence of element 0) so a cache that stored
"the span starts at sequence S" converts to an index after a compaction.

Cost of this choice. With `slack = capacity / 4` the buffers hold 1.25 times the
capacity: 10,800 points x 1.25 x 48 = 648,000 bytes, 633 KiB per full series
against about 760 KiB today. That is a 17 percent reduction, not the 80 percent
AR-1's "16 bytes per point" implied, because candles need open, high and low at
every level. The memmove runs once per `slack` appends: for the fine level every
30 minutes at one bucket a second, moving 7,200 x 48 = 345,600 bytes. The
alternatives are in "Rejected alternatives".

The bucket in the ring becomes a flat value:

```cpp
struct series_bucket {            // 56 bytes, trivially copyable
    series_handle series;         // 8
    std::int64_t at_ns;           // 8
    double value, open, high, low;// 32
    domain::market_level level;   // 1, then 7 of padding
};
```

4,096 slots are 229,376 bytes, half today's inline size, with no heap behind it.

### Version stamps

Keep the shape `market_table` has: **one table-wide counter, stamped onto the
row**. A stamp is never reused by another series and never repeats after a clear
(`market_table.hpp:119-120`, `state_tables_tests.cpp:906-945`). That property is
why a window's cache key needs no handle. It is also what makes a derived value
over N rings cheap: the key is the maximum of the members' stamps, because every
change gets a stamp larger than every earlier one, so the maximum changes exactly
when some member changes.

What bumps the stamp of a series row: an applied append (a new tail point or a
replaced tail), a seed (backfill merge) that changed anything, a failure recorded
or cleared. What does not: a bucket dropped as late, a bucket dropped as stale, a
catalogue refresh that changes no field. Today a dropped late point bumps
(`market_table.cpp:174`). That is a deliberate change with a test.

Catalogue stamps are separate: `catalogue.generation` from the same counter,
bumped by `set`, `fail` and `begin`.

Who reads what. The chart keys its plot on the row stamp and the span, as now.
The term structure window keys its ladder on the stamp of its source (today the
term structure row, later the maximum of its members). The series browser keys its
filtered view on `(catalogue.generation, filter, class, party, synthetic)`. The
feed does not read stamps. It reads a change list (step 3).

One cost the single stamp accepts: a medium or coarse append bumps the stamp of a
chart drawing the fine level. That is at most once a minute per series for medium
and once an hour for coarse, so a per-level stamp would save almost nothing.

### How a term structure's member series are referenced

A term structure window holds a `std::vector<series_handle>`, one per member, in
tenor order. It is the same type a chart holds, one handle. Members are ordinary
series rows. A member ring is watched like any chart's, so a curve window and the
charts of its members share one subscription and one ring, which is AR-6's second
half. The window never owns data.

Until the actor plan's step 8 the window still reads `term_structure_table`, the
result of a 30 s poll, and this plan only gives that table a stamp (step 5). After
step 8 the same window derives its curves from `series.columns(member, fine)`: the
newest value of each ring is a node, and the history is the carry-forward of
`term_structure_worker.cpp:79-97` over the rings' time axis, as a pure model
function tested against the worker's output on the same points. That function is
not in this plan. Two differences make it approximate, not identical: rings hold
one value per second (the last tick) and the worker reads raw observations, and a
member with no tick yet has no node until its backfill lands.

### Catalogue and rings, eviction, bounded memory

Three bounds, none of which exists today.

1. **Per ring.** The existing capacities, `market_fine_capacity` and the other two
   (`market.hpp:105-111`). Unchanged. A ring's buffers are allocated on its first
   point and grow geometrically to the cap, so a series that never ticks costs
   nothing.
2. **Resident budget.** `series_table` counts the bytes its rings hold. A series
   row carries a `watchers` count (step 3). When a release takes a row to zero
   watchers and the total is over the budget, the table evicts zero-watcher rows
   least recently released first, until it is under. Evicting releases the slot,
   so the generation bumps and any stale handle is rejected. A row with watchers is
   never evicted: memory then scales with open windows, which is the operator's
   choice, and it is stated in the status the frame loop already has for the feed.
   A reopened chart after eviction costs one backfill of 2,000 points, which the
   feed already does on every watch (`market_feed.cpp:222`).
3. **Scope removal.** `forget_scope(instance, account)` releases every row and
   the catalogue of a scope. The runtime calls it where it removes an account or
   an instance (`catalogue_service` effects), which closes MS-5's "never
   released".

The budget's value is open (D2). For scale, a full series is 633 KiB, so 200 live
charts hold at most 124 MiB, and 512 rows hold 316 MiB.

### Thread ownership

The series table is frame thread only, with the rest of the tables (ADR 2,
principle 1). No lock, no atomic.

| Thread | Owns | Crosses by |
|---|---|---|
| cnats delivery thread | decode, canonicalise, aggregators (under `tick_sink::mu_`), the producer end of the ring | `series_bucket` through the existing SPSC ring |
| stream thread | connect, `watch` and `unwatch` commands into the sink | the command vector and `mu_`, unchanged. The command now carries `(series_handle, canonical_uri)` |
| frame thread | every table, `intern`, `acquire`, `release`, the consumer end of the ring | none |

`tick_sink` stops keying by series id strings. It keeps
`unordered_map<std::string, aggregator>` by canonical URI for the lookup the tick
needs (the tick has only the URI) and stores the handle in the aggregator in
place of `series_id_`. The two-map arrangement of `uri_of_` and `series_`
(`tick_sink.hpp:94-97`) collapses to one map and a small vector of handles for
`unwatch`. The `mu_` stays. A handle-indexed array would remove the string hash on
the delivery thread, but the tick arrives with a URI, so the string work happens
anyway in `canonical_series_uri`. Removing that per-tick parse is a separate
change and is not planned here.

A watch for a series that is released while a bucket is in flight is handled by
the generation. Sequence: `watch(h)` sent, series released (generation g to g+1,
slot reused by another series), `unwatch(h)` not yet taken by the stream thread
(`market_stream.cpp:130-139`), the aggregator publishes a bucket tagged g, the
frame thread's `owns(layout, h)` fails, the bucket is dropped and counted in
`stale_buckets`. The new occupant's own watch carries generation g+1.

## Interaction with the other plans and the store

### The session actor

Steps 1 to 7 of `session-actor.md` need none of this and keep the string
interface. This plan's steps 1 to 3 change that interface in the one mechanical
way the actor plan predicted.

| Today | After step 2 |
|---|---|
| `tick_feed::watch(std::string series_id, std::string uri)` (`tick_feed.hpp:57`) | `watch(series_handle, std::string canonical_uri)` |
| `tick_feed::unwatch(std::string_view series_id)` (`:60`) | `unwatch(series_handle)` |
| `tick_feed::drain(std::vector<market_bucket>&)` (`:63`) | `drain(std::vector<series_bucket>&)` |
| actor `set_watch`: a set of `(series id, uri)` | a set of `(series_handle, canonical_uri)` |
| read answers carry `series_id` strings (`event.hpp`) | unchanged. The frame thread resolves `find(scope, id)` once per answer, a cold path |

If the actor lands first, its `set_watch` carries strings and this plan converts
it, which is a signature change in the actor, the fake link and `market_feed`. If
this plan lands first, the actor is written with handles. The second order is
cheaper, and `session-actor.md` already lists the table as a prerequisite of step 8
only.

The three points of Prerequisite E, mapped:

1. A stable key: `series_handle`, step 2.
2. A place for member rings: series rows, with `term_structure_state` holding
   handles (step 3), and the coordinate part of the key reserved (D4).
3. A stamp per ring: step 1, read by the term structure cache in step 5.

### Saved window specs and the store schema

Persisted windows name series by id string. `chart_state::series_id` and
`term_structure_state::member_ids` are in `state_json`
(`window_json_io.cpp:57-58,72-73`). That does not change. Handles are memory
only. The store schema does not change and `window_spec_entity`
(`window_repository.cpp:66-79`) is untouched. A restored window interns its series
at restore, from the `session_instance` and `session_account` columns the window
table keeps while unbound (`tables.hpp`, `window_columns`), so a chart has a
handle before its session signs in. `chart_state::max_points` stays in the JSON
and stays unread. Removing it is a store migration for no gain and is not part of
this plan.

### market_feed's watch set

Today `market_feed::pump` derives the wanted set from every window every frame
(SR-11). After step 3 the feed owns no per-frame derivation. A window holds
handles and the table counts holders. The feed receives a change list, the
handles whose "wanted" state flipped since the last frame, and applies watch and
unwatch for those. A full pass runs only on three events: a stream opens, a
catalogue is applied, a session leaves authenticated. A series is wanted when its
`watchers > 0`, it is `listed`, it has a canonical URI, and its `party_id` equals
the session's (`market_feed.cpp:188`). The backfill on first watch and on
`take_reconnected` stay as they are until the actor plan replaces the poll.

## Steps

Each step is one pull request that leaves the application working and the suite
green. Steps 1, 2 and 4 are the three pull requests Part D step 6 names. Steps 3,
5 and 6 are the reader migrations. One departure from Part D's wording: its second
pull request, "switch `append_point` and the bucket to the handle", cannot leave
the old table in place, because the chart reads the table the feed writes. Step 2
therefore moves every reader to the new accessors at the type level, and the copy
into `xs` and `ys` stays until step 4. That is the only way to delete
`market_table` without a second store.

### 1. The model: `series_table`, `series_ring`, tests

Add `series_tag` and `series_handle` to `handle.hpp`, `series_ring` (the sliding
columns), `series_table` (rows, catalogue, stamps, eviction, `forget_scope`) and
`series_bucket`. Nothing uses them yet.

Tests first, in `hedgr.model/tests/series_table_tests.cpp`, through the public
interface only:

- **Ring wrap.** Append `capacity + slack + k` points across the compaction
  boundary. The six columns read back as the newest `capacity` points, in order,
  contiguous, with `first_seq` advanced by the number dropped. Mutations: move
  one column fewer in the memmove, move the oldest and not the newest, compare
  `end` against `capacity` and not the buffer size. The first and third survive a
  test that reads only `value`, so the test compares all six columns.
- **Tail rules.** A point older than the tail is dropped, an equal timestamp
  replaces the tail, a newer one appends. Parity with `append_level`
  (`market_table.cpp:122-135`). Mutation: `<` for `<=`.
- **Stamps.** Every applied change stamps a value no other series held. A dropped
  late point and a refresh that changes nothing do not stamp. A stamp is larger
  after `clear()`. Parity with `state_tables_tests.cpp:906-945`, plus the one
  deliberate change. Mutation: stamp before the late check.
- **Staleness.** After `release`, the old handle is not `contains`, `append`
  through it returns `stale` and leaves the new occupant of the slot untouched,
  and `intern` of another key reuses the index with a new generation. Mutation:
  drop the generation compare in `owns`.
- **Seed.** A backfill merges into live points by bucket id and the live point
  wins. This fixes MS-7: today `merge_level` keys by exact `at_ns`
  (`market_table.cpp:103-120`), so a backfilled and a live bucket of one second
  survive together. Mutation: seeded overwrites live, key by `at_ns`.
- **Catalogue.** A refresh keeps the ring and its stamp, updates `uri` and
  `party_id` of a held row, marks an absent id `unlisted`, and bumps
  `generation` once.
- **Eviction.** Over budget, only zero-watcher rows go, least recently released
  first. A watched row is never evicted. Mutation: evict by oldest data, ignore
  watchers.
- **Scope.** `forget_scope` releases its rows and handles go stale, other scopes
  untouched.
- **Cost.** After warm up, `find`, `columns`, `append` and `version` allocate
  nothing (the `allocation_counter` of `lookup_cost_tests.cpp:59-65`), and a run
  of appends allocates only when a buffer first grows.
- **Differential.** A property test applies the same random sequence of seeds,
  appends and failures to `market_table` and to `series_table` and compares the
  columns and the failure text. It passes except for the two deliberate changes
  (late stamp, MS-7), which it excludes by construction. It is deleted in step 2
  with `market_table`. This is the lever that makes step 2 a move.

Outcome. None in the application. The step shows `append` at zero allocations in
steady state in a model test. Today `append_level` pushes onto a deque of 72 byte
elements, which libstdc++ allocates in 512 byte blocks (inferred), about one
allocation per 7 appends and one free per 7 front pops.

Risk is low, an addition. Deleted in this step: nothing. This is the only step in
which two stores coexist, and step 2 must follow it.

**Status: built.** `series_ring`, `series_table`, `series_bucket` and `series_handle`
are in `hedgr.model`, tested by `series_table_tests.cpp`, used by nothing, with
`market_table` untouched. The open decisions took the recommended values: D1 a
sliding window with 25 percent slack, D2 a 256 MiB byte budget with watched rows
never evicted, D5 merge by bucket id. D3 and D4 do not arise until step 2.

Deviations from the text above, each the smallest choice that left no ambiguity.

- **`acquire`, `release`, `erase`.** The staleness bullet says `release` frees the
  slot and the eviction text says `release` drops a watcher. `acquire` and
  `release` count watchers, and `release` evicts only when the count reaches zero
  and the rings are over budget. `erase(handle)` frees the slot at once, which is
  what eviction and `forget_scope` call. `set_budget` is added so a test can
  set the pressure after the rows exist.
- **A late bucket.** It stamps nothing, as planned. It still records
  `observed_at_ns` and clears a failure, as `market_table` does, so the
  differential compares both. Clearing a failure changes what the row shows, so
  that one case stamps.
- **A catalogue refresh never stamps a row.** The stamp says what the plot data
  is. Changed `uri` or `party_id` reach a reader through `catalogue.generation`
  and `info()`, not through the ring's stamp. The Stamps bullet's "a refresh that
  changes no field" therefore reads "a refresh".
- **Canonical URI.** `series_info::uri` is the catalogue row's `uri` as given.
  `hedgr.model` links no ORE codec, so the canonicalisation named under "What
  identifies a series" is the runtime's job and lands with step 2's `intern`
  call sites.
- **`catalogue_state`** is still the enum in `market_table.hpp`. It moves with
  step 2, which deletes that header.
- **Merge renumbers.** A seed that changes a ring moves `first_seq` past every
  number issued before it, because points are inserted in the middle and no old
  sequence number would name the same point. A cache holding one rebuilds, which
  it does anyway because the stamp changed.
- **The differential test** compares the six columns, the failure text,
  `observed_at_ns` and `newest_at_ns`, not the stamps. The MS-7 difference is kept
  out by whole-hour timestamps (bucket equality is time equality) in one run, and
  in the other by seeding a row only before it holds a point.

Mutations run against the finished code, 47 of them, all killed. That covers the
ones this step names (one column fewer in the move, the oldest moved, `end`
against capacity, `<` for `<=`, stamp before the late check, the generation
compare in `owns`, seeded over live, key by `at_ns`, evict by data age, ignore
watchers). Two survived the first draft of the tests and are now pinned: the
release order of eviction and a release that leaves a watcher behind. A linear
growth mutant changes no allocation count, because `std::vector::resize` grows
geometrically by itself, and is killed only through the byte accounting.

### 2. The bucket path and the writers

Replace the string with the handle from the delivery thread to the table, and
delete `market_table`.

- `bucket_aggregator` takes a `series_handle` and publishes `series_bucket`.
  `bucket_ring` holds `series_bucket`. `tick_sink::watch` takes
  `(series_handle, canonical_uri)` and collapses its two maps. `tick_feed` and
  `market_stream` change as in the table above.
- `market_feed` interns a series when it first decides to watch it, passes the
  handle to the sink, and applies drained buckets with
  `series.append(handle, ...)`, counting `stale` and `late`.
- `read_service` writes `series.apply_catalogue`, `seed` and `fail`. A backfill
  answer for an id that is not interned is dropped and counted, not stored (D3).
  `start_market_points_read` interns, so a manual refresh of a chart that is not
  live still has a row.
- Readers move to the new accessors with the copy kept: `chart_window.cpp`,
  `series_browser.cpp`, `crm_window.cpp`, `renderer.hpp:420`, `headless_desk.hpp`
  and the call in `instance_manager.hpp:184`.
- Deleted in this PR: `market_table` and its tests, `market_series_snapshot`,
  `market_point::term`, `market_bucket`, `composite_map<3, snapshot>`, the
  differential test.

Tests first. `market_stream_tests.cpp` already pins the sink keeping watched
ticks and dropping the rest (`:102`), a non-series URI (`:143`) and interleaved
watch and tick (`:189`); they move to handles. New, at the sink: a bucket of an
unwatched handle is not published, a re-watch of one series under a new URI
replaces its aggregator (`tick_sink.cpp:42-45`), and `on_tick` allocates nothing
on a closing bucket (the allocation counter around the call, after warm up).
New, at the feed: a bucket whose handle was released before the drain is dropped
and counted and does not write the next occupant, and a drained bucket lands in
the series of its own session (`market_feed_tests.cpp:455`). Mutations: skip the
generation check, publish before storing the handle, key `unwatch` by the old
string.

Outcome, measurable in a unit test. Allocations per closed bucket on the delivery
thread go from 1 (the UUID copy, `bucket_aggregator.cpp:53-57`) to 0. Slot size
112 to 56 bytes. Per drained bucket the frame thread's three string hashes and a
deque push become one index and six stores. This is small in time. The step is
what the actor needs.

Risk is medium. It touches the one thread crossing, a seam in three test files,
and every reader of the table at once. The differential test from step 1 is run
once more against the final code before it is deleted. Not verifiable without a
live feed: that the real cnats delivery thread publishes as the unit test does.
`market-eventing.md` verified the live path once, on 17 October 2026 against
brave_hopper. The same check is repeated before this lands.

**Status: built.** `market_table` is gone and `series_table` is the one store.
Three commits in one pull request, because no seam leaves a build with one
store: the bucket path, writers and readers together (the ring's slot type
breaks the producer and the consumer at once), then the deletion. The
differential test of step 1 was run against the final `series_table` code
(2 cases, 4,578 assertions, passed) and then deleted with `market_table`.

Deviations from the text above, each the smallest choice that left no ambiguity.

- **Canonicalisation lives in `market_feed`.** The feed calls
  `session::market::canonical_series_uri` (the existing ORE `oresmd_uri_codec`,
  `series_of`, `write` path) once, when it first watches a series, and passes the
  result to `tick_feed::watch`. A URI the codec does not read is not watched, is
  logged once and is still interned and backfilled, as before. `series_info::uri`
  stays the catalogue row's URI as given, so the model needs no codec. The sink's
  `watch` takes the canonical string and returns nothing.
- **The feed holds what it watches.** `market_feed` calls `acquire` when it
  watches a series and `release` when it unwatches, retires a stream or closes
  one. Without it a watched row has zero watchers and the budget could evict it
  under a live chart. Step 3 moves the holder from the feed to the windows.
- **Stale and late are counted by the feed** (`stale_buckets()`,
  `late_buckets()`) and the unheld backfill answers by `read_service`
  (`unheld_points()`). All three are readable through `instance_manager`.
- **`tick_sink` keeps one map and no vector.** Each aggregator carries its handle,
  and `unwatch` and a re-watch find it by a scan of the map under `mu_`. A watch
  set is hundreds of entries and changes on a window open or close, so the scan
  costs less than a second structure kept in step. A second watch of one URI by a
  different handle keeps the first aggregator, as the old code did.
- **Accessor names are kept.** `instance_manager::market()`, `render_input::market`
  and `desk.market` return a `series_table` and keep their names, so the headless
  frame tests change by type and by the calls the table renamed.
- **The "stale" bucket test uses a crafted handle.** The runtime has no path that
  releases a series row yet (that is step 3), so the feed test hands the drain the
  handle a released series would have held, its slot and the generation before the
  occupant's. The release and reuse itself is pinned in `series_table_tests.cpp`.
- **`market_point::term` was dead in the model only.** The session transport's
  own `market_point` keeps its `term`, which the term structure worker reads.
- **`market_feed_tests.cpp` rows carry real series URIs.** The feed canonicalises
  before it watches, so the fixture's `oresmd://test/<id>` could not be watched.
  The non-series case moved from the sink to the feed.
- **The chart still copies.** `update_chart_plot` takes `series_columns` and fills
  `xs` and `ys` from the `at_s` and `value` columns, as planned for this step.

Mutations run against the finished code, all killed, listed in the pull request.


### 3. Holders and the watch set

A window holds handles, and the feed reacts to a change list. Closes SR-11, AR-3
and MS-5.

- `window_table` gets `std::vector<std::vector<series_handle>> series`, one list
  per window, empty for kinds with no series, written through
  `set_series(window, handles)` only. `window_service` calls `intern` and
  `acquire` on open and restore, `release` on close and on account or instance
  removal. `series_table::acquire`/`release` change a `watchers` count and record
  a change when it crosses zero.
- `market_feed::pump` drops `live_windows`, `wanted`, `desired`, `entry.watched`'s
  string map and the per-chart `find_if`. It drains the ring, takes the change
  list and does the three full passes of "market_feed's watch set".
- A catalogue apply refreshes the held rows of its scope and records the ones
  whose watch state changed.

Tests first, at the existing seam: `market_feed_tests.cpp:362-433` (the watch set
follows open and closed charts, a chart with no catalogue row is not watched, a
chart opened before the catalogue is watched later, a backfill on watch, a
reconnect reseeds) must pass unchanged. New: a pass over an idle desk makes no
`watch` or `unwatch` call and allocates nothing; opening two charts of one series
watches once and closing one keeps the watch; closing the last unwatches; a
catalogue that drops a held id unwatches it; and a **holder audit**, a test
helper that asserts the sum of `watchers` equals the number of valid handles the
window table holds, called at the end of every lifecycle test (open, close,
restore, disconnect, remove account, remove instance). Refcounts leak the way the
in-flight sets leaked before #23, so the audit is the structure that makes a leak
fail a test. Mutations: skip `release` on close, skip `acquire` on restore, evict
a watched row.

Outcome. Per-frame work in the feed on an idle desk goes from
O(windows x catalogue rows) to O(1) plus the drain, with zero allocations. The
benchmark is the one in "How a number is taken" with the feed's `pump` in the
loop.

Risk is medium. Reference counts across window paths are where a leak or a
double release hides. Deleted in this PR: the per-frame derivation and its maps in
`market_feed.cpp:135-232`, `live_windows` if nothing else uses it,
`running_key`'s use in the feed.

**Status: built.** A chart window holds its series, the table counts the holders,
and `market_feed::pump` works from the table's change list. The open decisions
took the recommended values and D4 stays out of scope.

Deviations from the text above, each the smallest choice that left no ambiguity.

- **Only charts hold a series.** A term structure window holds none. Nothing
  reads a member ring until the actor plan's step 8, and acquiring the members
  would open a live watch and a backfill for each with no consumer, which a user
  would see as new traffic. `window_columns::series` is empty for every other
  kind, and the term structure takes its handles with step 8.
- **The series table moved to `instance_manager`.** The window service needs it
  before the read service exists, so the manager owns it and hands a reference to
  the window service, the read service and the feed. `read_service` no longer
  owns a table.
- **The feed has no window input.** `market_feed` lost its `window_table`. A
  restored chart's catalogue read is asked for by session, through
  `read_service::start_market_catalogue_read(instance, account)`, from the first
  pass that finds a held row with no catalogue.
- **What the change list records.** A row's first watcher, its last, an erase of a
  watched row, and a catalogue apply that moved the `listed`, `uri` or `party_id`
  of a watched row. A catalogue that changes nothing records nothing, so the 60 s
  read costs the feed nothing. A full pass runs only when a stream opens, from
  `series_table::held`.
- **A failed catalogue read no longer drops watches.** The old derivation read the
  catalogue state every frame and unwatched everything while the last read had
  failed. The plan's rule (`watchers > 0`, listed, the session's party) does not
  look at it, so a watched series stays watched through a transient failure and is
  not backfilled again after it.
- **The feed holds a session handle.** `session_table::find` builds a joined key
  per call, which is one allocation per stream per frame. The stream entry keeps
  the handle it was opened for and the sweep checks `contains` and `status`.
- **`window_service::release_scope` and `series_table::forget_instance`.** Removing
  an account or an instance makes its windows give back their holds, then forgets
  the rows and the catalogue (MS-5). `rebind` and `open` of an existing window
  re-hold, so an account that returns brings its charts back live.
- **The holder audit** is `hedgr.runtime/tests/holder_audit.hpp`. It checks every
  row's watchers against the valid handles the window table holds, the total
  against `series_table::watcher_total`, and that no window holds a handle whose
  row is gone.

Mutations run against the finished code, all killed except one equivalent: skip
`release` on close, skip `acquire` on restore, evict a watched row, no change on
the first or last watcher, no change from a catalogue apply or an erase, skip the
re-hold on rebind, a doubled acquire, no `release_scope`, no forget on removal,
the party check dropped, no pass on stream open, no unwatch when a want is lost.
Releasing before acquiring in `hold_series` changes nothing, because a window
that keeps its series returns before either runs.


### 4. The chart plots from the ring

The remaining half of AR-1. Closes the copy.

- `chart_plot` stops owning `xs` and `ys`. `update_chart_plot` takes the
  `series_columns` of the level (pointers, `size`, `first_seq`) and keeps
  `start_seq`, the candles, the EMA and the key. The frame passes
  `columns.at_s + start`, `columns.value + start`, `size - start` to ImPlot and
  to `fill_gradient_under_curve` and `plot_dashed`, with `Offset = 0`.
- The cache holds no pointer across frames. Pointers into a column are valid for
  the frame because compaction only happens in `pump`, before the render, and the
  chart converts `start_seq` to an index each frame.
- The chart reads its handle from `windows.series(window)` and the columns from
  `series.columns(handle, level)`. No string lookup is left on the frame path.
- Candle and EMA caches stay on the stamp. If a measurement shows the O(points)
  rebuild once a second per chart matters, make them incremental then (D1, D7). It
  is not planned.

Tests first. `chart_plot_tests.cpp` moves from deque input to columns and keeps
its assertions (span selection `:46-58`, sparse series kept whole `:60-69`, candle
width over All `:71-88`, rebuild only on a key change with zero allocation
`:90-117`). New: a plot over a ring that compacted between two frames still
starts at the right point (the `first_seq` conversion), the pointers handed to the
draw calls equal the table's column pointers (no copy: assert address equality
through a seam, or assert the plot's own vectors are empty), and a stale handle
renders the reading state and not stale data. Mutations: convert `start_seq`
without `first_seq`, copy into `xs` anyway, key the cache without the stamp.

Outcome. Per stamp change per chart: the O(points in view) copy of two vectors
(115,200 bytes at 7,200 points) goes to none, and the 23 MB a second figure for
200 charts at one stamp a second (200 x 7,200 x 16 bytes, computed) goes with it.
Per frame: unchanged, and stated so. See the benchmark.

Risk is medium. The custom draw code takes raw pointers and a count and must never
be handed a span across a compaction. Deleted in this PR: `chart_plot::xs` and
`ys`, `live_window_start` over a deque, `std::deque` in `chart_plot.hpp`.

**Status: built.** The chart plots from the table's columns and keeps no copy of
them. The open decisions took the recommended values (D1, D7).

Deviations from the text above, each the smallest choice that left no ambiguity.

- **`chart_plot_start`.** `chart_plot` keeps `start_seq`, and a free function turns
  it into an index of the frame's columns, clamped to the ring. The EMA takes a
  `std::span`, so it reads the column directly.
- **`chart_plot::xs` and `ys` were already pointers' worth of copy only.** Step 2
  had moved the deque to columns, so the `live_window_start` over a deque and the
  `std::deque` in `chart_plot.hpp` were gone. What step 4 deleted is the two
  vectors and the copy into them.
- **The key still needs the stamp, and a compaction always comes with one.** The
  `first_seq` conversion is exercised in a unit test by compacting without
  rebuilding, which the runtime cannot do. It is a guard, not a path.
- **"10 and 110 charts allocate the same" is not true of the chart.** The chrome
  formats seven strings a frame per chart (twelve on a frame that replots, which
  includes the log line). The test asserts the count per chart is the same at 10
  and 110, which is what a copy that grew with the ring would break.
- **A frame test seam.** `render_chart` is declared in `windows.hpp` so a test can
  draw charts without a workspace's layout clipping them.
- **Seven thousand points in view.** The widest span the combo offers is one hour,
  so the benchmark's series is half a second apart and every one of the 7,200 ring
  points is in view.

Measured, Release, 200 charts over full fine rings, median of 15 frames, five
runs. A frame with nothing new is 55.4 ms before and after. A frame after every
series ticked is 62.4 ms before and 58.3 ms after, so the replot of 200 charts
fell from about 6.7 ms to about 2.9 ms. The 2.9 ms is the log lines, the min and
max scan and the test's own appends. It is a CPU figure for building draw lists,
not a frame time.

### 5. The term structure ladder (UR-8)

- `term_structure` gets `version`, stamped from a table counter on `begin`, `set`
  and `fail`, as the market rows are.
- `window_ui_state` caches, keyed on `(version, history)`: the sorted ladder with
  `tenor_rank` computed once, an index from tenor to ladder position as a sorted
  vector and not a `std::map`, the label pointers, and one `ys` per overlay. The
  plot id is `PushID(c)` and a constant label, not `std::format`.
- Source unchanged: still the 30 s read. The actor plan's step 8 changes the
  source to `max(member stamps)` and nothing in the window.

Tests first, in the style of `frame_cost_tests.cpp:88-123,173-178`: the
allocations of one steady term structure frame with 10 overlays equal those with
1 (the CRM test pins the same property over currencies, `:173`), a new read
rebuilds, a change of history rebuilds, and the drawn ladder equals the old
sorted nodes for a curve with unordered tenors. Mutations: key without the
version, key without the history, sort by string and not by rank.

Outcome. Per frame per term structure window: one node vector copy, one sort, one
map, N vectors, N formats go to zero allocations in the steady state. Risk is low.
Deleted: `term_structure_window.cpp:180-193`, `:236-242` per frame, `:277`.

### 6. The series browser (UR-10)

- `series_catalogue::generation`, readable through `series.catalogue(...)`.
- The browser caches, in `window_ui_state`, the filtered and sorted view as
  **indices into the catalogue**, not pointers, keyed on `(generation, filter
  text, asset class, party, synthetic)`. A pointer into the catalogue's vector
  dangles when `set_catalogue` replaces it, which is the failure the generation
  key and the index exist to prevent.
- The two id copies and the party copy at `:273-274,300-301` become views. The
  party list parse at `:309` is keyed on the session's `parties` string.

Tests first: allocations of a steady browser frame equal at 200 and 5,000 rows
(the texture test at `frame_cost_tests.cpp:77-83` pins clipping, not this), a
filter edit rebuilds, a catalogue replaced by a shorter one does not read past
its end, and a refresh with the same rows in a new order shows the new order.
Mutations: key without the generation, cache pointers, key without the party.

Outcome. Per frame per browser: O(n log n) and n allocations to zero when
nothing changed. Risk is low. Deleted: the per-frame filter and sort at
`series_browser.cpp:454-470,556-585`.

**Status: built.** The browser draws from a cached view of indices and keeps no
per-row work between frames.

Deviations from the text above, each the smallest choice that left no ambiguity.

- **`series_catalogue::generation` already existed.** Step 1 added it, readable
  through `series.catalogue(...)` and drawn from the table's one counter, and
  `apply_catalogue`, `begin_catalogue` and `fail_catalogue` bump it. Nothing else
  writes a catalogue's rows, so there was no further change to bump on. This step
  adds a test that it never repeats, including over a removed scope.
- **The plan's allocation claim was wrong about the count.** The old frame
  allocated a constant 8 times at 200 and at 5,000 rows, because `visible` is
  reserved once and the asset class list is short. The cost was bytes and time,
  not a count that grew with rows. "Equal at 200 and 5,000" therefore passes on
  the old code. The test keeps it and adds a bound of 2, the chrome's identity and
  footer text, which the old code fails. The row cost is shown by the
  `[!benchmark]` case.
- **One cache, two shapes.** `series_browser_view::rows` holds the curves when the
  asset class is IR and the series otherwise. The key's asset class tells them
  apart. The asset class list is its own entry keyed on the generation alone, so
  a filter edit does not rebuild it. It sits before the asset class combo in the
  frame, and the view after the combos, as the old code did, so a choice made this
  frame shows this frame.
- **`curve_group` is gone.** Every IR series is its own curve, so a group held one
  member. A curve is a row index, its key is the row's id and its label is
  `curve_label_of`. The "tenors" column still prints 1.
- **The party list is parsed into one buffer.** `party_list` keeps each id and
  name terminated end to end, so the combo takes a `const char*` and nothing is
  copied per frame. Its key is the serialised string and the session's own party
  and name, because the fallback entry depends on them.
- **The IR curve table is still not clipped.** It draws every curve each frame.
  Clipping it is a behaviour change to a view this step leaves alone.

Measured, Release, 5,000 rows, `[!benchmark]` median of 100 samples, three runs. A
steady frame is 0.83 to 0.97 ms and 8 allocations before, 51 microseconds and 2
allocations after, about 17 times faster. It is a CPU figure for building draw
lists, not a frame time.

Mutations run against the finished code. Dropping the generation from the key and
dropping the party from the key are each killed. Keeping pointers instead of
indices behind the generation key is an equivalent mutant: the vector is replaced
whole, a stale generation rebuilds before any read, and only an address
sanitizer sees the dead pointer. With the generation also dropped it fails the
shorter catalogue test, as the first mutation does.

## How a number is taken

The repository has the pieces. `hedgr.ui/tests/frame_cost_tests.cpp` counts
allocations in one steady frame with `hedgr::testing::allocations()` and compares
a small desk with a large one (`:88-123`). `chart_plot_tests.cpp:119-143` and
`lookup_cost_tests.cpp:122-135` hold Catch2 `[!benchmark]` cases. `headless_desk`
builds an ImGui and ImPlot context and a `desk` with the tables
(`headless_desk.hpp:57-134`).

Add `allocations_with_charts(n)` beside `allocations_with_collapsed`: open `n`
chart windows over `n` series each holding a full fine ring, draw three warm
frames, count one steady frame, and check that 10 and 110 charts allocate the same.
Add a second case that appends one bucket to every series and draws once, and a
`[!benchmark]` case of one frame over 200 charts at 7,200 points in Release.

Take it on the base commit with the new test file first, then on each step, so
the before is measured and not remembered. Per `benchmark-checklist`: Release
build, the same machine, the median of several runs, and before trusting the
number assert the work happened (the points drawn equal the points held, the draw
list has vertices) and that no frame was served from a cache the benchmark did not
mean to use. A headless frame has no GPU. It measures the CPU cost of building
draw lists, not a frame time. That a headless chart frame runs `BeginPlot`
without a backend is inferred from `crm` and `series_browser` doing it, not
observed.

The numbers each step should move:

| Step | Number | Before (inferred) | After |
|---|---|---|---|
| 1 | allocations per steady append in a model test | about 1 per 7 | 0 |
| 2 | allocations per closed bucket on the delivery thread | 1 | 0 |
| 2 | slot bytes | 112 | 56 |
| 3 | feed `pump` allocations on an idle desk of 200 charts | one `std::string` per chart, two maps | 0 |
| 3 | feed string compares per frame, 200 charts, 5,000 rows | up to 1,000,000 | 0 |
| 4 | bytes copied per stamp change per chart at 7,200 points | 115,200 | 0 |
| 5 | steady term structure frame allocations, 10 overlays | grows with overlays and nodes (UR-8), not counted | 0 |
| 6 | steady browser frame allocations, 5,000 rows | grows with rows (UR-10), not counted | 0 |

The "before" column comes from reading the code. Each pull request description
replaces it with the measured figure.

## Rejected alternatives

- **Keep the strings and add a hash cache.** The lookup is already allocation
  free and hashes about 108 bytes (`lookup_cost_tests.cpp:53`). A cache would
  save a hash and keep the rest: the string in every bucket, the fat point, no
  generation for a stale watch, no place for a term structure's rings, no
  eviction. The actor still would not have a key.
- **One global ring buffer for all series.** One contiguous run of all points
  would make a single series a strided gather, which is the copy again, and one
  slow series would evict everyone's history. A ring per series row is the unit a
  chart plots and a window holds.
- **A ring per chart window.** Two charts of one series would hold two rings and
  two watches, and a term structure's members could not be shared with their own
  charts. It also loses history when a chart closes. Principle 5 says the series
  is the unit.
- **A pure wrapped ring plus ImPlot `Offset`.** Rejected on the indexer: the
  offset wraps at the count passed, so a span shorter than the ring cannot be
  drawn from a wrapped ring (`implot_items.cpp:500-519`). The workarounds are two
  draw calls plus a bridge segment per plot and wrap-aware fill, candle and EMA
  code, or writing every point twice into a mirrored buffer, which doubles the
  memory. A sliding window with an occasional memmove costs 25 percent more
  memory and makes every consumer plain pointer code.
- **A handle keyed by URI.** Unsafe across parties, see "The handle".
- **Series rows created by the catalogue.** 5,000 rows would own 5,000 rings or
  need a lazy flag on each. Interning on first reference keeps rings to series
  somebody holds.
- **A dirty flag on `window_table` for the watch set** (SR-11's fix). The table's
  `state()` accessor is non-const (`tables.hpp`), so a flag set by the setters
  would miss an edit through it. Handles and a change list make the feed's input
  the thing that changed.
- **A per-level stamp.** Saves a rebuild once a minute. Not worth three counters.
- **Moving the tick's URI canonicalisation off the delivery thread now.** It is
  per tick and per party tick, not per watched series, and a real cost, but it is
  not what the series table is for.

## Open decisions

- **D1. Ring layout.** Recommended: a sliding window over a contiguous buffer with
  25 percent slack, so every span is one pointer and a count. The alternative is a
  pure ring with two draw segments, which is smaller by 25 percent and spreads
  wrap handling over three consumers. Needs the user's choice only if memory is the
  tighter constraint than code.
- **D2. Resident budget.** The value, and whether it is bytes or a count of rows.
  Recommended: bytes, defaulting to 256 MiB, which holds about 400 full series.
  Watched rows are never evicted, so the budget only bounds history nobody is
  looking at.
- **D3. A backfill answer for a series nobody holds.** Recommended: drop it and
  count it, so a slow read cannot grow the table. Today it is stored for any id.
  The visible change: closing a chart while its backfill is in flight and
  reopening it shows nothing until the next backfill.
- **D4. Composite series.** Whether a chart of a series with a coordinate is
  meant to work, and so whether the live ring mixing terms is a bug to fix now.
  The plan assumes it is not in scope and reserves the key part for the actor
  plan's step 8. This is a correctness question that the code cannot answer.
- **D5. Merge by bucket (MS-7).** The new seed merge keys by bucket id. Today a
  backfilled and a live bucket of one second both survive (`market_table.cpp:103-120`).
  Recommended: accept the fix. It changes what a chart shows right after a reseed.
- **D6. `chart_state::max_points`.** Dead and persisted. Recommended: leave it. A
  store migration for no behavior is not worth a step.
- **D7. Incremental candles and EMA.** Not planned. Make them incremental only if
  the step 4 benchmark shows the once-a-second O(points) rebuild across hundreds
  of charts costing a visible fraction of a frame, which would also need the
  feed's second boundaries to align across series (inferred for a synthetic feed,
  not known).

## What cannot be verified without a live deployment

- That ticks of a series with a coordinate interleave in one ring (D4). A live
  check: publish ticks for two terms of one `ir_swap` series and chart it.
- The real size of a tenant's catalogue. The 1,000,000 compares per frame figure
  uses AR-3's 5,000 rows, and SR-11 says 500.
- That the synthetic feed's buckets close together across series, which sets how
  many charts rebuild in one frame. Computed worst case: 200 charts x 7,200
  points. Observed: never.
- Real resident memory under a long session. The 633 KiB per full series is
  computed. A full fine ring takes two hours at one tick a second to fill.
- The cnats delivery thread against the new ring slot. The unit tests call
  `on_tick` from one thread. The live check of `market-eventing.md` is repeated.
- Headless frame costs are CPU only. They say nothing of GPU time or of ImPlot's
  own per-frame culling, which this plan does not change.

## Risks

- **The draw cost does not go away.** `fill_gradient_under_curve` calls
  `PlotToPixels` twice per point per frame (`plot_fill.hpp`), so 200 charts at
  7,200 points is up to 2.9 million transforms a frame (computed, not timed), and
  neither a stamp nor a zero copy touches it. If the benchmark shows it dominates,
  the fix is to decimate to the plot's pixel width before drawing, which is its
  own change, and the contiguous columns make it simple. This plan does not
  promise a frame time.
- **Pointers into columns.** A cache that keeps a pointer across a `pump` reads
  freed or moved memory. The design hands pointers out per frame only and caches
  sequence numbers. A debug build can poison the old buffer on compaction to make a
  violation loud.
- **Reference counts.** A leaked `acquire` keeps a ring unevictable and a watch
  open. The holder audit exists for this and runs in every lifecycle test.
- **The memory claim is modest.** 17 percent per full ring. The gain is the bound,
  not the size.
- **Order.** Step 1 leaves two stores until step 2 lands. Land them back to back.

## How each step is checked

- The full build and suite pass with no warnings under the project's flags.
- Tests land first in each pull request and pass against the unchanged code
  where the code is unchanged, so a move shows no change in behavior. The
  differential test of step 1 is the proof for step 2.
- Each test is mutated against the targets the step names, and a survivor is
  killed or recorded as an equivalence, as the split plan did.
- A step that touches the thread crossing (step 2) names principle 1 and
  principle 4 in its description and shows the ring's single producer and single
  consumer are unchanged.
- Each step states its per-frame cost, as `docs/architecture.md` asks of a change
  on a hot path, with the measured number from "How a number is taken".

## Where it ends

A watched series is a handle, a row, and six contiguous columns per level. The
delivery thread publishes 56 byte flat buckets and allocates nothing. The frame
thread applies a bucket with an index and a generation check. A window holds
handles, the feed reacts to a change list, and an idle desk costs the feed nothing.
A chart plots straight from the table and caches only what it derives. Memory is
bounded by a budget and a scope removal. The term structure has rings to derive
from, and the actor plan's step 8 has the key, the place and the stamp it asked
for.

## Where the six steps ended

Market data is one `series_table` with no second store. A series row carries its
stamp and its rings as contiguous columns, the delivery thread publishes flat
buckets and allocates nothing, and the frame thread applies them by index and
generation. Windows hold their series, the table counts the holders, and the feed
works from a change list, so an idle desk costs it nothing. The chart plots from
the table's columns and copies none of them. The term structure ladder and the
series browser each cache what they derive on a stamp or a generation and cost a
steady frame nothing that grows with their data. `market_table` is deleted, and
the memory bound is a byte budget plus a scope removal.

What remains is the work that needed the key and the stamp this plan built.

- **The session actor's step 8.** The term structure still reads its curves on a
  30 s read. Its member series, the composite key and the `max(member stamps)`
  source are that plan's, and D4 (whether a series with a coordinate should chart
  at all) is still undecided.
- **The IR curve table** draws every curve each frame, unclipped.
- **The draw cost** of `fill_gradient_under_curve` is untouched. If 200 charts
  show it dominating, decimating to the plot's pixel width is its own change.
- **The live checks** under "What cannot be verified" were not run. Every figure
  here is a headless CPU figure, none a frame time.
