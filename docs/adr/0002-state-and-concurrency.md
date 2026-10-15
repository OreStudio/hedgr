# 2. State shape and the concurrency model

**Status.** Accepted.

## Context

Two requirements meet here. The frame loop must walk every session and every
window on every frame, and there may be hundreds of windows. The network
round trips must not block that loop, and the transport underneath is
synchronous.

A survey of OreStudio's client components found that all of them are blocking,
that `boost::asio::thread_pool` appears nowhere in the tree, and that
`ores.nats::service::client::request()` is declared as an awaitable but
implemented by detaching a `std::thread` per request and bouncing the result
through a channel. There is no async client to build on.

## Decision

**Data-oriented state.** State lives in tables organised by how the frame loop
reads it, not by domain taxonomy.

- `instance_table` and `identity_table` are cold: tens of rows, read when the
  Instance Manager is drawn, never touched by the per-window path. They are
  arrays of whole values keyed by the persisted id. Splitting them into columns
  would buy nothing.
- `session_table` and `window_table` are hot. Each is a structure of arrays,
  split into a hot block the frame loop reads every frame and a cold block read
  only when a row survives the cull. A token, a tenant name and a window's
  `std::variant` state sit in the cold block, so culling a window never touches
  them.
- Rows are addressed by a **generation-tagged handle**, not a pointer. Releasing
  a row bumps its generation, so a stale handle compares unequal to the new
  occupant instead of silently addressing it.
- Dispatch is a `constexpr std::array` of function pointers indexed by
  `window_kind`, so no virtual call and no branch sits on the per-window path,
  and adding a kind without a renderer is a compile error.

**One writer.** The frame loop thread owns the state tables outright. No other
thread may touch them.

**Blocking transport, coroutine orchestration.** `hedgr.session` is a thin
blocking wrapper, because that is what the transport is. `hedgr.runtime` owns a
`boost::asio::io_context` run by a fixed pool of `std::jthread`s. Each session
runs as a coroutine spawned on that pool: it connects, authenticates, then loops
on a `steady_timer`, and each blocking round trip happens on a pool thread,
which is what the pool is for. Sleeping and backoff are `co_await` on the timer,
never a blocking sleep.

**Two channels.** A worker never writes state. It builds a `model::event::event`
and pushes it onto the event channel. The frame loop drains that channel at the
start of a frame and applies the events. The interface does the symmetric thing
with commands. Each channel is a `std::mutex` guarded by a swap, so the only
shared mutable object in the process is a buffer whose lock is held for the
duration of a `std::vector` swap.

## Consequences

The state tables, the handles and the reducers are testable with no thread and
no daemon, and they are: `hedgr.model` covers handle reuse, stale-handle
rejection, and the hot columns staying stride-1 across holes.

Concurrency is bounded by the pool size rather than by the number of sessions.
Fifty sessions on a four-thread pool means four round trips in flight. That is
the honest consequence of a synchronous transport, and it is stated rather than
hidden. Raising the pool size raises the in-flight count.

Because a message carries a fixed event struct rather than a variant, a data
event allocates a string for its payload and nothing else. Events are occasional,
so this is not on the per-frame path.

## Alternatives considered

**Use `ores.nats`'s awaitable `request()`.** Rejected. It detaches a thread per
in-flight request and leaks on shutdown. Adopting it would spend a thread per
request and give nothing back.

**Qt's model: `QtConcurrent::run` plus a future watcher.** Rejected. It is the
same idea with a Qt dependency attached, and Hedgr's model layer is deliberately
Qt-free.

**One thread per session.** Rejected. A hundred sessions would mean a hundred
threads, most of them blocked on a socket, and no bound on the process's thread
count.

**A lock per table, written from workers.** Rejected. The frame loop reads hot
columns every frame; a mutex on that path costs more than it saves. A single
writer that drains a channel is cheaper and makes the ownership obvious.

**A dense array with a slot map instead of a sparse slab.** Deferred. A sparse
slab costs one predictable branch per row on the aliveness byte and keeps
handles stable across removals. Compacting live rows would remove that branch
and would let a window's handle change underneath the interface, which is worse.
