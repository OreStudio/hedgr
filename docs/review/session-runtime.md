# Review: hedgr.session and hedgr.runtime

Scope is `projects/hedgr.session` and `projects/hedgr.runtime` (src, include, tests). Every finding below was checked against the code at HEAD (b5a2732). OreStudio citations are from `/home/marco/Development/OreStudio/ores_dev_solid_dirac`, abbreviated `ORES/`. Hedgr paths are relative to `projects/`.

Severity counts: 4 high, 13 med, 14 low (31 total). SR-26 to SR-31 are the OreStudio reuse and consistency findings.

---

## Correctness, threading, lifetime

### SR-1 (high, S): a failed market read marks the live session disconnected
- **Where.** `hedgr.runtime/src/worker/market_worker.cpp:71-82`, `hedgr.model/include/hedgr.model/event/event.hpp:123,143`, `hedgr.runtime/src/manager/instance_manager.cpp:4088-4107`.
- **Defect.** `run_market` sets `fact.kind` only inside the catalogue and points branches. When `connect()` or `resume()` fails, the event goes out with the default kind `session_status_changed` and the default status `disconnected`, and `attempt == 0` skips the stale-attempt filter.
- **Failure scenario.** A chart backfill or catalogue read hits a broker blip or a TLS hiccup on its fresh connection. `apply` runs `sessions_.set_status(disconnected)` on a healthy, authenticated session. On the next frame `pump_market_streams` sees a non-authenticated session and tears down the tick stream. Every `start_*_read` then refuses, because each one checks for `authenticated`. The session worker keeps refreshing tokens, but no event ever sets the status back to authenticated. `catalogues_reading_` or `points_reading_` also stays set forever, because the `series_catalogue` or `series_points` case that erases it never runs.
- **Fix.** Set `fact.kind` from `job.catalogue` before the connect, as every `run_session_read` caller does. Better still, route `run_market` through `run_session_read` (`session_task.hpp`) and delete its copy of the connect, resume and wipe code. Add a test that a connect failure produces a `series_catalogue` event with a message and leaves the session `authenticated`.

### SR-2 (high, M): the SPSC bucket ring has one producer per subscription, not one per connection
- **Where.** `hedgr.runtime/src/worker/market_stream.cpp:199-223,327-333`, `hedgr.model/src/state/bucket_ring.cpp` (`try_push`), `ORES/projects/ores.nats/src/service/client.cpp:741` (`natsConnection_Subscribe`, with no `natsOptions_UseGlobalMessageDelivery` anywhere in ores.nats or Hedgr).
- **Defect.** By default cnats delivers each async subscription on its own thread. Every watched series is its own subscription, and all of them push into the one `bucket_ring` that `market_stream` owns. The only lock is the per-series `series_state::mu`, which does not serialize pushes from different series. The ring's own contract says "exactly one producer". `docs/plans/market-eventing.md` (Decision 2, Threading) assumes a single dispatch thread per connection, which is wrong for cnats defaults.
- **Failure scenario.** Two charts on the same session close a 1s bucket at the same moment. Two threads race on `next_` and on `slots_[next_ % capacity]`. The result is a torn `std::string` move, which means heap corruption, or a lost or duplicated bucket, or `published_` moving past an unwritten slot so the frame thread moves from a garbage string. This is a data race (UB) on the hot path whenever two or more series tick.
- **Fix.** The smallest correct change: replace the per-series mutex with one stream-wide producer mutex, held around `aggregator.on_tick`. Contention is negligible at bucket rate. The structural fix is to ask ores.nats for an option that enables global message delivery, which also removes one OS thread per watched series (up to 512 per session, see SR-25). Add a test with two producer threads against one ring under TSan.

