# 7. sqlgen defines the schema, and the store lives in the checkout

**Status.** Accepted. Reverses the part of [ADR 3](0003-a-store-without-credentials.md)
that rejected sqlgen.

## Context

ADR 3 chose the raw SQLite C API over sqlgen, and gave two reasons: sqlgen
pushes its own error types through the API, and `hedgr.model`'s types are plain
aggregates without persistence markers. The second reason still holds and is
honoured below. The first is a boundary problem with a boundary answer.

The cost of that choice is now visible. Every table was written twice: once as a
`CREATE TABLE` statement in `schema.cpp`, and once as a column list in the
`SELECT`s and the positional `bind` calls of the repository that reads it.
Nothing kept the two in step. A column renamed in one place and not the other
compiled, ran, and failed at the first query with a message about a name the
author had already stopped thinking about.

sqlgen is 0.6.0 in this vcpkg checkout, as `sqlgen::sqlgen`, and OreStudio's
`ores.dq` uses it for the same job.

## Decision

**A table is a struct.** Each table is described once, in
`hedgr.store/repository/entity.hpp`, as an aggregate with a `tablename` and
sqlgen's `PrimaryKey`, `Unique` and `std::optional` markers. There is no
`schema` member: SQLite has no schemas, and the field would be a lie.

**The DDL is generated from those structs**, in `ddl.cpp`, and both `apply()` and
every repository's `sql()` return the same statement from the same list. A
column cannot be renamed in one without the other.

**Reads and writes go through sqlgen.** `read<std::vector<Entity>> | where(...) |
order_by(...)`, `insert`, `update`, `delete_from`, over the connection
`sqlite::database` owns. The positional `bind` calls and the `statement` class
are gone.

**sqlgen's error channel stops at the boundary.** `rfl::Result` is translated to
`std::expected` and `store_error` in `query_failed` and `database::from_sqlgen`,
so nothing outside `hedgr.store` sees an `rfl` type. This is the boundary
discipline ADR 3's first objection asked for; it is applied rather than avoided.

**Three tables keep their hand-written statement**, and `ddl.cpp` says why: a
cascade foreign key. sqlgen's `ForeignKey` writes `REFERENCES "t"("c")` and its
SQLite dialect has no cascade property, so `account`, `instance_label` and
`account_label` are written out. The two composite keys that come with them --
`UNIQUE(instance_id, principal)` and the junctions' primary keys -- are in the
same statements for the same reason: sqlgen describes uniqueness a column at a
time and these are pairs. A per-column unique would forbid what the store is
for, so it is not a substitute.

**An entity is not a domain type.** `hedgr.model` is unchanged and names no
sqlgen type. The translation is a mapper pair per repository, which is where
ADR 3's second objection is honoured rather than argued with.

**A column default the original tables carried is now the writer's job**, because
sqlgen cannot express one. `created_at` is the case that matters:
`instance_repository::save` keeps the stored stamp when the row exists and stamps
now when it does not, which is what the `DEFAULT (datetime('now'))` clause used
to do.

## Consequences

The `hedgr.store` component links sqlgen and reflect-cpp, and its public headers
name `sqlgen::PrimaryKey`, so a consumer of the component needs both.

`store_error::sqlite_code` is now almost always 0. sqlgen does not carry the
`SQLITE_*` result code, so a constraint is recognised from the first words of
SQLite's own message, once, in `from_sqlgen`. The closed set of codes a caller
switches on is unchanged; only the numeric code beside it is lost.

`hedgr.store` and `hedgr.runtime`'s tests and library no longer compile against
the deleted `statement` type. Nothing outside the component used it, and nothing
outside it used `database::handle()` either.

## The database's location

A development build writes its store to `<repo>/.cache/hedgr/hedgr.db`. The
source directory is baked in at configure time as a compile definition on the
executable, rather than found by walking up from it: the executable lives in
`build/output/<preset>/publish/bin`, so the walk is four levels of guessing that
a relocated package breaks.

An explicit `XDG_DATA_HOME` still wins, because a caller that sets it is saying
where the file goes. When the baked directory is not there -- a package on a
machine that never had the source tree -- the path falls back to
`$HOME/.local/share/hedgr/hedgr.db`.

## Seeding development environments

A store with no instances is seeded with one instance per OreStudio checkout
found under the directory baked in beside this repository, each carrying the
`development` label and sitting in a `Development` folder under the instances
root. The label is blue with severity `none`: the palette's informational entry,
and a severity of none because nothing about a development instance makes
removing it dangerous.

The seeding is idempotent twice over. It runs only on a store that holds no
instances, and every seeded id is derived from the checkout's label, so a second
run would replace a row with itself rather than add a twin.

A checkout whose `.env` is missing or unreadable is skipped with a warning on the
process log and never a failure: the store must come up even if the tree beside
it is half-built. A machine with no checkouts at all -- a deployed package --
falls back to the one instance the process's own NATS options describe, which is
the seed this class always had.
