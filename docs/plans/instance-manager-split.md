# Splitting instance_manager

How `instance_manager` stops being one class that does eleven jobs, in steps
that each land on their own with the suite green.

## Why

`instance_manager.cpp` is 4,387 lines and its header is 849. It seeds a first
run, owns the catalogue, runs the store lock, supervises sessions, schedules
probes and reads, feeds market ticks and persists the window wall. The review
(`docs/review/architecture.md`, AR-16 and AR-17) found that the bug classes seen
so far share one cause. The jobs share one `apply()` switch, one `last_error_`
string written at 101 sites, and a set of tables that several jobs write.

The aim is a facade that routes commands and events and owns the channels, with
each job behind a narrow interface that takes the tables it needs by reference.
The aim is not a smaller line count. A job that is moved but still reaches back
into the manager has not been separated.

## What must not change

The public surface in section "Facade surface" below stays as it is, so the
application and every test keep working. Every test drives the manager through
commands and events and none reaches a private method, so a move that keeps the
accessors and the channels breaks no test by construction.

The rules in `docs/architecture.md` hold at every step. The frame thread stays
the only writer of the tables, and a service never gets a back-reference to the
manager.

## The jobs, measured

Line counts are method bodies at `origin/main` (`ceb93d3`). Free functions add
about 1,150 more.

| Job | Lines | Moves in |
|---|---|---|
| Facade and lifecycle | 730 | stays |
| First-run seeding | 393 | step 1 |
| Probe scheduling | 222 | step 2 |
| Password policy | 56 | step 2, with probes |
| Window controller | 336 | step 3 |
| Store lock and credentials | 238 | step 4 |
| Read dispatch and CRM scheduling | 503 | step 5 |
| Market feed | 162 | step 6 |
| Catalogue | 716 | step 7 |
| Session supervision | 257 | stays until the session actor |

## Coupling the split has to remove

These members are touched by more than one job. Each is a decision, not a
detail, and the plan names who owns it afterwards.

| Member | Today | After |
|---|---|---|
| `seed_options_` and `database_->path()` | read in five jobs at eleven sites | one `endpoint_for(instance)` on the facade |
| `sessions_` | owned by supervision, written by catalogue and the lock, read by windows, reads, CRM and the feed | stays with the facade. Services take `const session_table&` |
| `windows_` | owned by windows, cleared by catalogue and supervision | the window service exposes `unbind(session)`, and the others call it |
| `last_error_` | written everywhere | a condition raised through one call (prerequisite B) |
| `master_password_` and `store_lock_` | owned by the lock, read by supervision on every connect | the lock service exposes two read-only getters |
| `market_` | written by reads and by the feed | stays two writers. The read service writes the catalogue and points, the feed writes closed buckets |
| `pool_` | spawned on by the lock, supervision and reads | the facade owns the pool and hands out `io_context&` |

## Prerequisites

Each prerequisite is its own pull request and lands before the step that needs
it.

**A. Condition table (AR-21).** Replace the 101 `last_error_ =` assignments with
a `raise` call into a model `condition_table` that the status bar reads. Without
it every extracted service needs a `std::string&`, and the first service to
report two errors in a frame loses one. Needed before step 1.

**B. Store-lock thread crossing (AR-24, SR-8).** The unlock and set-password
coroutines write `pending_master_password_` and read `store_lock_` on a pool
thread, and `master_repo_->create` runs a store write on the pool. This breaks
the single-writer rule inside the class. Move the secret onto the event as a
`secret_string` and do the store write on the frame thread. Needed before
step 4. Doing it inside the extraction would double that diff and hide the
defect behind a class boundary.

**C. `session_read_for(handle)`.** A helper that replaces the ten copies of the
same twelve-line preamble in the read starters. It is a 150-line deletion in the
current class and needs no new type. Needed before steps 5 and 6.

Status: done. `session_read_for` returns the existing `worker::session_read`
and takes a `read_gate`, because the copies differ. Deployment, market,
backfill and image reads need a token. CRM, term structure and CRM names reads
start with an empty one. The currency image map checks the token and not the
session status. Nine copies became one helper, a net 70-line
deletion, not 150, because each starter still builds its own job. Tests pin each
starter against a signed-out, refused, dropped and token-less session.

## Steps