### SR-3 (high, M): in-flight read bookkeeping leaks when a session row goes away, so windows stay stuck after reconnect
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:4095-4103` (early return), which runs before the erases at 4110, 4149, 4164, 4179 and 4243. Also `do_disconnect_session` 2836-2857, `do_remove_account` 1863-1900 and `do_remove_instance` 1692-1820.
- **Defect.** `catalogues_reading_`, `points_reading_`, `deployment_reads_`, `images_reading_` and `currency_images_reading_` are cleared only when the answering event finds a session row. A disconnect removes the row while a read is in flight, the answer is dropped at `if (!handle) return;`, and the key is never erased. `crm_.begin`, `market_.begin_catalogue` and `currency_images_.begin` leave a `reading` state behind in the same way.
- **Failure scenario.** The operator signs out while the series browser's catalogue read is in flight, then signs in again. `start_market_read` returns at `catalogues_reading_.contains(session_key)` forever, so the browser never loads. No chart on that session is ever watched, because `pump_market_streams` needs the catalogue. The command centre is stuck the same way through `deployment_reads_`, and the currency flags through `currency_images_reading_`. Only a process restart clears it.
- **Fix.** Collapse the five sets into one in-flight registry keyed by `(session key, read kind, subject)`. Clear it on session removal, and erase on every answer before the session lookup. Read events also need an attempt or generation tag, so that an answer from a previous incarnation is dropped instead of applied (Model the Domain). Add a test: start a read, disconnect, reconnect, assert that the read can start again.

### SR-4 (high, M): tick callbacks can write into a destroyed `market_stream`; the drain is not synchronous
- **Where.** `hedgr.session/src/market/tick_stream.cpp:121-137,173`, `hedgr.runtime/src/worker/market_stream.cpp:199-205,382`, `ORES/projects/ores.nats/src/service/client.cpp:415-450` (`drain_impl`, `subscription::drain` calls `natsSubscription_Drain`, which is async), `ORES/projects/ores.nats/include/ores.nats/service/subscription.hpp:60` (the doc claims the drain "waits", the implementation does not).
- **Defect.** `market_tick_stream::disconnect` assumes `subscription.drain()` waits for in-flight callbacks, as its comment says. It does not. Each `series_state` holds a `bucket_ring&` and a `std::atomic<size_t>& ticks` that point into the `market_stream` object. `shared_ptr<series_state>` keeps the state alive but not the ring. `unsubscribe` (an erase, which triggers an async drain) has the same gap.
- **Failure scenario.** A tick is being delivered on a cnats thread while the frame thread runs `stop_market_stream`, which is `join()` followed by `erase`. The `market_stream` is freed, and the callback then runs `ring_.try_push`, a use-after-free. This is not rare. SR-5 makes every token refresh (about every 24 min with a 30 min token) destroy and recreate the stream while ticks are flowing. Sign-out, `session_failed` and shutdown hit it too.
- **Fix.** Make the ring and the tick counter owned by a `shared_ptr` that every `series_state` co-owns, so a late callback writes into memory that is still alive. Separately, report the false `drain()` contract to OreStudio, or have the ores wrapper call `natsSubscription_WaitForDrainCompletion`. Once the ring is co-owned, `disconnect` needs no blocking wait.

### SR-5 (med, S): every token refresh tears down and rebuilds the tick stream
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:4233-4241` (calls `start_market_stream`), 3577-3605, 3607-3616.
- **Defect.** A refresh calls `start_market_stream`. That calls `stop_market_stream` (a thread join plus a NATS close on the frame thread), constructs a new entry with an empty `watched`, and so the next pump re-watches and re-backfills every series. The restart buys nothing. `market_tick_stream::connect` resumes with the token locally (`session_client::resume`, `session_client.cpp:1042`, sets auth without a round trip), and a NATS subscribe does not present the JWT.
- **Failure scenario.** With 50 charts open, each refresh causes a frame hitch from the join, drops every open 1s, 1m and 1h bucket, opens a live gap, and starts 50 backfills of 2,000 points (100 requests on 100 fresh connections, see SR-25). It also creates the SR-4 use-after-free window.
- **Fix.** On `session_token_refreshed`, update the token in the session table only. If the stream ever needs a new token, add `market_stream::set_token` and apply it on the next reconnect. Delete the restart.

### SR-6 (med, M): connection loss is never detected; the reconnect and re-seed logic is dead
- **Where.** `hedgr.session/src/transport/session_client.cpp:340-342`, `hedgr.session/src/market/tick_stream.cpp:108-110`, `hedgr.runtime/src/worker/market_stream.cpp:367-377`, `hedgr.runtime/src/manager/instance_manager.cpp:3725-3731`, `ORES/projects/ores.nats/src/service/client.cpp:595-617` (`connected` is a flag set in `connect()` and cleared only in `disconnect()`; the disconnected and reconnected callbacks take a `nullptr` closure and only log).
- **Defect.** `is_connected()` never reflects the real cnats state. cnats reconnects transparently (default 60 attempts, 2 s apart), so `market_stream::run` never takes its `!stream.connected()` branch, `reconnected_` is never set, and the re-seed after reconnect that the plan relies on (Risks, "Reconnect gap") never happens. Once cnats gives up, the connection is closed while `connected()` still answers true, and the stream is dead for the rest of the session. The session worker's 2 s loop issues no request (SR-12), so the session stays `authenticated` while its broker is gone.
- **Failure scenario.** The broker restarts for 30 s. Charts silently miss 30 s of ticks with no backfill. A 3-minute outage leaves every tick stream permanently dead while the UI shows `authenticated` and `live`, followed by a growing stale age.
- **Fix.** OreStudio needs to expose connection status or a reconnected callback with a closure. Until then, have `market_stream` treat "no tick for N seconds while watching" as suspect and recycle the connection, and have the session worker issue a cheap authenticated read (replacing the placeholder) so a dead connection becomes `session_failed`. Delete `take_reconnected` if neither option lands.

