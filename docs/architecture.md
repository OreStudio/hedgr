# Hedgr architecture

Hedgr is a real-time client for OreStudio. It shows a desk of constantly
updating windows (charts, matrices, term structures) over live market data, and
it must stay responsive when hundreds are open. This document states the
principles that follow from that goal. The ADRs in `docs/adr/` record each
decision and its alternatives. This document is the summary a new change is
checked against.

## The data path

```
NATS broker
   |  one connection per session, one tick subscription per session
   v
session worker thread        decode, aggregate into buckets
   |  bounded ring of closed buckets, events through the event channel
   v
frame thread: pump()         the only writer to the state tables
   |  tables keyed by handle, version-stamped
   v
frame thread: render()       reads tables, pushes commands, writes nothing
   |  command channel
   v
runtime                      decides what to read, watch and poll
```

Everything below is a rule about one arrow in that picture.

## Principles

### 1. One writer

The frame thread owns every state table. A worker builds an event and pushes it
onto the event channel. The frame thread drains the channel at the start of a
frame and applies the events. No other thread reads or writes a table.

The shared mutable objects in the process are the two channels, the bucket
ring, the signal bus and the sound queue. A new shared object needs an ADR.

### 2. Rendering is a pure function of state

`hedgr.ui` reads tables and pushes commands. It does not call the runtime,
schedule reads, open files, or change a table through a `const` accessor or a
non-`const` handle. Rendering the same state twice draws the same frame.

### 3. Work per frame is proportional to what changed

The goal is hundreds of live windows, so cost cannot scale with what is open.
A window rebuilds derived data (plot arrays, candles, sorted rows, label
strings) only when the version stamp of its source changes. A frame with no
change draws from caches, and an idle desk costs close to nothing. The frame
loop waits rather than spins when nothing is dirty and the window is hidden.

### 4. The producer never blocks

A producer that cannot hand data over drops it and counts the drop. The ring
drops the newest bucket. No queue between a worker and the frame thread is
unbounded without a stated cap. The frame thread never joins or waits on a
thread that can be inside a network call.

### 5. One connection and one subscription per session

A session owns one connection. Reads and the tick subscription share it. Each
window that watches a series adds a reference to that series, and the runtime
subscribes once however many windows watch it. Adding a window never adds a
connection.

### 6. One owner per concept

Subject building, series URI grammar, time parsing, rate formatting, data
health and staleness thresholds, and the in-flight request registry each have
exactly one owner. A second copy is a defect even when it is correct today.

### 7. Reuse OreStudio, do not reimplement it

Hedgr speaks the server's protocol types and uses the server's libraries
(`ores.utility`, `ores.nats`, `ores.logging`, `ores.platform` and the `*.api`
messaging headers) for anything they already provide. The one deliberate
exception is the credential cryptography in ADR 6. A new hand-rolled parser,
codec or formatter for something OreStudio owns needs a stated reason.

### 8. Data-oriented state

Tables are organised by how the frame loop reads them. Rows are addressed by a
generation-tagged handle, and a stale handle compares unequal to the new
occupant. Hot columns are contiguous and trivially copyable, so a plot can read
them without a copy. Strings stay in cold blocks.

### 9. Dependencies run one way

`hedgr.model` depends on nothing in Hedgr. `hedgr.ui` depends on `hedgr.model`
and never on `hedgr.runtime`. Public headers do not leak a dependency their
consumers do not use. ADR 1 has the graph.

### 10. Secrets are wiped, and errors reach the operator

A secret lives in a type that cleanses on destruction and on move. It is not
copied into a plain `std::string`, a log line or a queue that relocates it.
Every failure at a boundary is a typed `std::expected`, and each surfaces to
the window that caused it rather than to one shared string.

### 11. Fail soft at the boundary, loud inside

External input (database rows, wire payloads, saved window state) is parsed at
the boundary, and one bad item never discards the rest. Inside the program,
types are trusted and an impossible state is an assertion.

## How a change is checked

A change that touches a hot path states its per-frame cost. A change that adds
a thread, a shared object or a connection cites the principle it relies on. A
change that adds a protocol or format parser names the OreStudio header that
did not fit. Each of these is a review question, and each is answered in the PR
description.

## Where the code does not yet meet these principles

The review of 2026-10 (`docs/review/`) lists every known gap with a file and a
line. `docs/review/README.md` maps each finding to the pull request that closes
it. Larger structural work, such as splitting `instance_manager`, a
version-stamped series table and a session actor, is sequenced in Part D of
`docs/review/architecture.md`.