Each step is one pull request. It leaves the public surface unchanged and the
full suite green. Order is by risk and by what depends on what.

### 1. Seeder

Move `seed_first_run`, the `ensure_*` folder and label helpers, the account
seeding, the `.env` and icon discovery free functions, and
`backfill_seeded_icon_keys`. Shape it as a free function
`seed_store(tables&, repositories&, options) -> errors` called once from
`start()`. `ensure_instance_manager_window` stays with the window controller,
because it makes a window row, not a catalogue row. `ensure_accounts_root` is
shared with the catalogue and stays a free function on the folder table.

Tests that already cover it are `environment_seed_tests.cpp`,
`account_seed_tests.cpp` and `seed_store_tests.cpp`. Add one test first, a
refused seed write becomes a start failure.

Risk is low. It is a pure move, called once, on the frame thread.

### 2. Probe scheduler and password policy

Move `pump_probes`, `spawn_probe_for`, `apply_probe_result`, `forget_probe`,
the two probe commands and the cadence constants, with `probes_`, `probing_` and
`probe_intervals_`. `probes()` stays on the facade as a forward. Password policy
shares the probe pool, so the facade keeps the pool and passes `io_context&` to
both. ADR 5 already isolates probes.

Add tests first for a result arriving for a removed instance, `probe_all`
skipping an in-flight probe, and the signal raised when reachability changes.
None of the three exists.

Risk is low. Inbound calls are two commands, one event, `forget_probe` from the
catalogue and cancellation from `stop()`.

### 3. Window controller and the window-kind table

Move open, close, refresh and configure for windows, `rebind_restored_windows`,
`open_accounts_window`, `save_windows` and the load of the saved wall, with
`windows_`, `window_repo_` and the serial. Introduce the per-kind trait table
(AR-17) in the same step. Today the polling policy for a kind is spread over
four sites and they disagree, and the table makes them one.

`refresh` returns a typed `read_request{kind, window}` and the facade routes it
to the read service in step 5. Until step 5 the facade routes it to the existing
read starters. That keeps the window service from calling reads directly.

Add tests first for `rebind_restored_windows`, a restored window reopened by
command rebinding and starting its read, the cadence and parked deadline per
kind, and a `save_windows` failure reaching the status bar.

Risk is medium. It has the largest fan-in, because catalogue, supervision,
reads, CRM and the feed all touch `windows_`.

### 4. Store lock and credentials

Needs prerequisite B. Move the set-password, unlock and lock handlers, the
credential store and the two lock events, with `master_repo_`,
`master_password_`, `store_lock_`, `unlocking_` and `lock_error_`. The facade
keeps `store_lock()`, `is_unlocking()` and `lock_error()` as forwards, and the
service returns "this was a session's credential failure" so the facade routes
it to supervision.

Add tests first for a lock failure that names an account leaving the lock
alone, `set_master_password` refused when one exists, and `has_credential`
surviving an account edit.

Risk is medium. The code is small, but it is the one place that wrote manager
state from pool threads. The extraction must not move those writes into a new
class and call them encapsulated.