### SR-7 (med, M): authenticated requests and tick decoding use a process-global codec that every new client overwrites
- **Where.** `ORES/projects/ores.nats/src/service/client.cpp:482-486` (`client::client` calls `set_default_wire_codec`), `ORES/projects/ores.nats/src/domain/wire_codec.cpp:26-39` (a plain static, no synchronization), `ORES/projects/ores.nats/include/ores.nats/service/request_helpers.hpp:48-95` (every helper uses `default_wire_codec()`). In Hedgr: `hedgr.session/src/transport/session_client.cpp:368` and every `authenticated_request_and_decode` call, plus `hedgr.session/src/market/tick_stream.cpp:61-63`.
- **Defect.** Hedgr supports a per-instance `wire_format` (`instance_manager.cpp:366,495-497,1069,1120`). `anonymous_read` works around the global codec for two reads only. Sign-in, refresh, select_party and every list read, plus `decode_tick`, encode or decode with whatever format the most recently constructed client set. Clients are constructed concurrently on the pool, probe and stream threads, so the static is also written racily.
- **Failure scenario.** One json instance and one msgpack instance are both open. A probe of the json instance constructs a client, and a concurrent msgpack session's sign-in or market read then encodes as json and gets a decode failure, which reads as "the instance answered something this client could not understand". Which request fails depends on timing, so it looks intermittent.
- **Fix.** Encode and decode with a per-session `wire_codec` built from `options_.wire_format`, generalizing `anonymous_read` into a `typed_request<Req, Resp>` helper on `session_client`, and pass the codec to `decode_tick`. Raise upstream that `client::client` should not mutate a global.

