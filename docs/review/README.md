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
| 1 | Architecture document and review record | AR-20 (docs part) | merged (#20) |
| 2 | Session state corruption on failed or interrupted reads | SR-1, SR-3 | merged (#23) |
| 3 | UI memory safety and identity | UW-1, UR-14, UR-15, UR-4, UR-19, UR-20, UR-21 | merged (#21) |
| 4 | Secret handling and store robustness | MS-1, MS-2, MS-4, MS-12, MS-8, MS-9, MS-16, MS-17 | merged (#22) |
| 5 | Build hygiene and warnings | UW-2, UW-3, UW-8, UW-9, UW-5 | merged (#24) |
| 6 | Live tick path | SR-2, SR-4, SR-5, SR-6, C-3 | merged (#26) |
| 7 | Backfill through the time-ordered endpoint | SR-9, C-4, SR-10 | merged (#28) |
| 8 | Data health has one owner | UR-1, UR-2, UR-OS-2, AR-14 | merged (#31, #32, #33) |
| 9 | Reuse OreStudio libraries | MS-22, MS-23, UR-OS-1, C-8, C-9, C-10 | merged (#25), partly deferred |
| 10 | Frame loop idle and minimised policy | UW-4, AR-2 | merged (#27), minimise only |
| 11 | Per-frame allocation and clipping | UR-5, UR-6, UR-8, UR-9, UR-10, UR-13, MS-6, SR-11 | merged (#30), partly deferred |

Structural work that follows the bundles above, in the order Part D of
`architecture.md` gives: version-stamped series table (steps 1 and 2 built, see `docs/plans/series-table.md`: `market_table` is deleted, the bucket carries a handle and AR-8 and MS-7 are closed, AR-7 is closed for the writers, the point and the key and stays open for the chart's per-frame lookup and `chart_state` holding a handle, MS-5 and SR-11 wait for step 3), `instance_manager`
split (done, see `docs/plans/instance-manager-split.md`), session actor (steps 1 to 5 done, see `docs/plans/session-actor.md`: one connection per session, every read on it, liveness and reseed, and the per-read connection code deleted; step 6, supervision into the actor, is gated on a live check). Each needs its own plan and is not scheduled here.

## Deferred from merged bundles

- Bundle 6: SR-6, reconnect detection — **done** by step 3 of `docs/plans/session-actor.md`. `ores.nats` still exposes no connection state, so the detection is a liveness request (`session_link::ping`, the deployment's anonymous `bootstrap_status`) rather than an upstream accessor: the first failure raises a status-bar condition and the first success after it re-seeds every watched series. D6 of that plan still asks whether to press for the upstream state.
- Bundle 7: the deadline-policy half of SR-10, in `instance_manager.cpp`.
- Bundle 9: UR-OS-1 and C-10 (CRM rate formatting through `currency_pair_rate_formatter`) need an edit at `crm_window.cpp:337-344`. UR-OS-3 needs `in_term_order` moved into an ores api library. UR-OS-5, UR-OS-6, C-8 and C-9 need session URI parsing or the core codec. UR-OS-4 is done: the CRM matches a parsed `fx_spot` identity rather than URI substrings, so a forward or option sharing the currencies is not chosen.
- Bundle 10: no redraw predicate, which needs version stamps first. GLFW on Wayland never reports a minimised window.
- Bundle 3: done. Folder ids in `browser.cpp` now come from a fresh v7 uuid, so a rename-then-create cannot reuse a key, as the instance and account ids already did.
- Bundle 4: `domain::credential::password`, worker job tokens and `market_stream_options::token` are still `std::string`.
- Bundle 8: UR-OS-6 is done: `market_series_row` carries a `market_asset` parsed from the identity, and the curve view, the browser's IR branch and the CRM's FX check branch on it, with the display string as the legacy fallback. UR-OS-4 is done (see bundle 9). Typed clocks and the age and time formatting are not in the model. Charts keep the FX thresholds because they hold only a series id. A disconnected CRM cell is still drawn grey. A collapsed CRM window keeps reading because the runtime cannot see collapse. AR-11 is done for the CRM only.
- Bundle 11: UR-8 (term-structure ladder copy and sort), UR-10 (series browser filter and sort), SR-11 and SR-14 (per-frame save and watch work) need version stamps on the window and session tables or a transactional `save_all`.
- AR-5 and SR-12: the tick, `session_data`, the `latency_us` column and the `window_data` event are deleted, and the session worker waits on the token refresh deadline only (prerequisite A of `docs/plans/session-actor.md`). The workspace window's "Last round trip" row went with them. SR-12 is done: a kind with no read — workspace, accounts, notice, grid, instance manager — is saved with a zero cadence and parked at all four deadline moments, so the interface stops asking for a refresh whose only answer was an event naming the window and carrying nothing. The instance-manager window, which was built outside the traits table, now takes its cadence and deadline from the table too.
- C-5 (guard only, prerequisite B of `docs/plans/session-actor.md`): a connect is refused while another live session (connecting or authenticated) uses a different wire format, and a probe waits under the same rule, because both end in the ORE client's process-wide codec. The format in force is read from the instance rows, so editing a row while its session is live is not seen. The policy-read worker also connects and is not guarded. D2 is decided (one codec per actor, see `docs/plans/session-actor.md`), which took the per-read writes of the global out with the per-read clients; the guard stays while the session worker, the probe and the policy read still build clients, and goes when they do.
- SR-8: the thread crossing (#41), the double-submit guard and the stale-result drop (#42) are done. A set-password that a lock overtakes leaves the lock `locked` over a store with no master row, until the next unlock reads it as absent.
- Split step 5: the term structure and CRM reads mark their table row and not the `pending_reads` registry.
- Split step 6: a refresh that follows a failure in the same frame opens a stream that the same frame retires, pinned in `market_feed_tests.cpp`. The close in the disconnect and remove-account hooks is redundant with the pump's sweep of sessions that are not authenticated, so no test can tell it from the sweep.
- Split step 7: removing an instance or an account stops its worker in `stop_session`, and the removal tests cannot tell that stop from the sweep that retires the session's stream, so it is unpinned. The probe forget in an instance removal is likewise masked by the slot reset. A removal still writes the table before the store, so a refused delete leaves the table without a row the store holds until restart, unlike a save, which restores the previous row.
- Side finding: the X11 fallback fails to initialise the OpenGL loader without an EGL hint.