Status: done (#42). `lock_service` takes the pool, the two channels and the
account table by reference. Its `apply()` returns that the store unlocked and
any credential failure that names an account, and the facade acts on both. The
two deliberate behaviour changes landed as their own commits: SR-8b refuses an
unlock or set-password while one runs, and SR-8c drops a result that a lock
overtook. `start_stored_connect` stays in the facade with supervision.

### 5. Read service

Needs prerequisite C. Move every `start_*_read`, `start_market_backfill`, the
image and term-structure handlers, `start_crm_read`, `pump_crm_reads` and
`apply_read_answer`, with the in-flight sets and the market, image,
term-structure and CRM tables. Replace the five in-flight sets with one registry
keyed by a typed request id, which removes the leak class fixed in #23 by
construction. Catalogue and CRM scheduling go together, because the batch owns
its deadline and the `crm_rates` answer reschedules it.

Add tests first for a points answer filling the table and clearing its mark,
image deduplication on in-flight and failed keys, a term structure refused when
its member lists mismatch, the image-map retry after a failed map, and a
deployment snapshot that both clears its mark and lands on the session row. None
exists.

Risk is medium. It is the second largest group, but most of it is the repeated
preamble that prerequisite C removes.

Status: done. `read_service` takes the pool, the event channel and the instance,
account, session and window tables by reference and holds no manager. The facade
routes a window's `read_request` to `start()`, and forwards the table accessors.
The market table stays one object that the feed will also write, so the service
hands out a non-const `market()` for it until step 6. It landed as the tests
first, a pure move, and the registry as its own commit. `pending_reads` keys the
five in-flight sets by a typed `request_id` and clears the mark from the answer
event, so a handler cannot forget to. The registry also fixed one omission, that
`stop()` never cleared the deployment marks.

Two groups are not covered by a test. `start_market_backfill` and the feed's
call to the catalogue starter need a live feed, which is step 6's seam, so they
were converted by reading. The term structure and CRM reads keep their `reading`
flag on the table row and not in the registry, because their rows already say
they are running.

### 6. Market feed

Move `start_market_stream`, `stop_market_stream`, `pump_market_streams` and the
retired-stream list, with `market_streams_`. Supervision calls three hooks, open
on authentication, retoken or open on refresh, and close on failure, disconnect
and account removal. The feed asks the read service for a catalogue and a
backfill through two callbacks.

The feed has no manager-level tests today. Add them first, and they are the real
cost of the step. They are the watch set following open and closed charts,
session loss retiring the stream, a drain applying buckets to the market table,
a token refresh keeping the watches, and a stale attempt dropped by `apply`. They
need a test seam on `market_stream`, either a fake stream or an injectable
factory. Three of these are already named in `docs/plans/market-eventing.md`.

Risk is medium to high. The code moved is 162 lines, but the group has three
threads and no behavioural tests.

Status: done. The seam is a `worker::tick_feed` interface that `market_stream`
implements and a `tick_feed_factory` the manager takes as a defaulted second
constructor argument, so production builds a real stream and a test builds a
fake. Every call across it is the frame thread's once per frame. Ticks still go
through the stream's own sink on the NATS thread, so no virtual call is paid per
tick. `market_feed` takes the instance, session and window tables by const
reference, the market table by reference and the two reads as callbacks, and
holds no manager. Supervision calls `open`, `retoken_or_open` and `close`.
`pump()` returns the refused catalogue reads for the facade to raise. The tests
landed first against the unmoved code, and each was mutated and killed except
the stop in the disconnect and remove-account hooks, which the pump's sweep
makes unobservable within a frame. They also cover `start_market_backfill` and
the feed's catalogue call, which step 5 converted without tests. No defect was
found. One quirk is pinned, that a refresh following a failure in one frame
opens a stream that the same frame retires.

### 7. Catalogue

Move the fourteen catalogue handlers, `rehome_leaves`, `drop_target_shortcuts`
and the load, with the six tables and repositories. The two removals cascade
into probes, supervision, the feed and windows. The service returns the list of
removed ids and the facade fans out, which keeps the catalogue free of the other
services. It goes last among the movable groups because the cascade needs those
services to exist.

Add tests first for a refused `save_instance` restoring the previous row, a
refused `attach_label` removing the attachment, and `remove_account` of a
signed-in account stopping its session and unbinding its windows.

Risk is low for the code and medium for the diff, at 716 lines.

Status: done. `catalogue_service` owns the six tables and their repositories
and holds no manager and no other service. Its `apply()` takes one of the twelve
catalogue commands (the plan counted fourteen, which included `rehome_leaves`
and `drop_target_shortcuts`, both private helpers) and returns a
`catalogue_effect`, the refusals in the order the handler raised them and, for a
removal, a `removal` naming the instance, the account if one, and the removed
instance's probe handle. The facade's `complete_catalogue` raises the refusals
and then winds down what the removal left, which is the probe, the sessions and
their windows, the stream of a removed account and the saved wall. `load()`
returns the sentence for the table it could not read.

The tests landed first, each mutated against the unmoved code. They cover a
refused save putting the previous row back for all five tables, a refused attach,
the cascade of both removals into sessions, windows, labels, shortcuts and
folders, the re-homing of leaves in the table and in the store, the wall saved
after a removal, and every refused cascade write reaching the status bar. The
survivors are equivalences. The stop of a worker in the two removals and the
feed close are invisible because the pump's sweep retires the stream the same
frame. The probe forget is invisible because a new occupant of the slot resets
the probe row anyway. The label delete in an instance removal is covered by the
store's own cascade, and the empty-target guard in `drop_target_shortcuts` is
unreachable from a command.

One real defect was found and fixed in its own commit, ahead of the move. A
refused re-attach of a pair that was already attached removed it from the table
while the store still held it. The move changes two things on purpose. The
session loop of an instance removal now runs after its store writes and not
before them, which is invisible because the two touch different state. The icon
backfill now runs after the whole catalogue load and not between the instances
and the accounts, which differs only when a later table also fails to read.

## Where the split ended

`instance_manager.cpp` went from 4,387 lines to 1,021 and its header from 849 to
489. The facade is now the constructor, `start()`, `pump()` in a fixed order,
`handle()` and `apply()` as routers, session supervision (`do_connect_session`,
`start_session_for`, `start_stored_connect`, `do_disconnect_session`,
`stop_session` and the session events in `apply()`), `complete_open`,
`complete_catalogue`, `save_windows`, `stop()`, the accessors as forwards and the
channels. Eight services hold the rest, `catalogue_service`, `lock_service`,
`market_feed`, `password_policy_service`, `probe_service`, `read_service`,
`window_service` and the seeder, and none of them holds a reference to the
manager.

What remains is session supervision, 257 lines of method bodies plus the event
cases, and it stays until the session actor replaces it with one connection per
session. The final count is higher than the 200 lines the review hoped for and
close to the 1,100 this plan predicted. The `last_error_` string is gone, and
each service returns its refusals for the facade to raise, so a frame that
refuses twice keeps both conditions.

### Not a step. Session supervision

Supervision stays in the facade. The session actor (Part D step 10 of the
review) rewrites it to one connection per session, so extracting it now would
move it twice, and it would pull the master password into a class that then
depends on the lock. What can land now are the tests that survive the actor, the
stale-attempt filter and a token-refresh case.

The plan for the actor is `docs/plans/session-actor.md`. Its step 7 moves
supervision into a `session_service`.

## Where it ends

After step 7 the facade is the constructor, `start()`, `pump()` in a fixed order,
`handle()` and `apply()` as routers plus supervision, `stop()`, the accessors and
the channels. That is about 1,100 lines, not the 200 the review hoped for. The
rest follows from the session actor, which is its own plan and depends on this
split and on the version-stamped series table.

## Facade surface

The application uses `start`, `pump`, `stop`, `save_windows`, `commands()`,
`events()`, `store_lock()`, `is_unlocking()`, `lock_error()`, `startup_failure()`,
`last_error()` and the table accessors, including the non-const `images()`. Tests
add `open_accounts_window` and `is_started()`. All of these stay, as forwards
where the table moves.

## Splits that were considered and rejected

- **One service per read table.** Each would repeat the same preamble, the same
  spawn and the same in-flight mark. What differs is the job struct, so the
  registry is one, not four.
- **CRM scheduling with the window service.** It scans windows, but the deadline
  and the reading flag live on the batch, and two windows share one read.
- **One market service over reads and streams.** It would join a thread-owning
  stream pump to twelve one-shot starters. Two writers of the market table cost
  less than one service with two lifecycles.
- **Supervision first.** It is rewritten by the session actor.
- **A last error per service.** The status bar reads one place. The condition
  table is the fix and is prerequisite A.
- **A lifecycle class for `stop()`.** It would be a layer with one caller. Each
  service gets a `cancel()` and `stop()` calls them.
- **Moving the lock coroutines without fixing the thread crossing.** It hides
  the defect behind a class boundary.
- **Passing the manager to the window service.** The service needs a
  `const session_table&` and nothing else from the manager.

## How each step is checked

- The full build and suite pass, with no warnings under the project's flags.
- Each step lists the tests it adds first. Those land in the same pull request,
  before the move, and pass against the unmoved code, so the move is shown not to
  change behaviour.
- The diff for a step is a move. A reviewer can compare the removed and the
  added bodies and find the same text.
- The public surface list above compiles unchanged.

## Open decisions

- Whether `condition_table` raises by severity or by cause, which decides how the
  status bar collapses two errors in one frame.
- Whether the seeder keeps `.env` parsing or uses an `ores.utility` helper if one
  fits.