### SR-8 (med, M): pool coroutines touch SQLite and frame-owned members (ADR 2 violation)
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:2690-2712` (`master_repo_->create` on the pool, reads `store_lock_`, writes `pending_master_password_`), 2758-2790 (unlock: reads `store_lock_`, writes `pending_master_password_`), 4031-4054 (the frame thread reads and clears the same member), 2820-2828 (lock does not cancel an in-flight unlock).
- **Defect.** ADR 2 says "No other thread may touch [state]". The `sqlgen::sqlite::Connection` behind every repository is shared with the frame thread, which writes windows on the same connection. `do_unlock_store` never checks `unlocking_`, so two unlocks can write `pending_master_password_` from two pool threads while the frame thread clears it.
- **Failure scenario.** (a) A first-run create runs on the pool while the frame thread saves a window: concurrent use of one sqlgen connection object. (b) The operator double-submits an unlock: two pool threads assign the same `secret_string`, which is a race. (c) The operator presses lock while an unlock is deriving: `store_unlocked` arrives afterwards and silently re-unlocks the store the operator just locked.
- **Fix.** Return the derived outcome on the event and keep every repository call and member write on the frame thread. For create, derive the seal on the pool and send the sealed blob back, then `upsert` it on the frame thread, as `start_credential_store` already does. Carry the password in an attempt-tagged handover slot, guard with `unlocking_`, and drop an unlock result whose attempt is older than the last lock.

### SR-9 (med, S): the "newest N points" backfill is ordered by observation id, not by time, and costs two round trips
- **Where.** `hedgr.session/src/transport/session_client.cpp:1313-1385`, `ORES/projects/ores.marketdata/core/src/repository/market_observation_repository.cpp:340-356` (generic list, `list_order(order, {"id"}, false)`) against :273-292 (by series, `observation_datetime` descending), `ORES/projects/ores.marketdata/api/include/ores.marketdata.api/messaging/market_observation_protocol.hpp:216-239` (`list_by_series_id_market_observations_request`).
- **Defect.** Hedgr sends the generic `list_market_observations_request` with a `series_id` filter, then fetches again at `offset = total - limit` on the assumption that the tail is the newest data. The server orders that endpoint by `id` ascending. The second fetch can also see a different `total` than the first.
- **Failure scenario.** A series whose history was backfilled after live data was written (ids do not follow observation time) seeds the chart with an arbitrary slice instead of the newest 2,000 points. Every backfill also costs two requests instead of one.
- **Fix.** Use `list_by_series_id_market_observations_request{series_id, offset=0, limit=N}`, which returns newest first in one round trip, then reverse it. `ORES/projects/ores.marketdata/client/include/ores.marketdata.client/market_data_client.hpp:130` (`list_observations_page`) already wraps this endpoint.

### SR-10 (med, S): a term-structure read pulls up to 20,000 observations per pillar to keep 20 instants, and restored windows poll it
- **Where.** `hedgr.runtime/src/worker/term_structure_worker.cpp:54-56,73-77`, `hedgr.runtime/src/manager/instance_manager.cpp:2929-2930,3086-3088,3147-3150` against 2974-2979.
- **Defect.** Each pillar calls `market_series_points(id, 10000)`, which is two requests (SR-9), decodes up to 10,000 rows, and then keeps the last 20 instants. All pillars run serially on one pool thread. A freshly opened term-structure window parks its deadline at the end of time. A restored one is set to 0 by `rebind_restored_windows`, and any refresh re-arms the 30 s cadence (`do_refresh_window` does not park it).
- **Failure scenario.** A 20-pillar curve restored from the wall issues about 40 requests and decodes about 200k observations every 30 s, holding one of 2 to 4 pool threads (ADR 2: four threads means four round trips in flight) and starving session reads.
- **Fix.** Request a small newest-first page per pillar (SR-9 endpoint, `limit` around 50), or use the curve read the deployment already offers (`list_series_identities` and the slice and evolution reads in `ORES/projects/ores.marketdata/api/include/ores.marketdata.api/messaging/operations_protocol.hpp:275-312`). Make the term-structure deadline policy one rule, applied in open, rebind and refresh alike.

### SR-11 (med, S): `pump_market_streams` allocates and runs a linear search per chart on every frame
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:3618-3741`, 178-188 (`live_windows` builds a vector per call).
- **Defect.** Every frame it builds a vector of all window handles, a `wanted` map of string-pair vectors, a `running_key` string per chart (two UUIDs, so a heap allocation), a `find_if` over the full catalogue (up to 500 rows of string compares) per chart, and a `desired` map per stream. All of this exists to discover that nothing changed.
- **Failure scenario.** With 200 charts at 60 fps, that is roughly 6M string compares and tens of thousands of heap allocations per second on the frame thread, for a watch set that changes only when a window opens or closes or a catalogue lands. This works against the "hundreds of constantly updating windows" budget.
- **Fix.** Recompute the watch diff only when the window table or a catalogue changes: a dirty flag set in `do_open_window`, `do_close_window`, the catalogue apply and session status changes. Index the catalogue by series id. Keep only the per-frame `drain` loop on the hot path.

