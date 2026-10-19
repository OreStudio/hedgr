# Review of 2026-10

A whole-project review at commit `b5a2732`, in five parts. Each file lists
findings with a file and line, a failure scenario, a fix and a size. The IDs
below are the ones the files use, so a pull request names the findings it
closes.

| File | Scope | Prefix |
|---|---|---|
| `architecture.md` | Cross-cutting design, principles, OreStudio reuse, target structure | AR, C |
| `model-store.md` | `hedgr.model`, `hedgr.store` | MS |
| `session-runtime.md` | `hedgr.session`, `hedgr.runtime` | SR |
| `ui-render.md` | `hedgr.ui` render layer | UR |
| `ui-widgets-app.md` | `hedgr.ui` widgets, `hedgr.imgui`, build | UW |

The findings were checked by reading code. None was reproduced by running it,
so a fix PR starts by confirming the defect. The OreStudio citations name
`ores_dev_solid_dirac`. The release build uses `ores_dev_brave_hopper`, so a
fix re-checks the cited header there.

The principles the findings are measured against are in `docs/architecture.md`.

## Bundles

Each bundle is one pull request, merged before the next starts.

| # | Bundle | Findings | Status |
|---|---|---|---|
| 1 | Architecture document and review record | AR-20 (docs part) | in review |
| 2 | Session state corruption on failed or interrupted reads | SR-1, SR-3 | open |
| 3 | UI memory safety and identity | UW-1, UR-14, UR-15, UR-4, UR-19, UR-20, UR-21 | open |
| 4 | Secret handling and store robustness | MS-1, MS-2, MS-4, MS-12, MS-8, MS-9, MS-16, MS-17 | open |
| 5 | Build hygiene and warnings | UW-2, UW-3, UW-8, UW-9, UW-5 | in review (#24) |
| 6 | Live tick path | SR-2, SR-4, SR-5, SR-6, C-3 | in review (#26) |
| 7 | Backfill through the time-ordered endpoint | SR-9, C-4, SR-10 | open |
| 8 | Data health has one owner | UR-1, UR-2, UR-OS-2, AR-14 | open |
| 9 | Reuse OreStudio libraries | MS-22, MS-23, UR-OS-1, C-8, C-9, C-10 | in review (#25) |
| 10 | Frame loop idle and minimised policy | UW-4, AR-2 | open |
| 11 | Per-frame allocation and clipping | UR-5, UR-6, UR-8, UR-9, UR-10, UR-13, MS-6, SR-11 | open |

Structural work that follows the bundles above, in the order Part D of
`architecture.md` gives: version-stamped series table, `instance_manager`
split, session actor. Each needs its own plan and is not scheduled here.
