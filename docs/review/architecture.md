# Hedgr architecture review

Read-only review of `/home/marco/Development/Hedgr/hedgr` at commit `b5a2732`, against
`docs/adr/0001` to `0008`, `CONTEXT.md`, `docs/plans/market-eventing.md` and
`docs/plans/ui-corrections.md`. OreStudio reference tree:
`/home/marco/Development/OreStudio/ores_dev_solid_dirac` (HEAD `3a49daea81`; Hedgr's
submodule `external/OreStudio` is at `718e142dca`, an ancestor of that HEAD).

Every claim below was checked by reading the cited lines. Sizes: S under a day,
M a few days, L a week or more. Severity: high means it blocks the stated goal
(hundreds of live windows at low frame cost) or breaks a stated invariant; med
means it costs scale, correctness margin or maintainability now; low is hygiene.

Part A states the principles the code implies and where each holds or breaks.
Part B is the numbered findings (AR-1 to AR-40). Part C is the OreStudio reuse
section (C-0 to C-14). Part D is the target structure and a landing order.

## Summary

The shape is right and the ADRs describe a sound design: one writer, two
channels, blocking transport behind coroutines, data-oriented tables, one-way
dependencies. The component edges hold in CMake and in every include. What does
not hold is the stated goal of hundreds of live windows, for three reasons that
compound.

First, the per-frame cost is proportional to what is open, not to what
changed: a vsync busy loop with no idle (AR-2), charts that copy their whole
series into fresh vectors every frame from a `std::deque` keyed by a
concatenated string (AR-1, AR-7), and a watch reconciliation that walks every
window and does a linear catalogue search per chart every frame (AR-3).

Second, the connection topology is per window, not per session: a long-lived
connection used only for token refresh, a second for ticks, and a brand new
connect-plus-resume for every one-shot read, with term structures polling
10,000 points per member every 30 s that way (AR-4).

Third, the live path has correctness defects that would hide behind scale
work: the "single producer" ring has one producer per watched series because
cnats runs each subscription on its own thread (AR-22); the frame thread joins
a thread that can be inside a 10 s connect (AR-23); the tick URI is a string
swap that never matches any curve-family series, so term structures cannot go
live (C-3); the backfill returns a random 2,000-point sample once a series
exceeds 2,000 observations (C-4); and one process-wide wire codec sits under a
per-instance wire format (C-5).

`instance_manager` (4,322 lines) is six services in one translation unit
(AR-16), and the "three places per window kind" promise is fifteen files with
four polling policies that disagree (AR-17, AR-31). Fix order: Part D step 0
(correctness, each a small PR), then idle and dirty tracking, then the series
table, then the split, then the session actor.

---

## Part A. Principles, inferred, with evidence

### P1. One writer. The frame thread owns every state table.

Stated in ADR 2 ("No other thread may touch them") and in the market plan.

Holds on the main paths. Workers push `model::event::event` and never see a
table (`projects/hedgr.runtime/src/worker/session_worker.cpp:80,97,260,366,395`,
`market_worker.cpp:70-110`). `market_stream` is documented and written as
"never touches the model" (`projects/hedgr.runtime/include/hedgr.runtime/worker/market_stream.hpp:51-61`);
its NATS callback only feeds a `bucket_aggregator` and the SPSC ring
(`projects/hedgr.runtime/src/worker/market_stream.cpp:54-64`). `pump()` drains
commands, then events, then probes, then streams, all on the frame thread
(`projects/hedgr.runtime/src/manager/instance_manager.cpp:1420-1456`).

Breaks at the edges.

- `window_table::take_focus_request() const` moves out of a `mutable std::string`
  (`projects/hedgr.model/include/hedgr.model/state/tables.hpp:609,671`,
  `projects/hedgr.model/src/state/window_table.cpp:93-95`) and is called by the
  UI during render (`projects/hedgr.ui/src/render/workspace.cpp:251`). The UI
  mutates a model table through a const accessor. See AR-12.
- `instance_manager::images()` has a non-const overload handed to the app and
  through it into `render_input`
  (`projects/hedgr.runtime/include/hedgr.runtime/manager/instance_manager.hpp`,
  the `image_cache& images() noexcept` line;
  `projects/hedgr.imgui/src/app/application.cpp:744`). The UI side can write a
  model table. Same frame thread, so no race, but the type no longer says who
  writes. See AR-12.
- The signal bus is a process-wide singleton inside `hedgr.model`
  (`projects/hedgr.model/src/signal/signal.cpp:52-58`, `static signal_bus
  instance`) with its own mutex, raised from workers and from the UI
  (`signal.hpp:64`, `application.cpp:787`). It is a third cross-thread channel
  and the only global in the model layer. ADR 8 sanctions the bus but not its
  placement as a global. See AR-13.

### P2. Two channels are the only shared mutable objects.

ADR 2: "the only shared mutable object in the process is a buffer whose lock is
held for the duration of a std::vector swap."

The channels are exactly that (`projects/hedgr.model/include/hedgr.model/command/channel.hpp`,
`push` under `scoped_lock`, `drain` swaps). But the sentence is no longer true
of the process. Shared mutable objects today: the two channels; the signal bus;
one `bucket_ring` per session (sanctioned by the plan); one `commands_` vector
plus condition variable per `market_stream`
(`market_stream.hpp:141-144`); one `std::mutex` per watched series on the tick
path (`market_stream.cpp:55,69`); the atomics `ticks_`, `subscriptions_`,
`reconnected_`; the sound player's queue. None of these is wrong on its own.
What is wrong is that the ADR undersells the inventory, so a reader who trusts
it will not look for the rest. See AR-20.

### P3. Push at the edge, pull at the interface. No callback reaches the UI.

Holds. The plan's pipeline (`docs/plans/market-eventing.md:22-35`) is what the
code does: NATS callback, aggregator, ring, `pump_market_streams` drain,
`market_table::append_point`, chart reads the deque.

### P4. Rendering is a pure function of state. The UI reads tables and pushes commands.

ADR 1 calls this load-bearing. It is mostly true and is leaking in four places.

- The UI decides when the runtime polls. `workspace.cpp:291-296` compares
  `now_ns` to `next_refresh_column()` and pushes `refresh_window`. The runtime
  owns the column and the cadence (`instance_manager.cpp:2924-2932,3049-3056`)
  but not the decision. See AR-11.
- The UI persists its own state file outside the store:
  `load_market_view`/`save_market_view` in `hedgr.ui`
  (`projects/hedgr.ui/src/render/workspace.cpp:113-119`,
  `projects/hedgr.imgui/src/app/application.cpp:145,273`). See AR-15.
- Domain rules live in UI headers: staleness thresholds per asset class, ISO
  8601 parsing, age formatting
  (`projects/hedgr.ui/include/hedgr.ui/render/data_health.hpp`, the
  `fx_health_thresholds`, `parse_iso8601_utc`, `age_text` definitions). The
  model has no notion of data health at all. See AR-14.
- Presentation rules live in the transport layer: `series_label`,
  `series_asset_class`, `series_term`, `series_curve_key` parse an oresmd URI
  into display strings in `hedgr.session`
  (`projects/hedgr.session/include/hedgr.session/market/series_label.hpp`).
  See AR-14.

### P5. Dependencies run one way. `hedgr.ui` never sees `hedgr.runtime`.

Holds in CMake: `hedgr.ui` links only `hedgr.model`, imgui, implot, ores::logging
(`projects/hedgr.ui/src/CMakeLists.txt:50-56`); `hedgr.runtime` links model,
session, store (`projects/hedgr.runtime/src/CMakeLists.txt:51-58`); the
executable links everything (`projects/hedgr.imgui/src/CMakeLists.txt:41-49`).
The include-level audit is in Part B under AR-22 onward.

### P6. State is data-oriented. Hot columns are stride-1, cold blocks are off the per-frame path.

Holds for `window_table` and `session_table` (the workspace walk at
`workspace.cpp:241-273` is the stride-1 walk ADR 2 describes). It was never
applied to market data, which is where the per-frame volume is:

- `market_table` is two `unordered_map<std::string, …>` keyed by a concatenated
  `instance\x1faccount\x1fseries` string
  (`projects/hedgr.model/include/hedgr.model/state/market_table.hpp`, the
  `catalogues_` and `points_` members; `market_table.cpp:31-44,167,184`). Every
  chart builds that key and hashes it every frame
  (`projects/hedgr.ui/src/render/chart_window.cpp:275-278`), and every bucket
  drained builds it again (`instance_manager.cpp:3735`).