### SR-12 (med, S): placeholder loops put steady load on both channels
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:3114-3123` and 4264-4273, `hedgr.model/include/hedgr.model/domain/window.hpp:217` (default cadence 1 s), `hedgr.ui/src/render/workspace.cpp:291-296`, `hedgr.runtime/src/worker/session_worker.cpp:380-395`.
- **Defect.** Every window kind without a real read (workspace, accounts, notice, grid, instance manager) keeps the 1 s default cadence. The interface pushes `refresh_window` for it every second. The runtime then copies the whole `window_spec` including the state variant (`windows_.spec(...)`, 3046) and pushes a `window_data` event that `apply` ignores. Each session worker also pushes a placeholder `session_data` event every 2 s.
- **Failure scenario.** 300 such windows produce 300 commands, 300 spec copies and 300 events per second through two mutexes, with string allocations, all doing nothing. The `session_data` "latency" shown is the timer wait, not a network measure.
- **Fix.** Delete the placeholder branch and the `window_data` and `session_data` kinds (Subtract Before You Add). Give kinds with no read an infinite deadline. If a liveness signal is wanted, replace the placeholder with a real cheap authenticated read, which also fixes SR-6's silent session.

### SR-13 (med, S): the frame thread joins the stream thread
- **Where.** `hedgr.runtime/src/worker/market_stream.cpp:239-249`, `hedgr.runtime/src/manager/instance_manager.cpp:3607-3616` and 3686-3688, called from `apply` (4240, 4252), `do_disconnect_session` (2838), `do_remove_account` (1884) and `stop()` (710-715, sequentially per session).
- **Defect.** `market_stream::stop` blocks on `thread_.join()`. The thread may be inside a blocking `client_.connect()` to an unreachable broker (the cnats connect timeout, since Hedgr's `connect_timeout` is not applied, see SR-17), inside `resume`, or inside `natsConnection_Close`.
- **Failure scenario.** An instance's broker goes unreachable and the stream is mid-connect. A sign-out or a token refresh (SR-5) freezes every window until the connect times out. At exit, N streams are joined one after another.
- **Fix.** Request the stop and hand the `unique_ptr<market_stream>` to a reaper (a detached joiner, or the pool) so the frame thread never joins. This is only safe after SR-4's co-ownership fix.

### SR-14 (med, S): `save_windows` rewrites every window as its own transaction on the frame thread
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:4293-4305`, called from 1820, 1900, 2855 and 3031. The store opens in WAL mode with no `synchronous` override (`hedgr.store/src/sqlite/database.cpp:87`), and no transaction wraps the loop.
- **Defect.** One autocommit upsert per window, so one fsync per window, for every disconnect, account or instance removal, and CRM reconfiguration.
- **Failure scenario.** With 300 windows open, a sign-out or a CRM party change stalls the frame for 300 commits.
- **Fix.** Save only the windows whose session binding changed, inside one transaction, or add `window_repository::save_all` that wraps a transaction.

### SR-15 (med, S): token refresh is single-shot, so one transient failure ends the session
- **Where.** `hedgr.runtime/src/worker/session_worker.cpp:336-354`.
- **Defect.** The refresh fires at 4/5 of the token's life. Any failure, including a request timeout of up to 30 s (`session_options.hpp`, `request_timeout{30000}`), pushes `session_failed` and exits, although 20% of the lifetime (6 min for a 30 min token) remains.
- **Failure scenario.** A 10 s broker restart that coincides with the refresh signs the operator out of every workspace on that session. Every window drops to the session-loss state.
- **Fix.** Retry with backoff until the token's actual expiry, and fail only on `authentication_rejected` or once the expiry passes. `ORES/projects/ores.nats/include/ores.nats/service/retry.hpp` (`retry_with_backoff`) already implements the awaitable backoff loop.

### SR-16 (low, S): timeouts and closed connections are reported as protocol errors
- **Where.** `hedgr.session/src/transport/session_error.cpp:128-157`, `ORES/projects/ores.nats/src/service/client.cpp:684-690` (`NATS request timed out` and `NATS request failed: <status>` are thrown as `std::runtime_error`).
- **Defect.** Only `nats_connect_error` and `session_expired_error` are classified. A request timeout or a closed connection becomes `protocol_error`, whose sentence is "the instance answered something this client could not understand".
- **Failure scenario.** A slow deployment shows the operator a decoding fault and sends them to the wrong team. SR-15's retry logic cannot tell a transient timeout from a real protocol fault.
- **Fix.** Map ores' timeout and closed-connection failures to `timeout` and `closed`. Matching the message text is the stopgap. The real fix is a typed error from ores.nats.

### SR-17 (low, S): `connect_timeout` is carried everywhere and applied nowhere
- **Where.** `hedgr.session/include/hedgr.session/transport/session_options.hpp:71`, `hedgr.session/src/transport/session_client.cpp:286-289`, `ORES/projects/ores.nats/include/ores.nats/config/nats_options.hpp:37-81` (no timeout field), `hedgr.runtime/src/manager/connection_options.cpp` (copied into every job).
- **Defect.** The 10 s value is dead configuration. The real bound is cnats' default, and probes, streams and reads all block for that.
- **Fix.** Delete the field until ores.nats exposes `natsOptions_SetTimeout`, or add the field upstream and wire it through.

### SR-18 (low, S): the market catalogue read is capped at 500 series with no pagination
- **Where.** `hedgr.session/src/transport/session_client.cpp:1236-1251` (`limit = 500`, single request), while `crm_names`, `currency_pair_conventions` and `currency_images` in the same file all paginate.
- **Failure scenario.** A tenant with more than 500 series silently loses the rest. A chart of series 501 never finds its row in `pump_market_streams` (3661) and is never watched, with no error shown.
- **Fix.** Paginate on `offset` and `total`, as the other three reads do, or surface a "truncated" note.

