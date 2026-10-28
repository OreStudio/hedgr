# The session actor

How a session ends up owning one connection, one thread and one wire codec,
instead of two long-lived connections and a new connection for every read, in
steps that each land on their own with the suite green.

This is Part D step 10 of `docs/review/architecture.md` (AR-4, AR-6, C-5, C-11,
C-12), with the supervision that `docs/plans/instance-manager-split.md` left in
the facade. It depends on the split, which is done, and on the version-stamped
series table (Part D step 6), which is not. The section "Prerequisites" says
which steps of this plan need that table and which do not.

Line references are to `origin/main` at `b22d160`. A claim marked "inferred" was
read off the code or a header and not observed running. Nothing here was run.

## Why

`docs/architecture.md` principle 5 says one connection and one subscription per
session, and that adding a window never adds a connection. The code does not
meet it. It also has no owner for the one question a connection raises, which is
whether it is still alive.

## What exists today, measured

### Connections per authenticated session

| Connection | Opened at | Lives | Does |
|---|---|---|---|
| Session worker | `session_worker.cpp:115` (built), `:133` (`connect`) | the whole session | signs in, then makes one request at 4/5 of the token's life (`:336-341`) |
| Tick stream | `tick_stream.cpp:99-120` (`connect`, then `resume`), on `market_stream`'s thread (`market_stream.cpp:148`) | the whole session | one party-wide subscription (`tick_stream.cpp:140-150`) |
| One per read | `session_task.hpp:64-68`, `market_worker.cpp:78-81`, `deployment_worker.cpp:87-91` | one request or one batch | `connect`, `resume` with a copied token, call, close |

So a session holds 2 connections the moment it authenticates, whether or not any
window is open. The stream opens on `session_authenticated`
(`instance_manager.cpp:896` calls `market_feed_.open`, `market_feed.cpp:80-107`),
not on the first chart. Every read adds a third and fourth for as long as it
runs. Two more connection sites exist and are not session reads. The password
policy read (`policy_worker.cpp:103`) is anonymous and runs before any session.
The probe (`probe_worker.cpp:185`) is isolated by ADR 5. Neither is in scope.

### Reads, per window kind

The cadence is `window_traits.hpp:130-139`. A read is one connection, so the
rate of reads is the rate of connects.

| Window kind | Read | Cadence | Connections per read |
|---|---|---|---|
| chart | backfill of 2,000 points | on watch, on manual refresh, on reseed (`market_feed.cpp:218-222`, `:234-240`), parked otherwise (`:130`) | 1 |
| series browser | catalogue | 60 s (`:137`) | 1 |
| command centre | deployment | 30 s (`:136`) | 1 |
| term structure | one request per member, 20 points each (`term_structure_worker.cpp:57-61`) | 30 s (`:138`) | 1 for all members, per window |
| CRM matrix | rates, then conventions (`crm_worker.cpp:60,83`) | 5 s per batch (`read_service.cpp:60`, `:494`), shared by windows on the same batch | 1 per batch |
| any image | one image by id (`read_service.cpp:414-430`) | once per id, deduplicated | 1 per image |
| session open | currency image map (`:250-270`) | once | 1 |

A desk of 100 windows, for example 40 charts, 20 term structures, 20 CRM
matrices on 4 batches, 10 series browsers, 5 command centres and 5 others, makes
in steady state `20 x 2 + 4 x 12 + 5 x 2 + 10 x 1` = 108 connects a minute, about
1.8 a second, on top of the 2 long-lived ones. Opening it adds 40 backfills, the
catalogue reads and one connect per distinct image. This is computed from the
cadence table. It was not measured on a running desk, and a slow answer lowers
the CRM figure because the next read starts after the answer.

The review's figure, "windows plus two", counted windows as connections. The
real figure is a rate, and it scales with polling windows, not with charts.

### Threads

- One `std::thread` per authenticated session, `market_stream::run`
  (`market_stream.cpp:50`, `:104-163`). It runs `connect`, the subscription and
  the command loop, waking every 100 ms (`:124`).
- The session coroutine runs on the shared pool (`session_worker.cpp:419`). It
  holds a pool thread only inside a blocking call, and wakes every 2 s
  (`session_worker.hpp:75`, `session_worker.cpp:324`).
- Every read holds one pool thread for its connect and its round trips
  (`session_task.hpp:61`). The pool is 2 to 4 threads (`executor.hpp:67-72`), so
  at most that many reads are in flight across all sessions.
- cnats runs its own threads per connection and one delivery thread for the
  subscription (inferred from cnats and from AR-22, not visible in Hedgr's
  code). Every read connection starts and stops them.