- A series is three `std::deque<market_point>` (`market.hpp`, the
  `market_series_snapshot` struct). A `market_point` carries a `std::string
  term` next to five doubles, so the hot series is neither contiguous nor POD.
- The chart copies the visible range into fresh `xs`, `ys` (and `ema`) vectors
  every frame and rebuilds candles every frame
  (`chart_window.cpp:376-401`). There is no version stamp on the snapshot, so
  nothing can know the copy is unchanged.

### P7. Work per frame is proportional to what changed, not to what is open.

Not stated anywhere, and it is the principle the stated goal needs most. The
code does the opposite throughout: the frame loop is a vsync-paced busy loop
(`application.cpp:385,680-681,834`, `glfwSwapInterval(1)`, `glfwPollEvents`,
no wait), every open window re-derives its drawing data from scratch, and the
runtime re-reconciles the watch set from every window every frame
(`instance_manager.cpp:3618-3674`). Idle CPU is a full redraw at monitor rate.
See AR-1, AR-2, AR-3.

### P8. The producer never blocks; overflow is counted, not queued.

Holds for the ring (`bucket_ring.hpp:48-56`, "drops the newest", `dropped_`
counter) and for `market_stream::watch` (push under a short lock). Does not hold
for the event channel, which is unbounded and carries payload vectors of up to
10,000 points per term-structure member
(`projects/hedgr.runtime/src/worker/term_structure_worker.cpp:56`). See AR-9.
Also the ring's `high_water()` is computed and read by nothing
(`bucket_ring.hpp:92`, no caller in the tree), and the plan's "last tick age"
counter does not exist.

### P9. One connection per session.

