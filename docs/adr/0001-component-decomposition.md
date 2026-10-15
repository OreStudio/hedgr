# 1. Component decomposition and folder layout

**Status.** Accepted.

## Context

Hedgr began as one component, `hedgr.imgui`, holding the GLFW window, the frame
loop, the NATS connection, the IAM login round trip and every widget. That was
correct for one connection to one instance.

The requirement is now many instances, each with identities and sessions, and
hundreds of floating windows over the top. A single component cannot carry that:
the state outlives any one connection, the network must not run on the frame
loop, and the layout must be testable without opening a window.

Hedgr also has to sit beside OreStudio and be read by people who know it, so
its layout follows OreStudio's component conventions rather than inventing a
private one.

## Decision

Hedgr is six components. Each is a **single-module component** in the shape
OreStudio uses for `ores.utility`: a CMake root that adds `src`, `tests` and
`modeling`, with `include/<component>/`, and no facet level.

```
projects/hedgr.model/     value types, live state tables, commands and events
projects/hedgr.store/     SQLite schema and repositories
projects/hedgr.session/   the blocking NATS transport
projects/hedgr.runtime/   executor, session workers, the instance manager
projects/hedgr.ui/        ImGui and ImPlot rendering
projects/hedgr.imgui/     the executable: GLFW window, frame loop, wiring
```

Dependencies run one way and only one way.

```
hedgr.model  ──┬──▶ hedgr.store ──┐
               ├──▶ hedgr.session ─┼──▶ hedgr.runtime ──┐
               └──▶ hedgr.ui ──────┘                    ├──▶ hedgr.imgui
                                                        │
                                   hedgr.ui ────────────┘
```

Inside a component the subfolders are facets, and the namespace is
`hedgr::<component>::<facet>`: `hedgr::model::state`, `hedgr::store::repository`,
`hedgr::session::transport`, `hedgr::runtime::async`, `hedgr::ui::render`.

Two consequences of the rule are load-bearing.

**`hedgr.model` depends on nothing else in Hedgr.** It holds no ImGui, no NATS
and no SQLite. Every other component can be tested against it with no daemon
running, which is what makes the layer worth having.

**`hedgr.ui` does not depend on `hedgr.runtime`.** The interface never calls the
runtime; it reads the state tables and pushes commands. That is why it needs a
command channel at all, and it is what keeps rendering a pure function of state.

## Consequences

An engineer adding a window kind touches three places and no others: a
`window_state` alternative in `hedgr.model`, a renderer in the table in
`hedgr.ui/src/render/renderer.cpp`, and an entry in the Instance Manager's
open-window action. The compiler enforces the first two.

Five shared libraries plus one executable is more CMake than the old single
component, and the payoff only arrives at the second and third components. The
scaffolding is generated rather than hand-written, so the cost is paid once:
`build/scripts/scaffold_component.py` writes an OreStudio-shaped component, and
`build/scripts/build-target.sh` builds one target without rebuilding the world.

`hedgr.imgui` keeps its name although it is now only the executable. Renaming
it would churn every build script and every path for no gain.

## Alternatives considered

**Keep one component and add folders.** Rejected. The state tables would still
be in the same library as ImGui, so no test of the state could link without a
GL context, and the network would still reach the frame loop.

**Two components, `hedgr.core` and `hedgr.imgui`.** Rejected. `core` would hold
the model, the store, the transport and the orchestration together, which is the
original problem with a new name: the store would drag in NATS, and the model
would drag in SQLite.

**A facet split inside each component, as OreStudio does for
`ores.scheduler`.** Rejected for now. Facets exist in OreStudio to separate an
`api` that other components link from a `service` that only a daemon runs.
Hedgr has no such split: each component has one consumer, and a facet level
would add a CMake file per component without changing a single dependency.

**Reuse `ores.qt`'s `ClientManager` shape.** Rejected. It is a single-connection
object that conflates connection lifecycle, identity state and a large typed
request surface. It does not compose to N connections, and its request methods
belong on per-domain facades instead.