- Probes use their own pool of 2 (`instance_manager.cpp:95,101`).

### Who owns what

| State | Owner today |
|---|---|
| Credential and sealed secret | the job, opened and wiped on a pool thread (`session_worker.cpp:158-217`) |
| Token | the session worker's client, copied to the session table on refresh (`instance_manager.cpp:927-935`), copied again into every read (`read_service.cpp:125`) and into the stream's next resume (`tick_stream.cpp:136`) |
| Tick subscription and bucket ring | `market_stream` through `tick_sink` (`market_stream.hpp:156`, `tick_sink.cpp`) |
| Watch set | the frame thread recomputes it from open charts every frame (`market_feed.cpp:130-155`) and diffs it against `entry.watched` (`:218-232`). Charts only. |
| Attempt counter | the manager, `attempts_[key]` (`instance_manager.cpp:584-587`), checked in `apply` (`:868-873`). Reads carry attempt 0, so they bypass the check. |
| Stop | a `stop_source` per worker polled once per 2 s (`:606-612`, `session_worker.cpp:323-333`) and `market_stream::request_stop` |
| In-flight reads | `pending_reads`, keyed by request id (`pending_reads.hpp:122`) |

### How failure is detected

- A sign-in failure is a `session_failed` event (`session_worker.cpp:90-98`).
- After sign-in the session worker makes one request, the refresh, at 4/5 of the
  token's life, 24 minutes for a 30-minute token (`:289-296`, `:341`). A broker
  that dies in minute 1 is noticed in minute 24. A refresh failure is final
  (`:342-354`), although 20% of the token's life remains (SR-15).
- The placeholder tick every 2 s (`:380-395`) made no request. It carried the
  timer wait as "latency" into `session_table::latency_us`, which fed the
  workspace window's "Last round trip" row. Prerequisite A deleted all of it.
- The stream asks `connected()` (`tick_stream.cpp:122`), which is a flag. In
  `ores.nats` at brave_hopper `connected` is stored true in `connect()`
  (`client.cpp:667`) and false only in `disconnect()` (`:673`). The disconnected
  and reconnected callbacks are registered with a null closure and only log
  (`:574-575`, `:213-230`). The `!stream.connected()` branch
  (`market_stream.cpp:147-155`) and `take_reconnected` (`market_feed.cpp:234`)
  therefore never fire after a loss. This is SR-6, still open.
- A failed read reports to its window and does not touch the session.

### Codec

`ores::nats::service::client::client` writes the process-wide codec on every
construction (`client.cpp:551`, `wire_codec.cpp:26-38`). Hedgr builds a client
per read, so today the global is written at the rate of reads, from pool
threads, with no lock. `anonymous_read` avoids the global for two reads
(`session_client.cpp:121-155`). `decode_tick` (`tick_stream.cpp:62`),
`authenticate`, `refresh` and every authenticated read use it. The guard that
Part D step 0 asked for (refuse two instances with different formats) is not in
the code. This is C-5.

## What a session actor is here

One object per session key `(instance, account)`. It owns:

- one `session_client`, so one NATS connection,
- one `wire_codec`, built from the instance's `wire_format`, passed to every
  encode and decode the actor does,
- one thread running one single-threaded `io_context` of its own, not the
  shared pool,
- the party-wide tick subscription, the `tick_sink` and the bucket ring, which
  today belong to `market_stream`,
- the token, its refresh deadline and its liveness deadline.

It accepts through a command vector, one mutex and one wake, the shape
`market_stream` already has (`market_stream.cpp:75-92`):

- `sign_in` with a credential or a sealed one and the master password, once,
- `read` carrying a typed request and the event to fill,
- `set_watch` carrying the whole desired set of `(series id, uri)` pairs,
- `stop`.

It emits events through the existing event channel, each tagged with the
attempt, and closed buckets through the existing ring.

**The actor never writes a table.** Principle 1 holds unchanged. The frame
thread drains the channel and the ring and applies. The actor reads nothing the
frame thread owns, because everything it needs travels in the command.

**`set_watch` is a level, not a refcount.** The frame thread already derives the
wanted set from the windows each frame and diffs it. The actor receives the
diff result as a set and applies it to its sink. This keeps the reference count
on the frame thread, where the windows are, and keeps the actor free of series
handles. Principle 5's "each window adds a reference to the series" is the
frame thread's derivation. It is the one thing that will change when the series
table lands, and only in what keys the set.

