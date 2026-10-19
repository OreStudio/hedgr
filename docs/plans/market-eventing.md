# Market data by event

How a chart stops polling for its series and starts reading the deployment's
tick stream, without handing the frame loop tens of thousands of events.

## Why

A chart currently asks for its newest 600 points every two seconds. Each read
opens a new NATS connection, resumes the session, fetches, and closes. Twenty
charts mean ten connections a second, a fixed two-second lag behind the market,
and six hundred points moved per chart per poll. The deployment already
publishes every tick on a subject, so the read is doing work the transport can
do once.

The interface stays immediate mode and pull based. What changes is where the
ticks enter: a long-lived subscription per session feeds a bounded ring, the
frame loop drains the ring once per frame, and the chart reads history from the
model as it does now.

## Pipeline

```
marketdata.v1.ops.market_tick.<tenant>.<party>.>
        |  one NATS subscription per session, filtered by series
        v
market_stream                 one per authenticated session
  decode tick
  aggregate into 1s / 1m / 1h buckets
        |  single-producer/single-consumer ring of closed buckets
        v
instance_manager::pump        frame thread, the only writer to the model
  drain ring -> market_table history rings
        v
chart renderer                reads the ring for its span every frame
```

## Decisions

1. **Push at the edge, pull at the interface.** No callbacks reach the UI. The
   frame loop is the only writer to the model, which is the invariant the rest
   of the runtime already relies on.
2. **A ring, not an event queue.** Closed buckets go through a preallocated
   single-producer/single-consumer ring with padded cursors. The producer is the
   NATS dispatch thread for that session's one connection, the consumer is the
   frame loop. Overflow drops the newest bucket and counts it; the producer never
   blocks.
3. **One party subscription, filtered by series.** A series is not a subject
   a feed publishes on: each of its points travels on the subject of the
   quote's canonical URI, which carries coordinates such as a swap's term, so
   no wildcard covers exactly one series. The stream subscribes to
   `market_tick_subject_all(tenant_id, party_id)`, reduces each tick's URI to
   its series through `oresmd_uri_codec` and `series_of`, and keeps the ticks
   whose series is watched. One subscription is one cnats delivery thread,
   which is what makes the ring single producer. (Revised from per-series
   subjects, which never matched a curve family's quotes; see C-3 in
   `docs/review/architecture.md`.)
4. **Live ticks are for the session's party.** A chart viewing another party
   through the picker gets its backfill and manual refresh, not a live stream.
   The tick subject is party scoped, and the session token is for one party.
5. **Retention** per watched series, about 170 KB: 1s for 2h (7,200 points), 1m
   for 2d (2,880), 1h for 30d (720).
6. **All reads the coarse ring.** It updates hourly. Spans up to 2h read fine,
   up to 2d read medium, beyond that coarse.
7. **No scheduled poll.** The open, a reconnect and the manual refresh seed
   history. A silent feed shows as stale, it does not trigger polls.
8. **Backfill merges.** The existing points read seeds history at all three
   levels and merges by time with anything the subscription already delivered.
   It never replaces newer live points.

## Components

### `hedgr.model` history

`market_series_snapshot` gains `fine`, `medium` and `coarse` deques of
`market_point`, each bounded by capacity, plus the monotonic stamp of the last
arrival. `market_table::set_points` buckets a backfill into all three levels and
merges it by time. `market_table::append_point` appends one closed bucket to one
level, trimming the oldest when full. Both are pure and tested without a
transport.

### `hedgr.session` tick stream

`market_tick_stream` owns one `session_client`, connected and resumed on the
stream's own thread. It subscribes once to the party's ticks, decodes
`ores::marketdata::messaging::market_tick`, parses the decimal value, the
observation time and the series the quote belongs to, and calls a handler. A
token refresh replaces the token the next resume uses and nothing else. The
connection reconnects through the wrapped client; after a reconnect the
runtime seeds history again.

### `hedgr.runtime` market stream

One `market_stream` per authenticated session, in a map on the manager. It owns
the stream thread, the watch set, the per-series current buckets and the bucket
ring. The frame thread posts watch and unwatch commands; it drains the ring.

Each frame, `instance_manager::pump` compares open chart windows with the
session's watch set. A chart is watched when its series row is in the
catalogue and the row's party is the session's party. The last chart of a
series unsubscribes it.

On watch, the manager starts the existing one-shot read as a backfill with a
2,000 point limit. The `series_points` event seeds history. The two-second
chart cadence is removed; the chart branch of `do_refresh_window` performs a
backfill on the manual refresh only.

### Interface

The chart picks the level from its span, and the header shows `live` or the age
of the last arrival instead of the poll age. The 1M to All buttons keep their
meaning.

## Threading

- NATS dispatch thread: decode, aggregate, push closed buckets. One producer.
- Stream thread: connect, reconnect, subscribe and unsubscribe. One per session.
- Frame thread: drain the ring, write the model, draw. One consumer.
- No lock is held while drawing. The ring and the model are the only shared
  structures, and the model is single writer.

## Backpressure

- The ring holds 4,096 buckets, about 32 seconds of the busiest case and far
  more in practice. Full means drop the newest and increment `dropped`.
- The producer never sleeps, yields or blocks.
- Per-level current buckets are one point each, so tick rate does not affect
  memory.
- Counters: subscriptions, ticks decoded, decode failures, buckets published,
  buckets dropped, ring high-water mark, last tick age.

## Tests

- Model: bucket boundaries at 1s, 1m and 1h; merge of backfill with live points;
  trimming at capacity; level selection by span.
- Ring: single producer and consumer wrap, overflow drops the newest, batch
  drain order.
- Tick parsing: decimal value and observation time, malformed values dropped.
- Runtime: watch set follows open and closed charts; session loss tears the
  stream down; drain applies buckets to the model.
- Integration: a synthetic feed against a live deployment, chart moves without
  polling.

## Risks

- **Subscription permissions.** The tick subject is party scoped. A token that
  may read market series may not be allowed to subscribe. If the subscription is
  refused, the chart falls back to the backfill and says so.
- **Reconnect gap.** cnats restores subscriptions, but ticks between the drop and
  the reconnect are lost. The backfill after reconnect fills the gap.
- **Late ticks.** A tick older than the current bucket start is dropped and
  counted rather than written out of order.
- **Watch count.** Watches cost one aggregator each and no subscription, so
  there is no cap; the series browser bounds them by what is open.

## Phases

1. Model history, bucketing, merge, capacity, tests.
2. SPSC bucket ring, tests.
3. Tick stream transport, decode, tests.
4. Runtime stream, watch diff, drain, lifecycle.
5. Remove the chart poll, chart level selection, live indicator.
6. Capture against a synthetic feed.

## Live check

Verified against brave_hopper on 17 October 2026 by publishing a tick to the
exact party-scoped subject with the tenant's own credentials. The chart moved
with no poll, logged its first live tick, and showed the age of the last
arrival. The tick stream subscribed with the session's party and the price
landed in the model through the ring.

Two environment problems surfaced and are not in this path. The catalogue read failed to decode because the client had reached
reflect-cpp's default time parser instead of ORE's, which accepts the
deployment's timestamp shape; the ORE reflectors are now included before the
protocol headers in every translation unit that decodes one. The running
marketdata service is also older than the current ORE source and should be
rebuilt with it. And
ORE's NATS disconnect callback can log after the logging core is torn down,
which segfaults at exit; the callback is in libores.nats, after the client has
finished.