### SR-19 (low, S): the `shutdown` command makes `stop()` a no-op and leaves the pools running into member destruction
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:1588-1611`, 698-700, and member order in `include/hedgr.runtime/manager/instance_manager.hpp:622-648` (`pool_` is declared before `events_`, so it is destroyed after it).
- **Defect.** After `command_kind::shutdown`, `started_` is false and `running_` and `probing_` are empty, so `stop()` returns at its guard. `pool_` and `probe_pool_` are then joined only in their destructors, after `events_`, `commands_` and the tables are already destroyed, while detached coroutines may still `events.push()`. No production code pushes this command today (grep finds only the handler), so this is latent.
- **Fix.** Delete the `shutdown` command kind and its handler, or make `stop()`'s guard check whether the pools have been stopped rather than `started_`.

### SR-20 (low, S): dead code in the session path and the executor
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:2454-2455,2490-2493` (`sealed_credential` is never non-empty, since the only caller, at 2409, passes none), `hedgr.runtime/include/hedgr.runtime/worker/session_worker.hpp` (`session_job::sealed_credential`, `master_password`), `hedgr.runtime/src/worker/session_worker.cpp:158-202` (the opening branch), `hedgr.runtime/src/async/executor.cpp:162-168` (`spawn` and `post` are used only by tests), `hedgr.runtime/src/worker/market_stream.cpp:363-365` (`changed` computed and discarded) and 335 (`retry_at` entries never erased).
- **Defect.** About 60 lines describe a second credential path that cannot run. Its comments ("the only place a credential is ever held") are false, because the live path decrypts in `start_stored_connect` and travels through `commands_`.
- **Fix.** Delete the sealed path from `session_job` and `run_session`, delete `executor::spawn` and `executor::post` or use them, and drop `changed`.

### SR-21 (low, S): handler exceptions kill pool threads or vanish
- **Where.** `hedgr.runtime/src/async/executor.cpp:51-67`, and every `co_spawn(..., boost::asio::detached)`.
- **Defect.** An exception escaping `run()` ends that pool thread permanently, so the pool shrinks with no log. An exception inside a detached coroutine is discarded by `detached`, so no event is pushed. The matching `*_reading_` flag (SR-3) or the session's `connecting` status then sticks.
- **Fix.** Loop `run()` inside the thread body, logging and resuming. Spawn coroutines with a completion handler that logs the `exception_ptr` and pushes a failure event.

### SR-22 (low, S): the interface gets a mutable reference to model state
- **Where.** `hedgr.runtime/include/hedgr.runtime/manager/instance_manager.hpp:200` (non-const `images()`), `hedgr.ui/include/hedgr.ui/render/renderer.hpp:427` (`image_cache& images`), `hedgr.model/include/hedgr.model/state/image_cache.hpp:68` (non-const `get`, which updates LRU recency).
- **Defect.** ADR 1 says the UI "reads the state tables and pushes commands". Here the renderer mutates the cache's recency on every draw. It is the same thread, so there is no race, but rendering stops being a pure function of state, and the hole sits in the API.
- **Fix.** Make recency a separate frame-stamp column updated in `pump`, or accept "touch on read" explicitly in ADR 2. Expose only `const image_cache&`.