**Relation to `tick_sink` and `market_feed`.** `tick_sink` moves under the
actor unchanged. It is already the single-producer ring behind a mutex, written
by the one delivery thread (tick_sink.cpp, `watch` and `on_tick` hold `mu_`).
`market_feed` keeps the part that is a function of the windows, which is
computing the desired set and applying drained buckets to the market table, and
loses its stream map, its retire list and its `take_reconnected` poll.

**Relation to `read_service`.** `read_service` keeps the gates
(`session_read_for`, `read_service.cpp:116-141`), the request registry, the job
building and the answer application. It stops spawning. Each
`worker::spawn_*_read(pool_, job, events_)` becomes
`actors.submit(session, request)`. The pure work lambdas in the workers, such as
`crm_worker.cpp:57-91`, do not change, because they already take a
`session_client&` and a fact. A read stops carrying a token, a principal and
connection options.

**Credentials.** The actor opens a sealed credential, signs in and wipes both on
its own thread, with the code that is in `session_worker.cpp:158-217` moved
intact. It keeps the token and never keeps the password. That has a consequence
stated below under reconnect.

## Ownership, current and target

| Piece | Today | Target |
|---|---|---|
| Long-lived connections per session | 2 | 1 |
| Connection per read | 1 each | none, a request on the actor |
| Session thread | pool coroutine plus a `std::thread` in `market_stream` | the actor's thread |
| Sign-in and credential wipe | `session_worker` on the pool | the actor, first command |
| Token | worker client, session table, every read job, stream | actor only. The session table may keep a copy for display, and reads stop reading it |
| Refresh timer | 2 s poll, due at 4/5 of life, single shot | actor deadline, backoff until expiry |
| Placeholder tick and `session_data` | present | deleted before the actor (prerequisite A) |
| Failure detection | refresh failure only | liveness request, plus refresh, plus any request failure |
| Tick subscription and sink | `market_stream` | the actor |
| Watch set derivation | `market_feed`, per frame | `market_feed`, per frame, unchanged. The actor receives the set |
| Stream map, retire list, `take_reconnected` | `market_feed` | deleted. A `link_restored` event replaces the poll |
| Attempt counter | manager `attempts_`, sessions only | `session_service`, tags sessions, reads and ticks |
| `running_` and `stop_source` | manager | deleted. The actor has `request_stop` |
| `start_session_for`, `start_stored_connect`, `stop_session` | facade | `session_service` (step 7) |
| `pending_reads` | `read_service` | `read_service`, unchanged, plus actor-stop clears |
| Wire codec | process global, written per client | one per actor, passed to every decode |
| Pool (`pool_`) | sessions, reads, lock coroutines | lock coroutines and the secret derivation for a stored connect only |
| Probe pool, probes, policy read | separate | unchanged |

## Prerequisites

Each is its own pull request and lands before the step that needs it.

**A. Delete the placeholder (AR-5, SR-12, Part D step 1).** Remove the tick,
the `session_data` event, its `apply` case, `refresh_interval` as a poll
interval, the `session_table` `latency_us` column and `session_facts.latency_us`.
The worker waits on the token refresh deadline only, and a stop wakes the wait
at once. The probe's own `latency_us` stays.

This is not invisible. The workspace window's "Last round trip" fact row read
that column, and its only non-zero writer was the tick, whose value was the
timer wait. Option 1 was decided: the row is removed, because a number that
never measured a round trip is worse than none. Step 3's liveness ping may add a
real "Last round trip" back, fed by an actual request. Needed before step 6.

**B. One wire format per process or a refusal (C-5).** Until D2 below is
decided, add the guard Part D step 0 named, refusing a connect when another
live session uses a different format. Test first, two instances with different
formats, the second `connect_session` raises a condition and starts nothing.
Needed before step 3. It can land today.

Status: done. `instance_manager::wire_format_holder` finds a live session
(connecting or authenticated) whose instance row has another format.
`do_connect_session` raises a `session` condition naming both formats and the
holder, before any credential is remembered or derived, and starts nothing.
A probe's connect also writes the process-wide codec, so a probe waits, keeping
its deadline, while a session holds another format. The deployment and policy
reads connect too and are not guarded. The guard keys on the instance rows, so
a row edited under a live session is not seen. Delete it when D2 is decided.

**C. `session_link`, the seam.** One interface over what the workers and the
actor call on a `session_client`, with a forwarding adapter and a fake. It is
step 1, not a prerequisite, because it is a pure addition.

