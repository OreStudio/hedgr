# 3. A local store that holds no credentials

**Status.** Accepted.

## Context

Hedgr needs to remember instances, identities and the window wall across
restarts. OreStudio had this before and removed it: the `ores.connections`
component, deleted on 2026-09-23, stored connections in SQLite through `sqlgen`
at `~/.local/share/ores.qt/connections.db`, and each connection carried
`encrypted_password`, an AES-256-GCM blob under a master password derived with
PBKDF2-SHA256 at 600,000 iterations.

That store fused two questions that have different lifetimes: *where does the
application point* and *who is the user*. The later TypeScript rewrite recorded
that fusion as the mistake that killed it, in its own decision record.

## Decision

The store keeps the first question and refuses the second.

`instance` holds where Hedgr points: name, tier, NATS address, subject prefix,
wire format, labels. `identity` holds who may be presented: a label and a
principal, scoped to one instance, because the same username on two instances is
two principals. Neither has a password column, and there is no master password,
no key derivation and no encrypted blob anywhere in the schema.

A credential is a `model::domain::credential`, it exists in memory between the
prompt and the connect call, and the runtime clears it as soon as the attempt
resolves. It is never written to the database, never logged, and never placed in
an event.

`window_spec` holds the window wall: a stable id, a kind, the session it belongs
to, its cadence and its state payload. The payload is serialised per kind with
reflect-cpp, with the kind travelling beside it in its own column, so no
`std::variant` is ever written to storage.

Persistence uses the raw SQLite C API, wrapped in move-only `database` and
`statement` classes whose every operation returns `std::expected`. This follows
`ores.orgmode`, the one component in OreStudio that talks to SQLite directly,
and it avoids adopting an ORM that imposes an exception-based error channel on
a codebase that uses `std::expected` at its boundaries.

## Consequences

Reconnecting after a restart costs one prompt for a password and nothing else.
That is a real cost to the operator and it is accepted deliberately: a local
SQLite file with a reversible password in it is a liability, and the window
between Hedgr starting and the first login is when that file is most exposed.

Opening the database is idempotent. `apply_schema` creates tables only when they
are absent, and the schema carries a `schema_version` row so a future change has
somewhere to hang.

Because the file is not a secret store, it can be inspected, copied and deleted
freely. Losing it loses configuration, not access.

## Alternatives considered

**`sqlgen`, as `ores.connections` used.** Rejected. It is available in this
vcpkg checkout at 0.6.0 and it is the prior art, but it maps entities through
its own macros and pushes its own error types through the API. `hedgr.model`
types are plain aggregates with no persistence markers, and they should stay
that way.

**`sqlitecpp` or `sqlite_orm`.** Rejected. Both are exception-first, so every
call site would need a `try`/`catch` to return to `std::expected`. That is
boundary discipline inverted.

**Persist an encrypted password under a master password, as before.** Rejected
for the lifetime of this design. It reintroduces key derivation, a master
password prompt at startup, and a file whose compromise is worth attacking. If
an operator later asks for convenience over this, it is one column and one
prompt away, and the decision is recorded here so the reversal is visible.

**Fold instances, identities and windows into one table.** Rejected. A window
belongs to a session, a session belongs to a pair of instance and identity, and
the cascade is what makes removing an instance remove everything under it.