### SR-23 (low, S): CRM name read failures are swallowed, and CRM reads refetch static conventions
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:4121-4125` (`crm_names` ignores `ev.message`), `hedgr.runtime/src/worker/crm_worker.cpp:66-74`.
- **Failure scenario.** A refused CRM name read shows as "this party has no CRMs". Every CRM matrix refresh also re-pages the full currency pair convention list, which is reference data.
- **Fix.** Add `crm_.fail_names`. Cache conventions per session, or per tenant like `currency_images_`.

### SR-24 (low, S): a stopped session never signs out server side
- **Where.** `hedgr.runtime/src/worker/session_worker.cpp:323-413` (the loop exits on `stop_requested` with no `client.logout()`), `hedgr.session/src/transport/session_client.cpp` (`logout` exists but no runtime caller uses it).
- **Failure scenario.** A disconnect leaves a valid server-side session and refresh token alive until expiry. Each reconnect adds another one.
- **Fix.** On a requested stop, call `client.logout()` (best effort, short timeout) before the coroutine returns.

### SR-25 (med, M): every read opens a fresh NATS connection, and every watched series costs an OS thread
- **Where.** `hedgr.runtime/include/hedgr.runtime/worker/session_task.hpp` (`run_session_read` builds a `session_client` per read), `market_worker.cpp:76`, `deployment_worker.cpp:87`, and the subscription threads from SR-2.
- **Defect.** Each catalogue, points, CRM, image, term-structure and deployment read does a full TCP (and TLS) connect, a local resume and a close. `docs/plans/market-eventing.md` "Why" names this cost for charts. It still applies to every other read, including the 2,000-point backfills the plan introduced. A session also holds an idle connection in `run_session`, used only for refresh, plus the stream's connection. The cnats default also gives each subscription its own thread.
- **Failure scenario.** Opening a workspace with 30 currency flags and 10 charts means about 50 connects in a burst through 2 to 4 pool threads. One session watching 200 series runs 200 cnats delivery threads plus the stream thread.
- **Fix.** Give each session one long-lived read connection, owned by the session worker or a per-session client pool sized 1 to 2, and serialize reads through it. Ask ores.nats for global message delivery (see SR-2).

---

## OreStudio reuse and consistency

### SR-26 (low, S): decimals are parsed with `strtod` and `stod` instead of the parser the server uses
- **Where.** `hedgr.session/src/market/tick_stream.cpp:68-70`, `hedgr.session/src/transport/session_client.cpp:1390`. Reuse `ORES/projects/ores.platform/include/ores.platform/numeric/floating_point.hpp:59` (`ores::platform::numeric::parse_double`), which the tick publisher itself uses (`ORES/projects/ores.marketdata/service/src/app/tick_plan.cpp:42`).
- **Defect.** `strtod` and `stod` are locale dependent and accept a trailing-garbage prefix (`"1.5abc"` parses as 1.5; `strtod`'s end pointer is checked only against the start). The server rejects what Hedgr accepts, so the two disagree on which values are valid.
- **Fix.** Use `parse_double` in both places and drop the malformed row, as `decode_tick` already does for an empty parse.

### SR-27 (med, S): the tick subject comes from string surgery, not from the server's canonical URI
- **Where.** `hedgr.session/src/market/tick_stream.cpp:31-43` (`tick_uri_for_series` swaps `type=series` for `type=quote`). The server builds the subject from `datum::oresmd_uri_codec::write(datum)` (`ORES/projects/ores.marketdata/service/src/app/tick_plan.cpp:38-52`, codec in `ORES/projects/ores.marketdata/core/include/ores.marketdata.core/datum/oresmd_uri_codec.hpp:56-63`). The shell does read, check `is_series`, write (`ORES/projects/ores.shell/application/src/app/commands/marketdata_commands.cpp:384-406`). The protocol also offers `parse_oresmd_uri` and `compose_oresmd_uri` (`ORES/projects/ores.marketdata/api/include/ores.marketdata.api/messaging/operations_protocol.hpp:201-270`).
- **Defect.** The subject token is the exact URI bytes, made by `subject_token(oresmd_uri)`. Any series whose stored URI field order or spelling differs from the codec's canonical quote spelling yields a subject nobody publishes on. The subscription succeeds, so the chart just never shows `live`. The plan's live check proved FX spot only.
- **Fix.** Canonicalize through the codec (link `ores.marketdata.core`'s datum codec as the shell does) or through `compose_oresmd_uri` once per watched series. Add a test that pins Hedgr's subject against `oresmd_uri_codec::write` for one series per asset class.

### SR-28 (low, M): `series_label.cpp` hand-parses oresmd URIs that the deployment already parses
- **Where.** `hedgr.session/src/market/series_label.cpp:30-158` (`series_asset_class`, `series_term`, `series_curve_key`, `series_curve_label`). Reuse `list_series_identities` and `resolve_series_identity` (`ORES/projects/ores.marketdata/api/include/ores.marketdata.api/messaging/operations_protocol.hpp:64-105,275-312`), which return the typed identity (asset class, instrument, quote, ccy, tenor and so on) from the server's own codec, or `market_series_identity` (`ORES/projects/ores.marketdata/api/include/ores.marketdata.api/domain/market_series_identity.hpp:66-200`).
- **Defect.** A second grammar for the URI drifts from the server's. Curve grouping (`curve_key`) feeds the term-structure window, so a grammar change on the server silently splits or merges curves in Hedgr.
- **Fix.** Read identities with the catalogue and derive labels and curve keys from the typed fields. Keep display formatting only.

### SR-29 (low, S): a hand-formatted ISO-8601 timestamp duplicates the platform's
- **Where.** `hedgr.runtime/src/manager/instance_manager.cpp:228-235` (`iso8601_now`). Reuse `ORES/projects/ores.platform/include/ores.platform/time/datetime.hpp:58` (`datetime::to_iso8601_utc`).
- **Fix.** Replace it with `datetime::to_iso8601_utc(std::chrono::system_clock::now())`. This also stays consistent with what the shell prints.

### SR-30 (low, S): the backfill should use ores' observation page read rather than its own two-step list
- **Where.** `hedgr.session/src/transport/session_client.cpp:1313-1424`. Reuse `ORES/projects/ores.marketdata/client/include/ores.marketdata.client/market_data_client.hpp:120-130` (`list_observations_page`, newest first, `list_by_series_id`).
- **Defect and fix.** Same root as SR-9. Listed separately because it is a reuse finding: the typed client already exists over the same `nats_client` Hedgr holds.

### SR-31 (low, S): the token refresh loop re-implements backoff that ores ships
- **Where.** `hedgr.runtime/src/worker/session_worker.cpp:323-378`. Reuse `ORES/projects/ores.nats/include/ores.nats/service/retry.hpp` (`retry_with_backoff`, awaitable and cancellation-aware).
- **Fix.** Build SR-15's retry on it instead of a new loop.

---

## Is `instance_manager.cpp` a god object

Yes. It is 4,322 lines, about 80 member functions, and 40 data members, including five separate in-flight sets, two master-password slots and three executors' worth of stop sources. It does nine unrelated jobs, and every bug class above (SR-1, SR-3, SR-5, SR-8, SR-11) comes from the jobs sharing one `apply` switch and one bag of maps.

Real seams, measured by line range today:

| Proposed unit | Owns | Lines today |
|---|---|---|
| `seed::first_run` (pure, returns rows) | `.env` scanning, `seed_first_run`, `ensure_*`, `seed_*accounts`, icon backfill | 339-586, 997-1400 (about 650) |
| `catalogue` | instances, accounts, folders, labels, labellings, shortcuts, their repos, cascade rules | 1639-2368 (about 730) |
| `store_lock` | `master_password_`, pending slot, `unlocking_`, `lock_error_`, create, unlock, lock, credential seal and open | 2591-2835, 4031-4080 (about 300) |
| `session_supervisor` | `running_`, `attempts_`, connect and disconnect, session events | 2369-2590, 2836-2857, 4088-4108, 4189-4263 (about 350) |
| `read_dispatcher` | one in-flight registry (replaces 5 sets plus `begin` flags), `session_read_for(handle)`, all `start_*_read`, read-event apply | 3157-3290, 3325-3575, 4109-4188 (about 500) |
| `market_feed` | `market_streams_`, watch diff, drain into `market_` | 3577-3741 (about 165) |
| `probe_scheduler` | `probing_`, `probe_intervals_`, schedule, result apply, signals | 3292-3323, 3743-3926, 3986-3995 (about 250) |
| `window_controller` | open, close, refresh, rebind, `save_windows`, deadline policy per kind | 2859-3125, 3126-3155, 4293-4305 (about 450) |
| `instance_manager` (facade) | tables, executors, queues, `pump()` routing | the remainder (about 200) |

`pump()` becomes: drain commands and route by kind to the owner, drain events and route by kind to the owner, then `probes.pump()`, `market_feed.pump()`. Each unit takes the tables it reads by reference, and only the frame thread calls it, so ADR 2's single writer is unchanged. The split is behavior preserving except where it fixes SR-3 (one registry cleared on session removal) and SR-8 (store work moves back to the frame thread inside `store_lock`).

A first cut that pays for itself before the split: `session_read_for(session_handle) -> std::optional<worker::session_read>` replaces the instance, account, token, principal and options block repeated ten times (3181-3205, 3242-3275, 3341-3354, 3364-3380, 3393-3410, 3435-3453, 3469-3487, 3500-3525, 3536-3567, 3585-3591).

Tests the split should land with, all currently missing: watch set follows open and closed charts, session loss tears the stream down, drain applies buckets (all three are named in `docs/plans/market-eventing.md` Tests and none exist), `market_stream` under two producers (SR-2), a stale attempt is dropped by `apply`, token refresh updates the token without dropping watches (SR-5), disconnect with a read in flight then reconnect (SR-3), market read connect failure (SR-1), and the session worker's refresh path. `runtime_tests.cpp` has 40 cases and none of them touches `market_stream`, `pump_market_streams`, the attempt filter or token refresh.