**D. Connection state from ores.nats (SR-6).** The real fix is upstream.
`nats_client::transport()` returns a `client&`, whose state is private to
`impl`. Hedgr cannot reach `natsConnection_Status`. Two changes would do it,
both at `client.cpp:574-575` and `:213-230`. Pass `impl_` as the closure of the
two callbacks and have them update `std::atomic<state>` and a
`std::atomic<std::uint64_t> reconnects`, then make `is_connected()` consult the
state and add `reconnect_count()`. The actor would compare the counter on each
wake and reseed when it moves. This is not a blocker. The Hedgr-side workaround
is the liveness request in step 3, which detects a dead broker within one
interval and a recovered one on the next success. It cannot see a blip shorter
than the interval, and it costs one request per interval. The upstream change
removes both limits. Ask for it when step 3 is in review, not before.

**E. The series table (Part D step 6), not landed.** Planned in
`docs/plans/series-table.md`, which maps these three points to its steps. What
the actor needs from it:

1. A stable key for a watched series, so `set_watch` and the buckets coming back
   name a series by handle and not by `(instance, account, series id)` strings.
   Today the sink keys by canonical URI (`tick_sink.cpp`) and the market table
   keys by concatenated strings (AR-7).
2. A place a term structure's member rings live, so the curve is derived on the
   frame thread from N watches and zero polls (AR-6, second half). This is the
   only part of the target the table blocks.
3. A version stamp on each ring, so a drained bucket that changes nothing costs
   nothing.

Steps 1 to 7 need none of this. They keep the string key, and the actor interface
is a set of strings, which the handle replaces in one mechanical change when the
table lands. Step 8 needs all three.

**F. Channel lifetime, only if D5 chooses detach.** The actor holds
`event_queue&` (a manager member, `instance_manager.hpp:436`). A thread that
outlives the manager would push into freed memory. Joining at exit avoids this.
Bounded wait then detach needs the channel owned by a `shared_ptr` the actor
shares.

## Steps

Each step is one pull request that leaves the application working and the suite
green. "Measure" is the number the step must move, and where it is a desk of 100
windows the figure is counted by a connect counter on the fake link in tests and
by the broker's own connection list on a live deployment.

### 1. The seam, and tests for the workers

> **Status: started.** `session_link` and `session_client_link` exist with
> `connect`, `resume`, both CRM reads, CRM names, the two image reads, the
> deployment read, the series tail, the catalogue, points/history and the
> provider list. The CRM, image, image-map, CRM-names, term-structure and market
> workers map through `run_*_read(link, …)` and are tested against one shared
> fake, including failed connects, refusals, failed pillars and a failed points
> read; the deployment and market workers own their client in a wrapper
> coroutine. The rest of the call surface (authenticate, refresh, logout, ping,
> subscribe) and the session worker follow.

Add `session_link`, an interface of the calls the workers and the actor make:
`connect`, `resume`, `authenticate`, `refresh`, `logout`, `is_connected`,
`ping` (the anonymous `bootstrap_status`), `subscribe_ticks`, and the eight reads
(`market_series`, `market_series_points`, `deployment`, `crm_rates`,
`crm_names`, `currency_pair_conventions`, `currency_images`, `image_by_id`).
`session_client_link` forwards to `session_client` and holds no logic. The work
lambdas and `run_session_read` take `session_link&`, which the compiler drives.
A `link_factory` is passed to the spawn functions the way `tick_feed_factory`
is passed to the manager (`tick_feed.hpp:70`). Production builds the real link,
a test builds a fake.

Tests first, against the unmoved code, through the fake. Each worker's mapping
from a link answer to an event, which only `crm_worker_tests.cpp` covers today.
A failed connect becomes the sentence on the fact, and the token is wiped. The
fake counts `connect` calls, and the first test pins today's behavior, one per
read, which is the baseline every later step lowers.

Mutations. Drop the `resume` call, drop the token wipe, drop the failure message
on a failed `resume`. Each must fail a test.

Risk is low. It is an interface and a signature change, and `session_link` is a
second description of `session_client`'s surface. That duplication is the price
of a test seam, and it is the one place this plan adds a layer without removing
one. Measure: none. The step produces the counter.

### 2. The actor takes over the tick stream

> **Status: started.** The codec half is in: `market_tick_stream` holds its
> deployment's format and decodes through it, and `decode_tick` has a
> codec-taking overload tested against msgpack and JSON. A live stream now
> sleeps until a watch change, a token or a stop, so an idle session costs no
> wakes; only a stream not yet connected/subscribed wakes on the retry cadence.
> The tick stream connects, resumes, disconnects and subscribes through a
> `session_link`, which the stream's run owns; a fake link test covers it. The
> stream is now named `session_actor`; the `set_watch` command level and the
> `read` command follow.

