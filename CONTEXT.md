# Hedgr context

Glossary only. Implementation decisions live in `docs/adr/`.

## Instance

A deployment of OreStudio that Hedgr can talk to. An instance is identified by
its NATS server address together with the subject prefix its services listen
on. It has a tier and a human label. An instance is configuration: it does not
change while Hedgr runs, and one Hedgr process may hold many.

## Tier

The safety classification of an instance: `production`, `staging`, or
`development`. Tier is the axis every destructive-action confirmation reads. It
is never inferred from the instance name.

## Identity

A principal that may be presented to an instance, such as a username. An
identity belongs to exactly one instance, because the same username on two
instances is two different principals. An identity holds no credential and no
token.

## Credential

A secret that authenticates an identity. Credentials are never persisted. They
exist in memory for the lifetime of a session only.

## Session

A live connection from one identity to one instance. A session is keyed by the
pair, so reconnecting an identity to an instance resumes the same session rather
than creating a second one. A session owns its transport, its bearer token, its
tenant and party selection, and its last error.

## Instance Manager

The window that owns instances, identities, and sessions. It is the root of the
interface and must be usable with no session connected. It replaces the login
dialog as the entry point.

## Window

A floating, movable panel that shows one thing for one session. Each window has
a stable id, a kind, and a piece of kind-specific state. Windows are opened from
the Instance Manager and closed independently. A window may outlive the session
it points at, in which case it renders the session-loss state rather than
closing.

## Window kind

The discriminator that selects a window's renderer and the shape of its state.
Adding a kind adds a variant alternative, a renderer entry, and a menu entry,
and changes nothing else.

## Handle

A stale-safe reference to a row in a state table, made of an index and a
generation counter. Handles replace pointers between state rows.