Implied by the plan ("one NATS subscription per watched series", "one
connection per session"). The code has three connection kinds per session: the
session worker's long-lived client used only for token refresh
(`session_worker.cpp:115,323-396`), the tick stream's own client
(`tick_stream.hpp:130`), and a brand new connect-plus-resume for every one-shot
read (`market_worker.cpp:76` and lines 78-80, `deployment_worker.cpp:87`,
`policy_worker.cpp:103`, `probe_worker.cpp:185`, plus the crm, term structure
and image workers which build a client the same way). A term-structure window
opens a connection every 30 s. See AR-4.

### P10. Every failure reaches the interface, as a typed `std::expected` at each boundary.

Stated in `ui-corrections.md:195-226`. `std::expected` holds at the store and
transport boundaries. The surfacing step is one string: `last_error_` is
assigned at 101 sites in `instance_manager.cpp` and read by one surface
(`workspace.cpp:175-178`). A second failure overwrites the first before the
operator has seen it. See AR-21.

### P11. One owner per concept.

Not stated. Violated for subject building, staleness, time parsing, series
labelling, and the in-flight request registry (six `unordered_set<std::string>`
members that all mean "a read is running", `instance_manager.hpp` private
block). See AR-14, AR-16 and Part C.

### P12. Hedgr reuses OreStudio libraries and speaks the server's protocol types.

Not stated in any ADR. ADR 6 argues the one deliberate exception (crypto). Part
C lists where the code rolls its own.

---

## Part B. Findings

### Data path: NATS tick to pixel

**AR-1. Every chart rebuilds its plot arrays every frame. (high, M)**
Evidence: `projects/hedgr.ui/src/render/chart_window.cpp:376-401` allocates
`xs`, `ys`, optionally `ema`, and `build_candles` output per frame; the source
is a `std::deque` (`chart_window.cpp:282-284`), so ImPlot cannot be pointed at
it. The series browser filters, groups and sorts the whole catalogue per frame
(`series_browser.cpp:357-365,460-475`); the term-structure window sorts its
ladder and builds `ys` per curve per frame (`term_structure_window.cpp:184-241`).
Failure: 200 charts at 7,200 fine points is 200 × 7,200 × 16 bytes copied and
freed per frame, about 23 MB/frame at 60 Hz, before ImPlot draws a line. The
frame budget is spent on copying unchanged data.
Fix: give `market_series_snapshot` a `std::uint64_t version` bumped by
`set_points` and `append_point`; store each level in a fixed-capacity
contiguous ring of `{at_s double, value double}` so ImPlot's `offset` parameter
plots it with zero copy; keep per-window derived data (candles, EMA, ladder,
filtered catalogue) in `window_ui_state` keyed by `(version, span, style)` and
recompute only on change. Split `term` and OHLC out of `market_point` so the
fine series is 16 bytes per point.

**AR-2. There is no frame-rate policy. The loop redraws at vsync whether or not anything changed. (high, S)**
Evidence: `projects/hedgr.imgui/src/app/application.cpp:385` (`glfwSwapInterval(1)`),
`:681` (`glfwPollEvents`, never `WaitEvents`), `:836-837` (a wait exists only
for capture runs). Nothing in `pump()` or the channels can wake the loop; a
tick arriving while the loop sleeps would have nowhere to go, which is why it
never sleeps.
Failure: idle CPU on a desk with no market open equals a full ImGui pass over
every open window at monitor rate; on a laptop this is the fan on and the
battery gone, and under load it leaves no headroom for the frames that matter.
Fix: `glfwWaitEventsTimeout(max_interval)` with `glfwPostEmptyEvent()` called
from the channel `push` and from the ring publish (a one-line hook into
`channel<T>` and `bucket_ring::try_push` through a `std::function<void()>` or a
plain atomic flag the app polls). Then a redraw predicate: input this frame,
events applied, buckets drained, or an animation running. Charts with live
data still redraw at most at vsync; a desk with no data draws at `1 /
max_interval`.

**AR-3. Watch reconciliation runs from scratch every frame. (high, S)**
Evidence: `projects/hedgr.runtime/src/manager/instance_manager.cpp:3618-3674`
walks every live window, does a linear `std::find_if` over the catalogue rows
per chart (`:3657-3660`), builds a `std::string` key per chart (`:3672`) and
two `unordered_map`s per session per frame (`:3624,3693`).
Failure: 300 charts over a 5,000-row catalogue is 1.5 M string compares per
frame, every frame, for a set that changes when a window opens or closes.
Fix: recompute the wanted set only on the events that change it (`open_window`,
`close_window`, `series_catalogue`, `rebind_restored_windows`, session status
change) and cache it on `market_stream_entry`; index catalogue rows by series
id in `market_catalogue` (`unordered_map<string, size_t>` built once in
`set_catalogue`). Independent of AR-7, which removes the strings altogether.

**AR-4. Connection topology does not scale: three connection kinds per session and a new connection per read. (high, L)**
Evidence: `session_worker.cpp:115` builds a long-lived `session_client` that
after sign-in does nothing but refresh the token and push a placeholder tick
(`:323-396`); `tick_stream.hpp:130` owns a second client; `market_worker.cpp:76-80`,
`deployment_worker.cpp:87`, `policy_worker.cpp:103`, `probe_worker.cpp:185`,
`term_structure_worker.cpp`, `crm_worker.cpp`, `image_worker.cpp`,
`crm_names_worker.cpp`, `image_map_worker.cpp` each construct a client,
`connect()`, `resume()` and drop it. Term structures poll every 30 s
(`instance_manager.cpp:2930`) and fetch 10,000 points per member
(`term_structure_worker.cpp:56`), on a new connection each time.
Failure: fifty term-structure windows on ten sessions is a connect and resume
round trip every 0.6 s on average, on the four-thread pool ADR 2 budgets for
round trips, and each one holds a pool thread for the connect timeout when the
broker is slow. The reconnect storm after a broker blip is N windows, not N
sessions.
Fix: one connection owner per session (call it `session_actor`), running on its
own thread as `market_stream::run` already does, holding one `session_client`,
accepting requests and subscription changes through its command vector, and
posting results to the event channel. `market_stream` becomes that actor's
subscription half; the one-shot workers become request types; the session
worker's token refresh becomes the actor's timer. The pool shrinks to probes and
store I/O.

**AR-5. A placeholder event is pushed every two seconds per session. (med, S)**
Evidence: `session_worker.cpp:380-395` ("Placeholder data path. There is no live
data protocol in Hedgr yet") pushes `session_data` with a heap-allocated
sentence at `refresh_interval{2000}` (`session_worker.hpp:75`). The market plan
landed the live path; this is left over.
Failure: N sessions produce N/2 allocations and events per second that
`apply()` must switch on (`instance_manager.cpp`, the `session_data` case), and
the "latency" it reports is the timer wait.
Fix: delete the tick and the `session_data` kind; keep the timer for token
refresh only, at the refresh deadline rather than every 2 s.

**AR-6. Subscription dedupe is per session and per kind, not per series. (med, M)**
Evidence: `pump_market_streams` only considers `window_kind::chart`
(`instance_manager.cpp:3628`); the watch map is keyed by `running_key(instance,
account)` (`:3672`); term structures and CRM matrices poll instead
(`do_refresh_window`, `:3036-3126`).
Failure: two accounts on one instance watching the same series are two NATS
subscriptions and two rings; a term-structure window of twelve members and the
twelve charts of its members are twelve subscriptions plus twelve polls. The
512 cap (`market_stream.hpp:66`) is then per account, not per series.
Fix: after AR-4, key the subscription owner by `(instance, tenant, party)` and
refcount series across every window kind; derive a term structure's current
curve from its members' fine rings on the frame thread, so a term-structure
window is N watches and zero polls.

**AR-7. Market state is keyed by concatenated strings and the hot point carries a string. (med, M)**
Evidence: `market_table.cpp:31-44` (`session_key`, `points_key`), `:167,184`
(lookup per `set_points` / `append_point`); `chart_window.cpp:275-278` (lookup
per frame); `market.hpp`, `struct market_point` has `std::string term`.
Failure: every drained bucket and every chart frame does two string
concatenations and a hash; the point struct is 56+ bytes with a heap pointer,
so a 7,200-point deque is not cache-friendly and cannot be handed to ImPlot.
Fix: a `series_table` in the shape of the other hot tables: a generation-tagged
`series_handle`, hot columns for the three rings and `observed_at_ns`, cold
block for `term`, failure text and catalogue row. `chart_state` holds the
handle; the ring carries the handle instead of `series_id`.

**AR-8. The ring copies a 36-byte string per bucket on the dispatch thread. (med, S, after AR-7)**
Evidence: `bucket_ring.hpp:42-46` (`market_bucket` has `std::string series_id`,
comment says "a key is a UUID, so the copy is cheap enough"); `:97` (4,096
slots each holding a string). A UUID exceeds SSO, so each publish allocates on
the NATS thread and each drain copies again (`:83`, "Copies the published
buckets").
Fix: with a `series_handle` the bucket is 32 bytes of POD and the ring is a
flat array.

**AR-9. The event channel is unbounded and carries bulk payloads. (med, M)**
Evidence: `channel.hpp`, no capacity; `event.hpp:178,184,211,214` carry
`vector<market_series_row>`, `vector<market_point>`, `vector<term_curve>`,
`vector<crm_cell>`; `term_structure_worker.cpp:56` reads 10,000 points per
member; `pump()` applies everything drained in one frame
(`instance_manager.cpp:1430-1431`).
Failure: a reconnect triggers a backfill per watched series
(`:3725-3731`) and a refresh per term structure; the results land within a few
frames and `apply()` merges them all in those frames (`market_table.cpp:128-145`
builds a `std::map` per merge). One frame stalls for the whole burst.
Fix: cap the work `apply` does per frame (apply at most K events or B bytes,
leave the rest in a local carry-over), and make bulk results land in the model
as a swap of a preallocated buffer rather than a merge through `std::map`.

**AR-10. The event struct is a 30-field union by convention. (med, M)**
Evidence: `event.hpp:121-320` (one struct, every field for every kind); ADR 2
says the fixed struct "allocates a string for its payload and nothing else",
which was true when the struct had a `payload` and is not now (ten vectors, a
`deployment_snapshot`, a `password_policy`). `apply()` is a 310-line switch
(`instance_manager.cpp:3997-4307`) with four kinds handled before the switch
and the rest inside it.
Failure: every event, including the 2 s placeholder, default-constructs and
destroys thirty members; adding a kind cannot be checked for a missing payload
by the compiler; `apply` grows without bound.
Fix: `std::variant<session_status_changed, series_points, …>` with
`std::visit`, one `apply_*` per alternative (most already exist as helpers).
Same for `command` (`command.hpp:130-210`).

**AR-11. The UI schedules the runtime's polls. (med, S)**
Evidence: `workspace.cpp:286-296` reads `next_refresh_column()` and pushes
`refresh_window` when due. The runtime owns the column and the cadence
(`window_table.cpp:123`, `instance_manager.cpp:2924-2932`).
Failure: a window that is open but not shown (the `shows_window` gate at
`:283`) never refreshes; a window kind that needs a refresh while hidden has
no way to get one; the policy is split across two components.
Fix: `pump()` scans the column itself (it is a contiguous `int64` span; a
stride-1 pass over hundreds of windows is cheaper than the UI's) and the UI
loses the branch.

**AR-12. The UI writes the model through `mutable` and a non-const accessor. (med, S)**
Evidence: `tables.hpp:609,671` and `window_table.cpp:93-95`
(`take_focus_request() const` clears a `mutable` member); `workspace.cpp:251`
(UI call); `instance_manager.hpp` non-const `images()`; `application.cpp:744`.
Failure: the type system no longer says who writes `window_table`; a future
render-thread split (or ImGui's multi-viewport rendering on another thread)
turns this into a race with no compiler warning.
Fix: `focus_request` becomes a plain field the runtime clears at the start of
the next `pump()`; `render_input.images` becomes `const` and texture uploads
go through the app-owned `textures_`, which already exists
(`application.cpp:747,764-766`).

**AR-13. The signal bus is a global in the model layer. (med, S)**
Evidence: `projects/hedgr.model/src/signal/signal.cpp:52-58` (function-local
static), `signal.hpp:86` (mutex), `application.cpp:787` (drained from the app),
`signal.hpp:64` (raised from workers).
Failure: model tests share one bus across cases; a second manager instance in a
test or a future multi-desk process shares signals; the "only shared mutable
object" invariant now has an exception nobody passes by reference, so it is
invisible at call sites.
Fix: a `signal_bus` instance owned by `instance_manager` beside `events_`,
passed to workers like the event queue, and exposed on `render_input` for the
UI's own raises.

**AR-14. Staleness, time parsing, labelling and formatting have no owner. (med, M)**
Evidence: `projects/hedgr.ui/include/hedgr.ui/render/data_health.hpp` defines
asset-class thresholds, `assess_data_health`, `parse_iso8601_utc`,
`as_of_text`, `age_text`; `projects/hedgr.session/include/hedgr.session/market/series_label.hpp`
parses oresmd URIs into labels, asset classes, terms and curve keys inside the
transport component; `market_status.hpp` is a second status vocabulary. Part B
under AR-22 onward and Part C list the copies found by the cross-component
audit.
Failure: a chart and a matrix can disagree about whether the same series is
stale; a change to the server's URI grammar is fixed in the session component
and missed in the UI's filter (`series_browser.cpp:470-471` matches on `uri`
directly).
Fix: a `hedgr.model/domain/health.hpp` owning thresholds and the assessment;
move URI grammar helpers to `hedgr.model/domain/series_uri.hpp` (or, better,
to the ORE helper if one exists, see Part C) and have session and UI both call
it; one `format.hpp` in model for age and timestamp text.

**AR-15. The UI persists a file of its own. (low, S)**
Evidence: `workspace.cpp:113-119`, `application.cpp:145,273`; `market_view.hpp`
declares `load_market_view`/`save_market_view` with `std::filesystem`.
Failure: a second persistence format beside the SQLite store, with no schema
versioning, no encryption, and no place in ADR 3/4/7's model of what the store
guards.
Fix: `market_view_state` becomes window state (it is already per-kind
preference) persisted through `window_repository` like the rest.

### Ownership and structure

**AR-16. `instance_manager` is six services in one class. (high, L)**
Evidence: `projects/hedgr.runtime/src/manager/instance_manager.cpp` is 4,322
lines; the header's private block holds 13 state tables, 8 repositories, 2
executors, 6 in-flight registries, the market streams map, the probe schedule,
the lock state machine and the window serial. Lines 60-690 of the `.cpp` are
free functions for environment-file parsing (`struct environment`,
`environments_root`, `env_value_of`, `env_key_of`), icon asset discovery
(`instance_icon_directories`, `instance_icon_asset_exists`), seeded label
naming and display names. `handle()` is a 28-case switch (`:1480-1639`) and
`apply()` a 20-case switch (`:3997-4307`). `window_kind::` appears 53 times in
this one file.
The real seams, by which members they touch:
1. **Catalogue store**: `instances_`, `accounts_`, `folders_`, `labels_`,
   `labellings_`, `shortcuts_`, the eight `*_repo_`, `database_`,
   `master_password_`, `pending_master_password_`, `store_lock_`,
   `unlocking_`, `lock_error_`, `startup_failure_`; methods `do_save_*`,
   `do_remove_*`, `do_attach/detach_label`, `rehome_leaves`, `ensure_roots`,
   `do_set_master_password`, `do_unlock_store`, `do_lock_store`,
   `drop_master_password`, `report_store_failure`, `do_store_credential`,
   `start_credential_store`.
2. **Seeding** (first run and development): `seed_first_run`,
   `ensure_development_folder/label`, `seed_instance_accounts`,
   `seed_accounts_for_instance`, `ensure_privileged_folder`,
   `backfill_seeded_icon_keys`, and the 600 lines of free functions. This is
   a one-shot importer and has no business in the per-frame object.
3. **Session lifecycle**: `sessions_`, `running_`, `attempts_`,
   `next_attempt_`, `seed_options_`; `do_connect_session`,
   `start_session_for`, `start_stored_connect`, `do_disconnect_session`,
   `stop_session`, the `session_*` event cases.
4. **Probe scheduler**: `probes_`, `probing_`, `probe_intervals_`,
   `probe_pool_`; `pump_probes`, `spawn_probe_for`, `apply_probe_result`,
   `forget_probe`, `do_probe_instance`, `do_probe_all`. ADR 5 already
   describes this as a separate thing with its own executor.
5. **Market data service**: `market_`, `market_streams_`, `term_structures_`,
   `crm_`, `currency_images_`, `images_`, the six in-flight sets;
   `pump_market_streams`, `start_market_*`, `start_term_structure_read`,
   `start_crm_*`, `start_currency_image_map`, `do_load_image`,
   `do_load_term_structure`, `do_configure_crm`, `do_load_crm_names`, the
   `series_*`, `crm_*`, `term_structure`, `image_*` event cases.
6. **Window service**: `windows_`, `window_repo_`, `next_window_serial_`;
   `do_open_window`, `do_close_window`, `do_refresh_window`,
   `rebind_restored_windows`, `ensure_instance_manager_window`,
   `open_accounts_window`, `save_windows`, `window_key_for`.
Failure: every change to any of the six touches the one translation unit;
`pump()` is the only place the order of the six is written down; a test of the
probe scheduler needs a database path.
Fix: see Part D. The class keeps `pump()`, the two channels and the table
ownership; each seam becomes a component the manager calls in a fixed order,
with the tables it needs passed by reference. The in-flight registries collapse
to one `in_flight` set keyed by a typed request id.

**AR-17. Adding a window kind touches fifteen files, not three. (med, M)**
Evidence: `grep -rc 'window_kind::'` over non-test sources: `instance_manager.cpp`
53, `panels.cpp` 28, `window_json_io.cpp` 20, `window.cpp` 20, `theme.cpp` 10,
`workspace.cpp` 8, `shortcut_bar.cpp` 5, `series_browser.cpp` 3, and seven
more. ADR 1 and `CONTEXT.md:151-155` promise "three places and no others".
The per-kind facts scattered today: cadence (`instance_manager.cpp:2924-2932`),
refresh action (`:3036-3126`), window key (`window_key_for`), title
(`workspace.cpp:298`), closability, store gating (`shows_window`), theme
colour, icon, JSON codec, menu entry, renderer.
Fix: a `constexpr` per-kind trait table in `hedgr.model/domain/window.hpp`
(cadence, needs-session, needs-store, title rule, refresh request kind) indexed
like the renderer table, so the compiler enforces one row per kind. The
renderer table (`renderer.cpp:76`) already shows the pattern.

**AR-18. Idle wakeups and an unneeded lock on the tick path. (low, S)**
Evidence: `market_stream.cpp:200-208` wakes every 100 ms per session with
nothing to do; `:55,69` takes a mutex per tick although the header says the
callback runs on "the dispatch thread of one session's connection"
(`bucket_ring.hpp:50-52`), so there is one caller.
Fix: wait without timeout and have `connect` failures use the existing
`wait_for_retry`; drop `series_state::mu` or state in a comment which second
thread it guards.

**AR-19. `channel::drain` throws the buffer's capacity away. (low, S)**
Evidence: `channel.hpp`, `drain` swaps with a default-constructed vector, so
the next `push` after every frame reallocates; two drains per frame
(`instance_manager.cpp:1428-1431`).
Fix: double buffer with `reserve`, or `drain(std::vector<T>& out)` that swaps
with a caller-owned vector that keeps its capacity.

**AR-20. ADRs, plans and `CONTEXT.md` disagree with the code. (med, S each)**
- ADR 2 "a data event allocates a string for its payload and nothing else" vs
  `event.hpp:121-320`. See AR-10.
- ADR 2 "the only shared mutable object" vs the inventory in P2.
- ADR 1 and `CONTEXT.md:151-155` "three places" vs AR-17.
- `CONTEXT.md:9-10` "An instance is configuration. It does not change while
  Hedgr runs" vs `command_kind::save_instance` and the edit form
  (`ui-corrections.md:9-11`).
- `docs/plans/market-eventing.md:95-97` "The two-second chart cadence is
  removed" vs `instance_manager.cpp:2927-2928` which still gives a chart a 2 s
  cadence, so the first refresh fires from the UI before `do_refresh_window`
  parks the deadline at `int64 max` (`:3049-3056`).
- The plan's counters "ring high-water mark, last tick age" vs
  `market_stream.hpp:106-119` (no high-water accessor, no last-tick age) and
  no reader of `bucket_ring::high_water()` anywhere.
- `ui-corrections.md:3-5` "Nothing here is done yet" while later sections
  record items as landed and verified. The file is a log, not a plan.
- `session_worker.cpp:381-382` "There is no live data protocol in Hedgr yet"
  after the plan's live check on 17 October.
Fix: a short amendment to ADR 2 listing the shared objects and the event
variant; retire `ui-corrections.md` into ADRs for what was decided (the lock
UX, the error rule, the one-icon rule) and delete the rest; fix the two
`CONTEXT.md` sentences.

**AR-21. One string is the whole error surface. (med, S)**
Evidence: `last_error_` assigned at 101 sites in `instance_manager.cpp`, read at
`workspace.cpp:175-178`; the rule at `ui-corrections.md:195-226` asks for
conditions that persist and remarks that fade.
Failure: a store refusal followed by a certificate failure shows the second;
the first is gone with no trace but the log.
Fix: a bounded `condition_table` (code, sentence, first_seen, last_seen,
count, cleared_at) in the model; `last_error_ = …` becomes `conditions_.raise(code, text)`,
and the status bar reads the table. This is also where ADR 8's "status bar is
the second subscriber" lands.

### Concurrency model: where single-writer and the channels are not respected

These come from a dedicated audit of every mutex, atomic, static and cross-thread
call. I re-verified each high and medium finding by reading the cited lines, and
re-checked the cnats behaviour in the solid_dirac tree.

**AR-22. The "single producer" ring has one producer per watched series. (high, S to fix, and it is a data race today)**
Evidence: `market_stream.cpp:174-179` makes one NATS subscription per series,
all aggregating into the one `ring_` (`market_stream.hpp:138`).
`tick_stream.cpp:150-161` calls `client_.subscribe` per series; in the ORE
client that is `natsConnection_Subscribe`
(`/home/marco/Development/OreStudio/ores_dev_solid_dirac/projects/ores.nats/src/service/client.cpp:741`)
and `UseGlobalMessageDelivery` is set nowhere in either tree (zero hits), so
cnats runs each async subscription on its own delivery thread. The per-series
mutex (`market_stream.cpp:55,69`) serialises one series only.
`bucket_ring.cpp`, `try_push`, reads and increments a plain `std::uint64_t
next_` and move-assigns a `std::string` into `slots_[next_ % capacity]` with
no synchronisation between producers. On reconnect, `series.clear()`
(`market_stream.cpp:215`) and `insert_or_assign` (`tick_stream.cpp:161`) let an
old and a new callback for the same series overlap.
Failure: two series tick in the same microsecond; both threads read the same
`next_`, both write the same slot's `std::string`, one publish is lost or
doubled. A chart that watches two or more series is exposed on every tick.
Fix (immediate): enable cnats global message delivery on the tick connection,
or route all callbacks through one dispatch queue, so the plan's assumption
holds; add a debug assertion that `try_push` is called from one thread id.
Fix (structural): the `session_actor` of AR-4 owns one dispatch thread per
session by construction.

**AR-23. The frame thread joins a network-bound thread. (high, S)**
Evidence: `market_stream::stop()` joins (`market_stream.cpp:86-96`); it is
called from `pump_market_streams` (`instance_manager.cpp:3687`) when a session
leaves `authenticated`, from `stop_market_stream` (`:3614`) and from
`start_market_stream` (`:3578`). The run loop checks `stopping_` only at the
top of the loop (`:206`); between checks it is inside `stream.connect()`
(`tick_stream.cpp:99,105`, a blocking connect plus resume) with defaults of 10 s
connect and 30 s request timeouts
(`projects/hedgr.session/include/hedgr.session/transport/session_options.hpp:63,71`).
`instance_manager::stop()` (`:698-755`) then `executor::~executor` join the pool
the same way, and `io_context::stop` does not interrupt a synchronous NATS call
(`projects/hedgr.runtime/src/async/executor.cpp:94-99`).
Failure: the broker drops, the session fails, the next `pump()` joins a thread
that is mid-reconnect to an unreachable host; the UI freezes for up to 40 s.
Closing the app during any outstanding request hangs for the request timeout.
Fix: never join on the frame thread. `stop()` sets the flag and notifies; the
thread is moved to a reaper list the manager drains when `joinable()` and
finished (an atomic `done` flag), or detached after `disconnect()`. For the
pool, cancel through the client (a `close()` that wakes blocking calls) before
join, and join off the frame thread at shutdown.

**AR-24. Pool coroutines read and write manager members. (med, S)**
Evidence: the unlock and set-master-password coroutines capture `this`
(`instance_manager.cpp:2702-2730,2773-2815`), run
`master_repo_->create(...)` on the pool thread (`:2708`), write
`pending_master_password_` (`:2727,2797`) and read `store_lock_`
(`:2713,2811`, and `:2597` through `report_store_failure` called from the
stored-connect coroutine at `:2564`). The frame thread writes `store_lock_`
(`:2742,2759,2832,4042`) and moves out of `pending_master_password_`
(`:4041,4054`). Nothing but the UI's `unlocking` guard
(`workspace.cpp:238`) prevents two unlocks in flight; `do_unlock_store` does not
check `unlocking_`.
Also: the SQLite connection is documented as "exactly one"
(`projects/hedgr.store/include/hedgr.store/sqlite/database.hpp:35-36`) and is
used from the pool at `:2708` while the frame thread uses it for every other
repository.
Failure: a `lock_store` command lands while an unlock is failing: a racy read
of `store_lock_` and a stale lock state in the event; a stored-connect failure
clears the pending secret while the worker writes it. The store write off the
frame thread is undefined unless SQLite is in serialized mode.
Fix: the coroutine carries the password by value and returns it inside the
event (the event already carries secrets elsewhere); the lock state it reports
is the frame thread's business, so drop `fact.lock = store_lock_` and let
`apply` fill it. Key derivation stays on the pool, the `create()` write moves
to the frame thread after the event, or the store gets its own connection for
the pool.

**AR-25. Workers push onto the command channel. (low, S)**
Evidence: `instance_manager.cpp:2586,2660` push a `retry` command from a pool
coroutine carrying a decrypted password.
Failure: thread-safe, but ordering against UI commands is nondeterministic and
a secret rides a channel the ADR describes as interface-to-runtime.
Fix: make it an event; `apply` issues the connect.

**AR-26. Overflow and health counters are computed and read by nobody. (low, S)**
Evidence: `market_stream::dropped()`, `ticks()`, `subscriptions()`,
`bucket_ring::high_water()` have no readers outside tests; `bucket_ring::drain`
is documented as "copies" and implemented as `out.clear()` plus move
(`bucket_ring.cpp`, `drain`).
Fix: expose them on `render_input` through a small `stream_stats` row per
session and show them on the status foot; fix the comment.

**AR-27. Exit waits for every queued sound. (low, S)**
Evidence: `projects/hedgr.ui/src/sound/player.cpp:199-204` drains the queue
before honouring stop; `:294` joins on the main thread.
Fix: check the stop flag per item.

**AR-28. Hidden mutable statics in the UI. (low, S)**
Evidence: file-scope mutable globals at `instances_window.cpp:111-126`,
`accounts_window.cpp:79`, `command_centre.cpp:86`; a `capture_run` singleton at
`widget/capture.cpp:167`; `static thread_local std::string` inside inline
header functions at `widget/controls.hpp:185,278` (one copy per shared object
that includes it). All frame-thread only.
Fix: move into `window_ui_state` or the workspace.

Checked and correct: `channel<T>` is a swap under a lock and only `pump()`
drains it; the ring's acquire/release pairing is right for one producer; every
pool worker takes its job by value plus `event_queue&` and captures no manager
state except the unlock paths above; `pump()` makes no synchronous network
call; the UI gets every table by const reference except `images`; no row
pointer is held across frames; loggers are `_mt`.

### Dependency rules and cross-component duplication

**AR-29. Component edges hold; the executable reaches past runtime. (med, S)**
Evidence: the include matrix over `src`, `include` and `tests` is clean (model
includes model; store, session and ui include model only; runtime includes
model, session, store; no ui file includes runtime or session). The executable
links `hedgr.session.lib`, `hedgr.store.lib` and `hedgr.model.lib`
(`projects/hedgr.imgui/src/CMakeLists.txt:41-49`) and includes
`hedgr.session/transport/session_options.hpp`
(`projects/hedgr.imgui/src/app/application.cpp:27`), building
`session_options` by hand at `:122-128` and dropping `wire_format` while
`instance_manager.cpp:487-495` reads the same variables from `.env` files
with `wire_format`. ADR 1's diagram gives the executable only runtime and ui.
Fix: a runtime `connection_options_from(config)` and the executable loses the
session and store links.

**AR-30. PUBLIC links and public headers leak NATS, sqlgen and asio into every consumer. (med, S)**
Evidence: `instance_manager.hpp:37-49` includes `image_map_worker.hpp`,
`image_worker.hpp` (which include `session_task.hpp` and through it
`session_client.hpp` and ores.nats), the store repositories and
`sqlite/database.hpp` (sqlgen), and `async/executor.hpp` (boost::asio), so
`application.hpp:26` compiles against all three. `hedgr.model` links reflectcpp
and magic_enum PUBLIC with no public header using either
(`projects/hedgr.model/src/CMakeLists.txt:30-33`; reflectcpp is used only in
`src/domain/window_json_io.cpp`, magic_enum nowhere). `hedgr.session` links
`ores::iam.client`, `iam.api`, `marketdata.api`, `assets.api`, `logging`,
`utility` PUBLIC while its public headers include only ores.nats
(`projects/hedgr.session/src/CMakeLists.txt:36-45`). `ores::logging` is PUBLIC
in ui, runtime and store with no public header including it. Boost headers are
included by model (`image_cache.hpp:25-28`, `export.hpp:23`) with no Boost
link target, so model compiles only because the include path arrives
transitively.
Fix: worker headers move to `runtime/src`; `instance_manager.hpp` forward
declares; flip the listed PUBLICs to PRIVATE; link `Boost::headers` where the
headers are included.

**AR-31. Polling policy per window kind is decided in four places that disagree. (high, S)**
Evidence (all `instance_manager.cpp`): focusing an existing window
`:2896-2900` (chart and term_structure never poll, crm_matrix refreshes at
once), new window `:2974-2978` (chart, term_structure, crm_matrix never poll),
after a refresh `:3056-3059` (chart and crm_matrix never; term_structure every
30 s), rebind of a restored window `:3147-3150` (only chart never). The cadence
table `:2923-2932` says term 30 s and crm 5 s, honoured on some paths only.
Cadence is persisted (`projects/hedgr.store/src/repository/window_repository.cpp:76,102`),
so restored windows carry whatever cadence the code had when they were saved.
Failure: the same term-structure window polls or not depending on whether it
was opened, focused, refreshed or restored.
Fix: the per-kind trait table of AR-17, and stop persisting cadence.

**AR-32. One column, two timestamp formats. (high, S)**
Evidence: runtime `iso8601_now` (`instance_manager.cpp:228-235`,
`"%Y-%m-%dT%H:%M:%SZ"`) writes `instance.created_at` at `:1081,1131`; store
`timestamp_now` (`projects/hedgr.store/src/repository/query.cpp:29-37`,
`"%Y-%m-%d %H:%M:%S"`) fills a blank `created_at` at
`instance_repository.cpp:117` and is used by `master_repository.cpp:136`; the
UI has a third formatter, `as_of_text` (`data_health.hpp:122-131`).
Fix: the store owns the one formatter and the runtime stops writing the
column.

**AR-33. Health verdicts, clocks and timestamps have three owners and mixed clocks. (med, M)**
Evidence: thresholds in UI (`data_health.hpp:47-60`); the CRM window at HEAD
gives two verdicts for one cell, the footer's client-side age from the
`observed_as_of` string (`crm_window.cpp:95-101`, parsed with `sscanf` every
frame via `parse_iso8601_utc`, `data_health.hpp:140-163`) and the server's
`cell.status` words (`crm_window.cpp:159-179,204,303,330-336`), which can
disagree. `observed_at_ns` is steady-clock in `market_worker.cpp:119` and in
probe results (`instance_manager.cpp:3852`) while `market_point.at_ns` is wall
clock, both plain `int64`. `instances_window.cpp:1053-1054` reads
`system_clock` directly instead of `input.wall_now_ns`. The `now_ns` helper is
copied five times (`application.cpp:86-90`, `market_worker.cpp:58-62`,
`instance_manager.cpp:333-337`, `deployment_worker.cpp:100`,
`crm_worker.cpp:43-47`).
Fix: `model::domain::steady_ns` and `wall_ns` as distinct types; one
`health.hpp` in model; parse `observed_as_of` once at the boundary and store
`wall_ns`; the renderer reads only `render_input` clocks.

**AR-34. Error-to-sentence mapping is re-done in every worker. (med, S)**
Evidence: `session_error.cpp` already builds the sentence (`message_for`,
`make_error`); then `market_worker.cpp:52-56` and `deployment_worker.cpp:61-65`
are identical `describe()` copies with a dead "empty message" branch,
`policy_worker.cpp:55-60` prefers `detail` and prefixes the subject,
`probe_worker.cpp:42-64` writes "NATS is down" or "IAM is down",
`term_structure_worker.cpp:63-64` uses message else detail, and the crm,
crm_names, image and image_map workers drop `detail`.
Fix: one `describe(const session_error&)` in session returning headline and
detail; workers copy the struct.

**AR-35. Subjects and prefixing: a dead second implementation and two hard-coded strings. (med, S)**
Evidence: Hedgr's `make_subject` (`session_options.hpp:92-100`) accepts an
empty prefix and a trailing dot; the ORE client's `make_subject` always appends
`.` and throws on an empty prefix, and it is the one that runs;
`session_client::subject()` (`session_client.cpp:1425`) has no callers and
`make_subject` is used only by tests. `policy_worker.cpp:45` hard-codes
`"iam.v1.ops.get_password_policy"` for log sentences while the real request
goes through the ORE request type (`session_client.cpp:1023-1027`);
`session_worker.cpp:103` hard-codes `"iam.v1.ops.bootstrap_status"` for the
placeholder of AR-5.
Fix: delete `make_subject` and `subject()`; take the subject string from the
ORE request type's own constant (see Part C).

**AR-36. Ticks and history points are parsed two ways. (med, S)**
Evidence: `tick_stream.cpp:66-77` uses `strtod`, requires `isfinite` and
`at_ns > 0`; `session_client.cpp:1387-1397` (history points) uses `stod` in a
try/catch with neither check, so backfill accepts `inf` and `NaN` that live
ticks reject, and the merge at `market_table.cpp:128-145` then mixes them.
Fix: one `parse_observation` in session (or the ORE decimal parser, Part C).

**AR-37. Structured data crosses to the UI as text the UI parses back. (med, M)**
Evidence: `series_label.cpp:86-110` builds `"FX EUR/USD"`; `widget/flags.cpp:57-77`
splits that label on the second word and `/` to find flags
(`series_browser.cpp:105`). Parties are serialised with `\x1e` and `\x1f` in
`session_worker.cpp:238-240` and parsed in `series_browser.cpp:83-99`.
Fix: `market_series_row` gains `base`, `quote`, `asset_class`, `term`; the
session event carries a `std::vector<party>`.

**AR-38. Prices show different precision per surface. (med, S)**
Evidence: `chart_window.cpp:136-144` picks 2 to 5 decimals by magnitude;
`crm_rate.hpp:44-61` uses the server convention's `decimal_places`; the ImPlot
axes hard-code `"%.5f"` (`chart_window.cpp:429`,
`term_structure_window.cpp:232`). USD/JPY shows 3 decimals in the label and 5
on the axis.
Fix: per-instrument precision on the catalogue row (from the server
convention), read by every surface.

**AR-39. Smaller duplicates. (low, S each)**
`tenor_rank` twice (`term_structure_window.cpp:51-77`,
`series_browser.cpp:153-180`, the second with external linkage); OHLC rollup
twice (`bucket_aggregator.cpp` and `chart_window.cpp:213-238`, where all-zero
OHLC is treated as "plain point", so a true high of 0.0 is replaced by the
close); age and latency text in four formats (`data_health.hpp:166-175`,
`command_centre.cpp:286`, `instances_window.cpp:1928,1935`,
`workspace_window.cpp:251,263`, `crm_window.cpp:199`); `chart_span_labels`
duplicating `chart_spans` by hand (`market_view.hpp:37-47`); probe state words
twice (`theme.cpp:494-515`, `probe.cpp:23-45`); the byte-budgeted LRU twice
(`image_cache.hpp`, `image_texture_cache.cpp`); `ORES_NATS_*` read twice
(`config/parser.cpp:180-197`, `instance_manager.cpp:487-495`).

**AR-40. Window kinds: the variant is not aligned with the enum, and two kinds have no opener. (med, S)**
Evidence: `window.hpp:191` has 9 variant alternatives for 10 kinds and nothing
ties a kind to its state, so ADR 1's "the compiler enforces the first two" is
not true; `grid` and `notice` have no code path that sets `new_window_kind`,
so they are reachable only through restored rows and grid's default `"trades"`
state is dead; `crm_matrix` opens only from `series_browser.cpp:439`;
`series_browser` opens from two menus (`workspace_window.cpp:113-121`,
`command_centre.cpp:466-472`).
Fix: part of the trait table in AR-17; delete `grid` and `notice` or give them
an opener.

---

## Part C. OreStudio reuse and protocol consistency

Reference tree: `/home/marco/Development/OreStudio/ores_dev_solid_dirac` at
`3a49daea81`. Paths below under `ores/` mean that root. I verified the three
high findings (C-3, C-4, C-5) and the refusal and include-order findings by
reading both trees; the rest are from the audit and were spot-checked.

**C-0. The pin and the build disagree, and the submodule is empty. (med)**
Hedgr's gitlink pins `external/OreStudio` at `718e142dca` (February 2026, PR
#412), which is 3,071 first-parent merges behind the reference HEAD and
predates `ores.nats`, `ores.marketdata`, `ores.platform` and
`ores.utility/rfl/time_point_parser.hpp`. The directory is empty, so Hedgr has
never built against the pin. What it builds against comes from CMake caches:
the debug cache points at solid_dirac (`3a49daea81`) and the release cache at
`ores_dev_brave_hopper` (`c725c76a92`), and `ores.nats` client, subscription
and the observation filter differ between those two. Debug and release Hedgr
compile against different protocol headers. Fix: pin the submodule to the tree
the build uses, initialise it, and make `ORES_BUILD_DIR` derive from it.

**C-1. Subjects AGREE.** The tick subject comes from
`ores::marketdata::domain::market_tick_subject` (`tick_stream.cpp:146`,
`ores/projects/ores.marketdata/api/include/ores.marketdata.api/domain/tick_subjects.hpp`),
the same helper the server uses (`ores/projects/ores.marketdata/service/src/app/tick_plan.cpp:50`).
Every request subject is `Request::nats_subject` on the server's own type
(`session_client.cpp:142,369,470,564,614,660,705,751,785,836,903,1097,1142,1188,1261,1346`).
Duplicates: the hard-coded `"iam.v1.ops.bootstrap_status"` at
`session_worker.cpp:103` (equal to
`ores/projects/ores.iam/api/include/ores.iam.api/messaging/bootstrap_protocol.hpp:34`
today) and the dead `make_subject` of AR-35, which diverges from
`ores/projects/ores.nats/src/service/client.cpp:619` on empty prefix, trailing
dot and `_INBOX`.

**C-2. Protocol types AGREE.** Hedgr names the server's request and response
types for login, parties, refresh, select_party, logout, bootstrap_status,
password_policy, tenant_types, tenants (ores.iam.api), crm_rates,
market_series, market_observations (ores.marketdata.api), crm_topology_configs,
currency_pair_conventions, currencies (ores.refdata.api) and images
(ores.assets.api). All decoding goes through `ores::nats` `wire_codec` and
`request_helpers`; `rfl::json` appears only for local window state.

**C-3. Live ticks for every curve family subscribe to a subject nothing publishes. (high, S)**
Hedgr: `tick_stream.cpp:44-57`, `tick_uri_for_series` replaces `type=series`
with `type=quote` and its own comment admits "a composite series that names no
coordinate cannot be watched as one exact quote".
Server: the datum URI is "the series' URI with `type=quote` and the coordinate
fields of this observation"
(`ores/projects/ores.marketdata/api/include/ores.marketdata.api/messaging/market_observation.hpp:82`);
`field_role::coordinate` makes `term`, `maturity`, `contract_month`
coordinates (`ores/.../ores.marketdata.api/datum/schema.hpp:121,301-330`);
`tick_plan.cpp:33-38` canonicalises through `oresmd_uri_codec::read` and
`write` before publishing.
Failure: for ir_swap, mm, fra, zero, discount, basis and futures series the
subject's last token never matches; the subscription succeeds and receives
nothing, with no error, forever. Only coordinate-free families (fx_spot, the
family the plan's live check used) work. This is why term structures still
poll.
Reuse: `market_tick_subject_all(tenant, party)` plus `datum::series_of`
filtering, which is exactly what `ores.shell` does
(`ores/projects/ores.shell/application/src/app/commands/marketdata_commands.cpp:384-413`);
`series_of`, `datum_at`, `datum_at_point` are in
`ores/.../ores.marketdata.api/datum/market_datum.hpp:137-157`, a library
Hedgr already links. One party-wide subscription per session also removes the
per-series subscription thread of AR-22 and the 512-watch cap.

**C-4. History backfill returns a random sample once a series has more than 2,000 observations. (high, S)**
Hedgr: `session_client.cpp:1325-1375` sends `list_market_observations_request`
with a `series_id` filter and no order, fetches `offset = total - limit` when
`total > limit`, then sorts client-side; the comment at `:40-44` assumes the
default is "oldest first". Runtime limit is 2,000 (`instance_manager.cpp:3568`).
Server: the generic list orders by `id` (`ores/projects/ores.marketdata/core/src/repository/market_observation_repository.cpp:356`),
`is_sortable` admits no field (`:79-82`), and per the audit observation ids are
random v4 UUIDs (`ores/projects/ores.marketdata/service/src/app/feed_ingest_loop.cpp:259,297`).
Failure: a 1 Hz series passes 2,000 points in 33 minutes; from then on the
"latest window" is 2,000 random points from the whole history, sorted after
the fact. The chart is silently wrong and the merge of AR-9 interleaves them
with live buckets.
Reuse: `list_by_series_id_market_observations_request`
(`ores/.../market_observation_protocol.hpp:216-235`,
`"marketdata.v1.market_observations.list_by_series_id"`), whose default order
is `observation_datetime` descending (`repository.cpp:292`), wrapped by
`ores::marketdata::client::market_data_client::list_observations_page`
(`ores/projects/ores.marketdata/client/include/ores.marketdata.client/market_data_client.hpp`).
brave_hopper adds `observation_datetime_from/to` for windowed reads.

**C-5. One process-wide wire codec against per-instance wire formats. (high if formats are mixed, else med, M)**
Server client: `ores::nats::client::connect` calls
`set_default_wire_codec(opts.format)` (`ores/projects/ores.nats/src/service/client.cpp:485`)
into an unguarded global (`ores/projects/ores.nats/src/domain/wire_codec.cpp:26-38`),
documented as decided once per process.
Hedgr: `wire_format` is per instance (`hedgr.model/domain/instance.hpp:88-89`,
`connection_options.cpp:60`); many clients connect concurrently from pool
threads; `decode_tick` (`tick_stream.cpp:62`) and every
`request_and_decode` call use `default_wire_codec()`. `session_client.cpp:107-115`
acknowledges the problem and works around it only in `anonymous_read`.
Failure: two instances with different formats, the last connect wins, the
other's responses fail to decode and its ticks drop with one warning; the
global is written from several threads unsynchronised.
Fix: either Hedgr declares one wire format per process (and the per-instance
field goes), or the session_actor of AR-4 carries its own `wire_codec` and
passes it to every decode (the ORE helpers accept one).

**C-6. Refusals read as a broken server. (med, S)**
`anonymous_read` (`session_client.cpp:121-155`) goes through
`session.request()` (`:938-952`) and never calls
`ores::nats::service::throw_if_refused`
(`ores/projects/ores.nats/include/ores.nats/service/refusal.hpp`); no Hedgr
file references `throw_if_refused` or `request_refused_error`.
`session_error.cpp:90-106` maps `nats_connect_error` and `session_expired_error`
but not `request_refused_error`
(`ores/projects/ores.nats/include/ores.nats/service/request_refused_error.hpp`,
which carries codes such as `forbidden`), so a permissions refusal becomes
`protocol_error`, "the instance answered something this client could not
understand". Reuse: `request_and_decode` already checks refusals; map the
refused error to a `forbidden` sentence.

**C-7. ORE's time parser is enforced per translation unit, and one test TU misses it. (med, S)**
`ores.utility/rfl/reflectors.hpp` brings the partial specialisation of
`rfl::parsing::Parser` for `system_clock::time_point`
(`ores/projects/ores.utility/include/ores.utility/rfl/time_point_parser.hpp`).
`session_client.cpp:32` and `tick_stream.cpp:21` include it before the protocol
headers; `projects/hedgr.session/tests/market_tests.cpp` does not, and encodes
`market_tick` with `observation_time` at `:101,118`. The same specialisation is
therefore present in one TU and absent in another of the same binary, which is
an ODR violation whose outcome the linker decides. Fix: one Hedgr header
`hedgr.session/transport/protocol.hpp` that includes the reflectors and then the
protocol headers, included everywhere (ORE does this in
`ores/projects/ores.service/include/ores.service/messaging/handler_helpers.hpp`).

**C-8. URI grammar is hand-parsed with the coordinate semantics wrong. (high for curve grouping, M)**
`series_label.cpp:38-157` derives label, asset class, term and curve key by
splitting the URI string. `series_term` falls back to `tenor` and
`series_curve_key` strips both `tenor` and `term`, but in the schema `tenor` is
an identity field of `ir_swap`, `mm_future`, `oi_future` and `term` is an
identity field of `basis_swap`, `bma_swap` (coordinate `maturity`)
(`schema.hpp:310-330`). So USD 3M-index and 6M-index swap series collapse into
one curve and basis swaps group by the wrong field. It does no percent-decoding
and re-sorts parameters, so the key is not canonical.
Reuse: `oresmd_uri_codec::read`
(`ores/projects/ores.marketdata/core/include/ores.marketdata.core/datum/oresmd_uri_codec.hpp`)
and `datum::series_of` give identity versus coordinate fields by schema. This
is the model-level `series_uri` owner AR-14 asks for, and it should be ORE's
codec, not a Hedgr copy.

**C-9. Tick and observation value parsing differ from the server. (low to med, S)**
`tick_stream.cpp:68` uses `strtod` (locale-dependent, accepts trailing junk
and hex); `session_client.cpp:1390` uses `stod` without `isfinite`. The server
parses the same text with `ores::platform::numeric::parse_double`
(`ores/projects/ores.platform/include/ores.platform/numeric/floating_point.hpp`,
used in `tick_plan.cpp:41`), locale-independent and strict. `double` itself
AGREES with the server (`ores::utility::decimal` is for money; rates stay
double). Reuse: `parse_double` in both places (also closes AR-36).

**C-10. CRM rate formatting is a port that dropped tick snapping. (med, S)**
`crm_rate.hpp:44-64` ports
`ores::refdata::client::presentation::currency_pair_rate_formatter`
(`ores/projects/ores.refdata/client/include/ores.refdata.client/presentation/currency_pair_rate_formatter.hpp`,
header-only) but omits rounding a direct rate to `tick_size * pip_factor`
before formatting; Hedgr carries both fields and never uses them. The same
rate can display differently from other OreStudio surfaces. Reuse: include the
formatter.

**C-11. Connection options re-declare `nats_options` and drop the format. (med, S)**
`session_options.hpp:25-83` mirrors `ores::nats::config::nats_options` (url,
prefix, three TLS paths) plus a string wire format re-parsed at each use, a
`request_timeout` duplicating
`ores/projects/ores.nats/include/ores.nats/service/timeouts.hpp`, and a
`connect_timeout` that is never honoured. `application.cpp:122-129` copies
`nats_options` field by field and drops `.format`, so `--nats-wire-format` and
`ORES_NATS_WIRE_FORMAT` are parsed and ignored for the seeded instance. Reuse:
hold a `nats_options` directly.

**C-12. Reads re-implemented beside `market_data_client`. (med, folds into AR-4)**
`session_client` re-implements `list_series` and paged observation reads that
`ores::marketdata::client::market_data_client` provides
(`ores/projects/ores.marketdata/client/include/ores.marketdata.client/market_data_client.hpp`).
Login completion (`session_client.cpp:400-450,818-870`) duplicates
`ores.shell`'s `complete_login`
(`ores/projects/ores.shell/application/include/ores.shell/app/login_helpers.hpp`),
which is shell-internal and not linkable, so that one is justified until ORE
exposes it. Proactive token refresh has no interactive ORE counterpart
(`make_service_token_provider` is for service accounts), so it is justified.

**C-13. Justified divergences, recorded so nobody re-litigates them.**
Crypto (ADR 6): at HEAD `ores.security/crypto` has only `password_hasher.hpp`
and `ores.utility/crypto` only `sha256.hpp`; commit `9a00394350` deleted the
encryption surface, so the ADR's premise holds. `secret_box.cpp:101-160`
hand-rolls base64 over `EVP_EncodeBlock` where
`ores/projects/ores.utility/include/ores.utility/convert/base64_converter.hpp`
exists; Hedgr's is stricter about alphabet and padding; low.
`password_policy.hpp` mirrors the server response so the UI can show per-rule
results, which `ores.security::validation::password_validator` cannot drive;
justified. Logging and program options use `ores.logging`,
`nats_configuration` and `environment_mapper_factory::make_mapper("HEDGR")`
correctly; `HEDGR_MASTER_PASSWORD` via `getenv` and a hard-coded version string
instead of `ores.utility/version` are low. The `.env` reader
(`instance_manager.cpp:377-500`) has no ORE C++ equivalent; its keys match the
reference `.env`.

**C-14. Small agreements that depend on an undocumented server choice. (low)**
`parse_iso8601_utc` (`data_health.hpp:140-163`) matches the `T` form the CRM
handler writes (`ores/projects/ores.marketdata/service/include/ores.marketdata.service/messaging/crm_handler.hpp:65-69`),
but the documented wire shape uses a space and
`ores::platform::time::datetime::from_iso8601_utc`
(`ores/projects/ores.platform/include/ores.platform/time/datetime.hpp:73`)
accepts both. If the server is brought in line with its spec, Hedgr reads 0.
Reuse: `datetime::from_iso8601_utc` and `to_iso8601_utc` (also replaces
`iso8601_now` at `instance_manager.cpp:228` and AR-32's formatters).

Count: 15 items (C-0 to C-14), of which 4 are high (C-3, C-4, C-5 under mixed
formats, C-8), 7 medium, 4 low or justified.

---

## Part D. Target structure and landing order

### Target

```
hedgr.model
  domain/   value types; window_kind trait table; health thresholds; series_uri
  state/    tables (existing) + series_table (handles, contiguous rings,
            version stamps) + condition_table
  event/    std::variant event; command/ std::variant command; channel (double
            buffered, wakes the host)
  signal/   signal_bus as a type; no global

hedgr.session
  transport/ session_client (one connection, requests + subscriptions)
  market/    tick decode only (labelling moves to model or to ORE helpers)

hedgr.runtime
  actor/     session_actor: one thread, one connection per session; a command
             vector in, events out; subscriptions refcounted by series handle
  service/   catalogue_service (store + lock), session_service, probe_service,
             market_service, window_service, seeder (one-shot)
  manager/   instance_manager: owns tables + channels, pump() calls the
             services in a fixed order, nothing else

hedgr.ui    reads tables, pushes commands; per-window derived caches keyed on
            version stamps; no scheduling, no file I/O, no domain thresholds

hedgr.imgui frame loop with a wait + wake policy; wiring
```

### Order, each step landable and testable on its own

Step 0 comes before everything because each item is a correctness defect that
is small on its own and that every later step would otherwise build on.

0. **Correctness first, one PR each.**
   - C-3: subscribe to `market_tick_subject_all(tenant, party)` once per
     session and filter with `datum::series_of`; delete `tick_uri_for_series`.
     This also makes AR-22 true by construction (one subscription, one
     delivery thread) and removes the 512 cap. Test: a synthetic tick for an
     `ir_swap` series reaches the chart.
   - C-4: backfill through `list_by_series_id_market_observations_request`;
     delete the offset arithmetic. Test: a series with 3,000 observations
     backfills its newest 2,000.
   - AR-23: `market_stream::stop()` stops joining on the frame thread. Test:
     a session failing mid-connect does not block `pump()`.
   - AR-24: unlock coroutines stop touching `pending_master_password_` and
     `store_lock_`; the store write moves to the frame thread.
   - C-5: decide one wire format per process or carry a codec per actor;
     until decided, refuse to start two instances with different formats.
   - C-7: one `protocol.hpp` that includes the reflectors first, included by
     every TU that touches a protocol type, tests included.
   - C-6: map `request_refused_error` to a sentence.
1. **Delete next** (AR-5, AR-18, AR-19, the stale cadence in AR-20). Remove
   the placeholder tick and `session_data`, the 100 ms wakeup, the per-tick
   mutex, the chart's 2 s cadence. No behaviour an operator sees changes.
   Tests: existing runtime tests.
2. **Frame-rate policy** (AR-2). Wake on channel push and ring publish, wait
   otherwise, redraw predicate. Measurable: idle CPU before and after with the
   same desk. One PR in `hedgr.imgui` plus a hook in `channel` and `bucket_ring`.
3. **Runtime owns scheduling, UI stops writing** (AR-11, AR-12, AR-13).
   Move the deadline scan into `pump()`, make `focus_request` runtime-cleared,
   `render_input.images` const, bus an instance. Three small PRs.
4. **Watch set by event, not by frame** (AR-3). Cache the wanted set on
   `market_stream_entry`, index the catalogue. Test: watch set follows open and
   close (the plan already names this test).
5. **Event and command variants** (AR-10, AR-9's per-frame cap). Mechanical,
   compiler-driven; `apply()` becomes `std::visit`. Land as one PR per channel.
6. **`series_table` with handles and contiguous rings** (AR-7, AR-8, AR-1 part
   one). Model-only PR with tests for ring wrap, version bump, handle
   staleness; then a runtime PR switching `append_point` and the bucket to the
   handle; then a UI PR plotting through ImPlot `offset` with no copy and
   caching candles/EMA on the version stamp.
7. **Health, URI grammar and formatting to one owner** (AR-14, AR-32, AR-33,
   C-8, C-9, C-10, C-14). URI grammar comes from `oresmd_uri_codec` and
   `datum::series_of`, value parsing from `parse_double`, timestamps from
   `datetime::from_iso8601_utc`, CRM formatting from
   `currency_pair_rate_formatter`; health thresholds and typed clocks are the
   one Hedgr-owned addition, in model. Callers migrate and the UI and session
   copies are deleted in the same PR.
8. **`condition_table`** (AR-21). Model type plus tests; then the 101
   assignments become a `raise` call (a scripted rewrite, reviewed by diff);
   then the status bar reads it.
9. **Split `instance_manager`** (AR-16, AR-17). In this order, because each
   extraction only moves code: seeder (pure extraction, no table ownership
   change), probe_service (ADR 5 already isolates it), window_service plus the
   kind trait table, catalogue_service with the lock state machine,
   market_service. Session service last because step 10 changes it.
10. **`session_actor`** (AR-4, AR-6, C-5, C-11, C-12). One connection per
    session holding a `nats_options` and its own `wire_codec`; reads go
    through `market_data_client`; one party-wide tick subscription feeds a
    per-series dispatch on the actor's thread; subscriptions refcount by
    series handle across kinds; term structures derive from member rings.
    This is the largest step and depends on 6 and 9. Measurable: connections
    per session under a 100-window desk goes from "windows plus two" to one.
11. **Docs** (AR-20). Amend ADR 2 for the variant and the shared-object
    inventory, write an ADR for the actor and the frame policy, retire
    `ui-corrections.md`.