`market_stream` becomes `session_actor`: the same thread, `connect`, `resume`,
subscription, `tick_sink` and ring, now over `session_link`, with the
single-threaded `io_context` and a command vector that accepts `set_watch`,
`stop` and, unused so far, `read`. It still implements `tick_feed`, so
`market_feed` and its fake (`market_feed_tests.cpp:147`) do not change. It
opens on `session_authenticated`, as the stream does. Its codec is built from
the options and passed to `decode_tick`, which stops calling
`default_wire_codec()` (`tick_stream.cpp:62`).

Tests first. `market_stream_tests.cpp` already covers the sink and the stop that
does not wait for a connect (`:149`). Add, over the fake link, a stream whose
`connect` fails then succeeds, a `set_watch` that replaces the set (adds and
removes), a token change used by the next `resume`, and two actors with
different wire formats decoding each other's ticks correctly. The last one fails
today.

Mutations. A `set_watch` that only adds, a decode through the global codec, a
`stop` that joins in `request_stop`.

Risk is low to medium. It is the thread `market_stream` already has, behind a new
loop. The loop changes from a 100 ms poll to a wake on command, which removes
10 wakes a second per session. Measure: connections per session stay 2. Wakes
per second per idle session fall from 10 (`market_stream.cpp:124`) to 0.

### 3. Liveness, loss and reseed (SR-6)

The actor sends `ping` every interval while idle and on any wake after a gap,
and treats a timeout or `closed` as loss. It emits `link_lost` once, keeps its
subscription (cnats is reconnecting underneath), and keeps pinging. The first
success after a loss emits `link_restored`. If D4 adds a status it also moves the
session to a degraded state, otherwise `link_restored` alone drives the reseed.
`market_feed` handles `link_restored` by calling `read_backfill_` for every
watched series, which is what `take_reconnected` was meant to do
(`market_feed.cpp:234-240`). Delete `take_reconnected`, `reconnected_` and
`ever_connected`. The round trip time of the ping is the real latency that the
placeholder pretended to report.

If prerequisite D has landed, the actor also compares `reconnect_count()` and
`is_connected()` on each wake, and a closed connection tears the actor's link
down and reconnects (see "Failure and lifecycle").

Tests first, over the fake link. A ping that times out emits `link_lost` once and
not once per ping. A success after loss emits `link_restored` once. `link_restored`
triggers one backfill per watched series and none for an unwatched one. A loss
that never recovers does not busy-loop. The frame thread is never blocked
(prerequisite for the test, a fake whose `ping` blocks and an assertion that
`request_stop` returns).

Mutations. Emit `link_lost` every ping, skip the reseed on restore, reseed
unwatched series, a ping interval of zero.

Risk is medium. It adds a request every interval per session, 10 s by default
(D4), so 100 sessions add 10 requests a second to the broker. The gain is that an
outage stops being invisible. It cannot be verified without a live broker, see
"What cannot be verified". Measure: time from a stopped broker to `link_lost`,
bounded by one interval plus the request timeout.

### 4. Reads move onto the actor, one kind at a time

Add `session_service`'s first piece, a map from session key to actor, and
`submit(session, request)`. Each pull request converts one group, in this order,
by lowest risk and highest count first.

1. Images and the image map. The most numerous and the simplest, one call each.
2. CRM names and rates. Shared batches, with a 5 s cadence, so the largest steady
   share of connects.
3. Deployment. One call, one window kind.
4. Term structure. One request per member, serial on the actor, so a 12-member
   curve holds the actor for 12 round trips. See D3.
5. Market catalogue and points. Last, because the backfill is entangled with the
   reseed of step 3 and with the feed's watch.

For each group: the `spawn_*` function becomes a request the actor runs with the
same work lambda against the actor's link, with no connect and no `resume`. The
read's job loses `options`, `token` and `principal`. The registry's mark is
cleared by the answer event as today (`pending_reads.hpp`), and also by a new
failure event the actor emits for every queued request when it stops, so a
stopped actor cannot strand a mark (the class of defect fixed in #23).

Tests first, per group, over the fake link and the fake feed. The answer lands in
the table. The read makes zero `connect` calls. A read for a session with no
actor, or an actor that is not signed in, is refused with the same condition the
gate gives now. A session stopped with a read queued clears the mark and reports
a failure, and the next sign-in can read again. A read answered after the pair
was signed in again is dropped by attempt and still clears its mark (see
"Stale attempts"). A refresh in the middle changes the token the next read uses
without any change in the read.

Mutations. Leave the mark set on actor stop, skip the attempt check, run the read
on the frame thread (a test with a link whose call asserts it is not the frame
thread), pass an empty token.

Risk is medium per group, the highest in the market group. One group is one pull
request so a regression names its kind. Measure, per group, connects per minute
on the 100-window desk from the earlier table. After CRM 48 of the 108 are
gone, after deployment 58, after term structure 98, after the catalogue read
all of them. Charts add only their one-off backfills.

### 5. Delete the per-read connection code

Delete `run_session_read` (`session_task.hpp:58-77`), the `connect` and `resume`
blocks in `market_worker.cpp` and `deployment_worker.cpp`, `session_read::token`,
`principal` and `options`, the copy of the token in `read_service.cpp:125`, and
the `session_read_for` gate branches that exist only to decide whether a token
is held. This is the subtraction that follows step 4, and it lands alone so the
deletion is one reviewable diff.

Tests are the existing ones. The PR description shows by grep that a client is
built only in the session worker, the actor's link, the probe worker and the
policy worker. Risk is low. Measure: the connection sites in
`projects/hedgr.runtime/src` and `hedgr.session/src/market` go from 7 to 4, and
to 3 when step 6 deletes the session worker. The 7 are `session_worker.cpp:115`,
`session_task.hpp:64`, `market_worker.cpp:78`, `deployment_worker.cpp:87`,
`policy_worker.cpp:103`, `probe_worker.cpp:185` and the stream's client in
`tick_stream.cpp`.

### 6. Supervision moves into the actor

The actor takes `sign_in` as its first command, so it is created at the connect
command, not at authentication. `session_worker.cpp` is deleted. Its sealed
credential open, sign-in, wipe and event construction move intact. The refresh
becomes a deadline in the actor's loop at 4/5 of the lifetime, retried with
backoff until the token's real expiry and failing the session only on
`authentication_rejected` or expiry (SR-15). The backoff is hand-written because
`retry_with_backoff` (`retry.hpp`) is an awaitable and the actor's blocking
calls are not. Alternatively the actor's `io_context` runs it as a coroutine,
which is why D1 recommends the `io_context`. A user disconnect calls
`logout` best effort before the actor stops (SR-24).

`market_feed` loses `open`, `retoken_or_open`, `close` and its stream map. The
actor subscribes when its own state reaches authenticated and shuts the
subscription when it leaves it, so the feed's per-frame sweep of sessions that
are not authenticated (`market_feed.cpp:196-206`) goes with it.

Tests first. The seam is the fake link with a scripted sign-in. They cover a
sealed credential that cannot be opened (the code and the sentence from
`session_worker.cpp:192-197`), a rejected password, a refresh that fails once and
succeeds, a refresh that fails until expiry, a refresh that changes the token the
next read uses, and the wipe, which is observable by giving the actor a
credential it must not still hold after `authenticate` returns (a test hook that
reads the job after the call). The existing sign-in tests in `runtime_tests.cpp`
must pass unchanged.

Mutations. Skip the wipe, fail the session on the first refresh failure, do not
call `logout` on a user disconnect, tag events with the wrong attempt.

Risk is high. It is the only step that moves secret handling, and it removes the
session worker that has no direct tests today (no test file references
`spawn_session`). Land it last of the thread work and only with the live check.
Measure: long-lived connections per session 2 to 1, and per session threads
(actor plus `market_stream`) 2 to 1.

### 7. `session_service`

What remains in the facade after step 6 is `running_`, `attempts_`,
`start_session_for`, `start_stored_connect`, `do_connect_session`,
`do_disconnect_session`, `stop_session` and the session cases of `apply`
(`instance_manager.cpp:468-730`, `:886-960`). Move them into `session_service`,
which takes the instance, account and session tables and the lock getters by
reference, returns effects for the facade the way `catalogue_service` does, and
holds no manager. The two lock getters are `store_lock()` and
`master_password()`, the same two the split plan named. It lands after the actor
because moving supervision first would move it twice.

Tests are the step 6 tests plus the stale-attempt tests, which are the ones the
split plan said survive the actor. Risk is low. It is a move of code that
step 6 has already rewritten. Measure: `instance_manager.cpp` loses about 260
lines of method bodies and the session cases, and the facade holds no
`std::stop_source`.

### 8. Term structures from member rings

Needs prerequisite E. A term structure window becomes N watches and no read. The
curve is derived on the frame thread from the members' fine rings, the
`term_structure` read kind and its 30 s cadence are deleted, and the 20-point
history of `term_structure_worker.cpp:57` becomes the rings' own history. This
is its own plan once the series table has landed, and it is listed so the end
state is whole. Measure: connects per minute on the desk reach 0 from steady
reads other than the series browser, the command centre and CRM.

## Failure and lifecycle

States of an actor, which is a closed enum on the actor and does not change
`session_status`: `connecting`, `signing_in`, `ready`, `link_lost`, `closing`,
`failed`.

**Connect.** Blocking. The 10 s `connect_timeout` is carried and not applied
(`session_client.cpp:286-289`, SR-17), so the bound is cnats's own default. A
failure is a `session_failed` event with the transport error, with no retry, for
the reason the comment at `session_worker.cpp:134-139` gives. A `stop` that
arrives during connect is honored when the call returns.

**Authenticate.** One attempt with the credential, which is then wiped. The
actor holds no password afterwards. That is the line between the ADR 4 and ADR 6
model and a convenient one, and the plan keeps it.

**Refresh.** At 4/5 of the lifetime the actor sends `refresh`. On a transient
failure it backs off, 1 s doubling to 30 s, and retries until the token's
expiry. On `authentication_rejected` or on expiry it emits `session_failed`. On
success it emits `session_token_refreshed` for the session row and keeps the
token itself. Nothing restarts. The subscription does not present the JWT, so a
refresh needs no resubscribe (SR-5, already fixed in `market_feed.cpp:108`).

**Link loss.** See step 3. While the link is lost, reads that arrive are queued
up to a cap and answered with a failure if the loss outlasts the request
timeout, so no mark is stranded. If cnats gives up, which is its reconnect limit
(inferred 60 attempts 2 s apart from cnats's defaults, since Hedgr's client sets
none, `client.cpp:555-590`), the connection is closed and `is_connected()`
without prerequisite D still answers true. The ping then fails forever, and
after N consecutive failures (a constant, so ping interval times N exceeds
cnats's window) the actor tears the client down and reconnects: `connect`, then
`resume` with the token it holds. That works while the token is valid and a
refresh can then be issued. It cannot work after the token expired, because the
password is gone, and the actor emits `session_failed` and the operator signs in
again. This is deliberate.

**Reconnect and reseed.** On `link_restored` the frame thread starts one backfill
per watched series and marks every polling window due now. The backfill merges
with live buckets and never replaces newer ones (`market-eventing.md`, decision
8), so a reseed is idempotent and a false positive from the workaround costs a
read and corrupts nothing.

**Backpressure.** The command vector is bounded by the registry. `pending_reads`
admits one request per request id, so the queue holds at most one entry per
distinct window read, and the actor adds a hard cap of 256 queued requests,
refusing the newest with a failure event that clears its mark. The event channel
is unchanged and carries the answers. The bucket ring keeps its rule, drop the
newest and count it (`bucket_ring.hpp:64`, principle 4). The actor never blocks
on the channel or the ring.

**Shutdown.** `request_stop` sets a flag and wakes the loop and returns. The
frame thread never joins. The actor goes on the retire list that `market_feed`
already has (`market_feed.cpp:124-130`, reaped at `:251` by `finished()`), now
owned by `session_service`. A queued request is answered with a stop failure. An
in-flight call finishes, up to the 30 s request timeout, because
`io_context::stop` cannot interrupt a blocking NATS call (`executor.cpp:94-99`)
and Hedgr cannot reach `natsConnection_Close`. So the actor does not improve
exit latency over the pool today, which joins the same way. What improves is the
mid-session case, where the frame thread no longer waits. The fix for exit
needs upstream, a `client::close()` that wakes blocked calls, or D5.

**Stale attempts.** Reuse the idea in `lock_service` (`lock_service.hpp:167-173`,
`lock_service.cpp:329`). The manager hands each actor an attempt number when it
creates it, from the one `next_attempt_`. Every event the actor emits carries it,
including read answers, which carry 0 today and so bypass the check at
`instance_manager.cpp:868-873`. `read_service::apply` compares the event's
attempt with the current one for the pair and drops a mismatch, but clears the
registry mark first, because dropping without clearing is what leaves a read
marked running (the comment at `instance_manager.cpp:853-857`). A read in flight
when the operator signs out and in again therefore cannot land in the new
session's tables. Whether that happens today for a changed party or tenant is
not shown, because the tables key by instance and account, not by attempt.

## Rejected alternatives

- **A pool of one or two clients per session** (SR-25's fix). It removes the
  connect cost and keeps two connections, two tokens to keep in step and two
  places to detect loss. The actor is the same change with one.
- **An actor per window.** A hundred windows would be a hundred connections and
  threads, which is the problem.
- **One connection per instance, shared by accounts.** A connection carries one
  token, and an account is a session.
- **Keeping coroutine-per-request on the shared pool over one shared
  connection.** It shares the connection but not its owner. Nothing would own
  loss, the token or the subscription, and a blocked read still holds a pool
  thread.
- **`ores.nats`'s awaitable `client::request`.** It detaches a `std::thread` per
  request (`client.cpp:785-793`), ADR 2's reason, unchanged.
- **Keeping the stream and the session worker as two threads and adding reads to
  one.** It leaves two connections and two token holders, so principle 5 stays
  false.
- **Using tick silence as the loss signal.** A quiet market looks like a dead
  link. A request is the only honest probe.
- **`session_actor` per party.** A tick subscription is party scoped and a token
  is for one party, but a session is one party today and a second party would be
  a second session, not a second actor.

## Open decisions

- **D1. Thread model.** Recommended: a dedicated thread running a single-thread
  `io_context` per session. The thread count equals today's, one
  `market_stream` thread per authenticated session, but ADR 2 rejected "one
  thread per session", so it needs an amendment (Part D step 11). The
  `io_context` gives timers and lets `retry_with_backoff` be reused. Alternative:
  a strand on the shared pool, no new threads, at the cost that a 30 s read
  occupies a pool thread that other sessions need. Alternative: thread plus
  condition variable, the shape `market_stream` has, with a hand-written backoff.
- **D2. Wire format.** One per process, which deletes the per-instance field and
  lets Hedgr use `market_data_client` for the market reads (C-12). Or one codec
  per actor, which keeps the field but means Hedgr writes its own typed request
  helper, because `market_data_client` and `request_and_decode` read
  `default_wire_codec()` internally (`market_data_client.cpp:60,82`,
  `request_helpers.hpp:51,69,90`). Recommended: per actor, with prerequisite B's
  guard until the first release that needs it. The user decides whether two
  formats in one desk is a feature.
- **D3. Concurrency of reads on one session.** Serial is simple and a slow
  request delays the others on that session, including a refresh that falls
  due. A bounded K in flight on the same connection is possible if cnats allows
  concurrent requests on one connection (inferred, not read). Recommended:
  serial, measure the queue wait, revisit.
- **D4. Liveness.** The request, which is `bootstrap_status` (anonymous, so it
  proves broker and IAM and not the token), the interval, 10 s proposed, and
  whether a lost link shows to the operator as a new `session_status` or only as
  a status-bar condition. A new status touches the UI.
- **D5. Exit.** Join, as today. Or bounded wait then detach, which needs
  prerequisite F. And whether the actor logs out on app exit. `logout` takes a
  request timeout of up to 30 s, so logging out on exit would make exit slower
  than it is.
- **D6. Whether to ask for the upstream connection state now** or after step 3
  shows how much the workaround misses.

## What cannot be verified without a live deployment

- cnats's reconnect behavior against a real broker restart: that subscriptions
  restore, how long it keeps trying, and that the ping succeeds on the restored
  connection. Step 3 is tested over a fake link that scripts those answers. The
  fake proves the actor's logic, not cnats's.
- That a request and a subscription callback may use one `natsConnection` from
  two threads. cnats documents its connection as thread safe, and the stream
  already subscribes while the pool reads on other connections, so one connection
  with both is new in this process (inferred).
- Token refresh against the real IAM with the real lifetime.
- The tick subscription's permissions, and the connection count on the broker.
  The live check is the broker's connection list, before and after, with a desk
  of 100 windows, then a stopped broker for 30 s and for 3 minutes.
- TLS. The step 4 and 6 tests use no transport.
- Thread counts. They were read from code, not counted on a running process.

## How each step is checked

- The full build and suite pass, with no warnings under the project's flags.
- Tests land first in each pull request and pass against the unchanged code
  where the code is unchanged, so a move shows no change in behavior.
- Each test is mutated against the target the step names, and a survivor is
  either killed or recorded as an equivalence, as the split plan did.
- A step that adds a thread, a shared object or a connection names the principle
  it relies on in the description. This plan adds none beyond the actor, which
  replaces `market_stream`'s thread, and removes connections.

## Where it ends

A session is a key, a table row and one actor. The actor has one connection and
one codec, a command vector in and events out, and the frame thread is the only
writer. Connections per session go from `2 + 108 a minute` for the example desk
to 1, and the only places in `hedgr.runtime` that build a client are the actor,
the probe and the password policy read. `instance_manager.cpp` drops by the
supervision it still holds. A broker that dies is noticed in one interval and not
at the next refresh. What remains for the series table is step 8.
