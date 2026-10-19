# hedgr.ui render review

Scope: projects/hedgr.ui/src/render, include/hedgr.ui/render, tests, plus
src/widget/browser.cpp where the render windows depend on it. Paths are
relative to /home/marco/Development/Hedgr/hedgr/projects/hedgr.ui unless
stated otherwise. crm_window.cpp line numbers are from HEAD (the working tree
has uncommitted WIP that shifts them by about 17 lines after line 140).

Every finding was checked against the code. Findings UR-18 to UR-40 came from
a delegated pass over the browser windows. I re-read the high-severity ones
(UR-18 to UR-21) myself before including them.

## Market data, health and the frame loop

### UR-1 (high) src/render/chart_window.cpp:283-287
- Defect: chart health is computed from the newest point of the level the
  period selects, not from the series' last arrival.
  `market_series_snapshot::observed_at_ns` (hedgr.model/.../domain/market.hpp:189)
  holds that arrival and the chart never reads it.
- Failure: `bucket_aggregator` (hedgr.model/src/state/bucket_aggregator.cpp:41-62)
  publishes a bucket only when the next bucket's first tick arrives, stamped
  with the last tick inside it. With period 1D the chart reads the medium
  (1 min) level, so the newest point is 0 to 60+ s old. Against the FX
  thresholds (live 5 s, delayed 30 s) the footer reads STALE about half of
  every minute. With period All it reads the coarse (1 h) level and is STALE
  almost always.
  - The pulse dot and the price tint follow the same wrong state.
  - The same series in two windows shows LIVE in one and STALE in the other,
    depending only on the period combo.
  - The plan (docs/plans/market-eventing.md, Interface) says the header shows
    the age of the last arrival.
- Fix: judge health from the newest point across levels, `max(fine.back,
  medium.back, coarse.back)`, or from a wall-clock last-tick stamp on the
  snapshot. Keep the level only for plotting.
- Size: S

### UR-2 (high) src/render/crm_window.cpp:140-146 (HEAD)
- Defect: the CRM window runs its own 5-second poll from render, on
  `ImGui::GetTime()`. It pushes `refresh_window` from the draw path, while the
  runtime deliberately disarms the CRM cadence (`do_refresh_window` sets
  `next_refresh_ns` to max for `crm_matrix`, hedgr.runtime/src/manager/instance_manager.cpp:3056-3060).
- Failure: the poll is a hidden scheduler inside render, so it depends on
  whether the body is drawn.
  - A collapsed or clipped CRM window (culled at workspace.cpp:399) stops
    polling. Uncollapsing it gives a burst refresh.
  - Ten CRM windows on the same party poll ten times every 5 s with no
    coordination.
  - It contradicts the runtime's "must not rearm a poll" rule and ADR 1's
    "rendering is a pure function of state".
- Fix: give `crm_matrix` a cadence in the runtime (`spec.cadence`) and let the
  existing `next_refresh_column` path in workspace.cpp:291 drive it. Delete
  `crm_next_poll` from `window_ui_state`.
- Size: S

### UR-3 (med) src/render/crm_window.cpp:159-173 vs 193-207 (HEAD)
- Defect: the fresh/stale/unavailable summary and the matrix dedupe duplicate
  (base, quote) cells by different rules. The summary inserts a pair into every
  status set it appears in. The matrix keeps one cell per pair and prefers
  non-unavailable.
- Failure: a CRM whose batch holds both a direct-unavailable and a
  reciprocal-fresh row for EUR/USD counts EUR/USD as fresh and as unavailable,
  and turns the summary amber. The matrix shows a fresh rate for that cell.
  The headline disagrees with the grid under it.
- Fix: build the `cells` map once, then count statuses from its values.
- Size: S

### UR-4 (med) src/render/crm_window.cpp:350 (HEAD)
- Defect: each rate cell is a `Selectable(rate.c_str())`, so its ImGui id is
  the formatted rate. ImGui tables do not push a per-cell id
  (`TableBeginCell` in imgui_tables.cpp does no PushID).
- Failure: two pairs whose rates format identically collide. A 20-currency
  matrix holds 380 four-decimal rates, so "1.0000"-style or close-cross
  duplicates are likely. Effects:
  - clicking one highlights or activates the other;
  - a double-click can open the wrong pair's chart;
  - ImGui 1.91's id-conflict debug highlights fire.
- Fix: `ImGui::PushID(base)`/`PushID(quote)` around the cell, or label it
  `rate##base/quote`.
- Size: S

### UR-5 (med) src/render/crm_window.cpp:159-242 and 343 (HEAD)
- Defect: every frame, each CRM window rebuilds three
  `std::set<pair<string,string>>`, a `std::map` of cells, a `std::map` of
  conventions built with two `substr` allocations each, a copy of the
  currency set, and a `pair_text` and `std::format` string per cell. It also
  calls `request_flags` with a freshly concatenated `"FX "+base+"/"+quote` for
  every pair.
- Failure: a 20x20 matrix costs on the order of 2,000 heap allocations per
  frame per CRM window while the snapshot has not changed. With several
  matrices on a wall this dominates the frame. The data only changes when a
  read lands, every 5 s at most.
- Fix: cache the derived matrix (sorted currencies, cell pointers, formatted
  rates, counts) in `window_ui_state`, keyed on the snapshot's identity or a
  generation counter that `crm_table` bumps on apply. Request flags once per
  currency, not per pair.
- Size: M

### UR-6 (med) src/render/chart_window.cpp:376-401
- Defect: every frame, each chart allocates and fills `xs`, `ys`, the candle
  vector and the EMA vector over the whole visible window. At the fine level
  that is up to 7,200 points per series (plan, Retention). It also walks the
  deque linearly in `live_window_start` (158-168), where the deque is sorted
  and `lower_bound` would do.
- Failure: 20 charts on the 1H period means about 4 x 7,200 doubles allocated
  and recomputed per chart per frame (an EMA pass included), for data that
  changes at most once per second. That is several MB of allocator churn per
  frame on the path ADR 2 is written to keep cheap.
- Fix: keep the plot arrays in `window_ui_state`. Rebuild them only when the
  newest `at_ns`, the size of the chosen level, or the span/style/EMA choice
  changes. Use `std::lower_bound` for the start.
- Size: M

### UR-7 (med) src/render/term_structure_window.cpp:114-131, 180
- Defect: the "primary" curve is the one with the most nodes, with ties going
  to the later one. The history overlay counts back from primary, and health
  is judged on primary's `at_ns`.
- Failure: when the newest curve has fewer nodes than an older one (a partial
  publish, or one tenor missing), these follow:
  - the newest curve is never drawn, because `end = primary + 1`;
  - the "latest" highlight goes to an older curve;
  - health reads the older stamp and can show STALE while fresh data sits in
    the table.
- Fix: the newest curve is `curves.back()`, assuming the table orders them by
  time. Build the x ladder as the union of tenors across the shown curves.
- Size: S

### UR-8 (med) src/render/term_structure_window.cpp:183-245
- Defect: every frame copies the primary curve's node vector (strings
  included) and sorts it with `tenor_rank` parsing both strings on every
  comparison. It also builds a `std::map<std::string,size_t>`, allocates one
  NaN-filled `ys` vector per overlaid curve, and runs a `std::format` per
  curve for the plot id.
- Failure: the cost is per window per frame for data that changes on a read.
  Ten curve windows with 10 overlays each run 100 vector allocations plus map
  churn every frame.
- Fix: cache the sorted ladder and the per-curve `ys` in `window_ui_state`,
  keyed on the term-structure snapshot. Use `PushID(c)` and a constant label.
- Size: M

### UR-9 (med) src/render/series_browser.cpp:650-675, 610-627
- Defect: the series table and combo submit every catalogue row each frame,
  with no `ImGuiListClipper`. Each row runs `request_flags`,
  `currencies_of(label)`, two `image_texture` lookups and `draw_series_row`,
  which allocates `std::string{label}` just to call `AddText`.
- Failure:
  - A tenant with a few thousand series costs O(n) work and n allocations per
    frame per browser window, even when 20 rows are visible.
  - `image_texture` -> `textures->get` "marks the texture as used by this
    frame" (renderer.hpp:376). Every flag in the catalogue stays hot, so the
    texture cache cannot evict anything while a browser is open.
- Fix: wrap both loops in `ImGuiListClipper`. Pass `label.data()` and
  `label.data()+size()` to `AddText`.
- Size: S

### UR-10 (med) src/render/series_browser.cpp:305-365, 459-475, 561-590
- Defect: every frame re-parses the session's serialised party list
  (`parties_of`), rebuilds the asset-class list with an O(rows x classes)
  `std::find` and string copies, re-filters the whole catalogue with four
  case-insensitive substring scans per row, and re-sorts it with `tenor_rank`
  parsing.
- Failure: the cost is O(n log n) and allocations per frame per browser, for
  a catalogue that changes only on refresh.
- Fix: cache the filtered and sorted view in `window_ui_state`, keyed on
  (catalogue generation, filter text, asset class, party, synthetic flag).
- Size: M

### UR-11 (med) src/render/crm_window.cpp:65-66, chart_window.cpp:274-278 and 363-365, term_structure_window.cpp:171-172, series_browser.cpp:254-259
- Defect: the four market-data windows each render a lost session
  differently.
  - CRM says "This window is not a CRM."
  - Chart says "Reading the series..." forever.
  - Term structure says "Sign in to read this curve."
  - The series browser says "This window is not signed in...".
- Failure: an operator whose session dropped sees a chart that claims to be
  loading and a CRM that claims to be the wrong kind of window. CONTEXT.md
  (Window) requires a window to render the session-loss state.
- Fix: one shared `session_gate(input, handle) -> optional<session_handle>`
  in panels.cpp that draws the session-loss message and the NO DATA footer.
  Every market window calls it first.
- Size: S

### UR-12 (med) src/render/chart_window.cpp, crm_window.cpp, term_structure_window.cpp, series_browser.cpp (structure)
- Defect: the market windows each re-implement the same scaffold:
  - PushID;
  - `foot.identity = identity_text(identity_of(...))`;
  - a variant `get_if` with a "not a X" fallback;
  - session lookup;
  - the health assessment, then `data_health_word`, `health_colour` and
    `as_of_text` into the foot;
  - a manual `draw_status_foot` plus PopID on each of 3 to 5 early-return
    paths.

  The "delayed" amber is spelled as
  `readiness_colour(readiness::bootstrapping)` at crm:175, crm:334,
  chart:250 and market_status.hpp:40.
- Failure: the duplication has already drifted.
  - Thresholds: chart uses FX thresholds even for an IR or commodity series
    opened from the browser's All list (chart_window.cpp:287).
  - Health source: CRM health comes from a parsed server string, chart health
    from a point stamp, term-structure health from the curve stamp.
  - Status words: the CRM's per-cell "stale" amber does not come from
    `data_health` at all.
- Fix: a `market_frame` RAII helper that owns PushID, the foot and the final
  `draw_status_foot`/PopID. A `foot_from_health(health, as_of_ns)` helper. A
  `theme::health_delayed_colour`. Thresholds chosen by the series'
  `asset_class`.
- Size: M

### UR-13 (med) src/render/workspace.cpp:298-346, 430
- Defect: for every open window, every frame, before the culling `Begin`
  check, the walk builds `window_title(...)` (a `std::string` copy of the
  label), a `std::format` begin label, and a `std::string` key for the
  `ui_state_` lookup. Every market window body then builds `identity_of`
  (four strings plus an `instances.find`).
- Failure: window ids are long ("inst:...", uuid-like), so each frame pays at
  least three heap allocations per open window, collapsed ones included. With
  the stated hundreds of windows that is about 1,000 allocations per frame
  before any content is drawn. The culling comment at workspace.cpp:401-404
  claims the opposite.
- Fix: key `ui_state_` by window index plus generation (a vector parallel to
  the window table, or a transparent-hash map with `string_view` lookup).
  Cache the begin label per window and rebuild it when the title changes.
  Compute `identity_of` only when the footer is visible.
- Size: M

### UR-14 (med) src/render/workspace.cpp:599-601, 686-689
- Defect: `draw_startup_failure` and `draw_lock_prompt` return without
  `ImGui::End()` when `Begin` returns false. ImGui requires `End` regardless.
- Failure: if either window is ever skipped (hidden for its first auto-fit
  frame, or `SkipItems`), ImGui hits the "Missing End()" assert at the end of
  the frame, or leaves the window stack corrupt in release.
- Fix: call `ImGui::End()` before the return in both places.
- Size: S

### UR-15 (med) src/render/workspace.cpp:599-628
- Defect: the startup notice passes `&startup_notice_open_` to `Begin`, but
  nothing reads it before calling `Begin` again. It is also forced back to
  true on `IsWindowAppearing`.
- Failure: the title-bar close button is drawn and does nothing. The notice
  cannot be dismissed, which reads as a broken control.
- Fix: return early when `!startup_notice_open_`. Or pass `nullptr` so no
  close button is drawn, if the notice is meant to be permanent.
- Size: S

### UR-16 (low) src/render/series_browser.cpp:391-393
- Defect: `ImGui::OpenPopup("Choose CRM")` is called every frame while
  `crm_picker_open` is true.
- Failure: a click outside the popup closes it inside ImGui, and the next
  frame's `OpenPopup` reopens it. The picker cannot be dismissed by clicking
  away; only Cancel closes it.
- Fix: call `OpenPopup` once on the button click and drop `crm_picker_open`
  as a per-frame driver.
- Size: S

### UR-17 (low) src/render/series_browser.cpp:189-219, 497, 541
- Defect: `group_curves` makes one group per IR series, so `members.size()`
  is always 1.
- Failure: the combo shows "1 tenors" and the Tenors column shows 1 for every
  curve. The curves are also sorted by the series' index tenor (always 1D
  per the comment), so the tenor sort is a no-op.
- Fix: drop the tenor count and the tenor sort for curves, or show the
  ladder size from the term-structure table once it is read.
- Size: S

## Browser windows (Instances, Accounts) and helpers

### UR-18 (high) src/widget/browser.cpp:228-234, 732-733, 744
- Defect: `drag_payload_data` holds a `std::string`, and
  `ImGui::SetDragDropPayload` memcpy's the struct. The local is destroyed at
  the end of the source block, so the copy holds a dangling pointer, into the
  dead stack SSO buffer or freed heap.
- Failure: on drop, `accept_drop` reads `.id` from freed memory. Every folder
  drag in both trees is UB. In practice it gives a garbage id in `save_folder`
  or a crash.
- Fix: make the payload trivially copyable, using a fixed `char id[N]` or the
  folder's index. Add `static_assert(std::is_trivially_copyable_v<...>)`.
- Size: S

### UR-19 (high) src/render/instances_window.cpp:368
- Defect: a new instance's id is `"inst:" + name`, and the runtime upserts by
  id (hedgr.runtime/src/manager/instance_manager.cpp:1665).
- Failure:
  - Creating "foo" while "foo" exists overwrites that row: its folder, icon
    and labels are replaced by the form's.
  - Renaming "foo" to "bar" keeps id "inst:foo", so a later new "foo"
    silently replaces "bar".
- Fix: refuse in the form when the id or the name already exists, and guard in
  the runtime's create path. Better, generate ids independently of names.
- Size: S

### UR-20 (high) src/render/accounts_window.cpp:160
- Defect: account ids are `acct:{instance}:{name}`, with the same upsert
  collision as UR-19.
- Failure: rename account A, then create an account with A's old name. A's
  principal and folder are replaced and its labels detached, while
  `has_credential` stays. The new principal inherits A's credential flag.
- Fix: same as UR-19.
- Size: S

### UR-21 (high) src/render/accounts_window.cpp:150-151 and 306-307
- Defect: duplicating appends " copy" twice. `open_edit_form` writes
  "{name} copy" into the draft, and `save_account` appends " copy" again when
  `form_duplicating` is set.
- Failure: Copy "ops", then Save, stores "ops copy copy" under id
  "acct:i:ops copy copy". The form showed "ops copy".
- Fix: drop the append in `save_account`.
- Size: S

### UR-22 (med) src/render/accounts_window.cpp:424
- Defect: `draw_remove_form` only handles `confirm_remove_leaf`, while the
  browser sets `confirm_remove_folder` for an account folder
  (browser.cpp:457-458).
- Failure: Remove on an account sub-folder does nothing and leaves the flag
  stuck. Account folders can be created but never removed.
- Fix: handle `confirm_remove_folder` by pushing `remove_folder`, as the
  Instances form does. Better, move the remove dialog into the browser.
- Size: S

### UR-23 (med) src/render/accounts_window.cpp:920-926
- Defect: the edit and duplicate forms refill from the store whenever
  `draft_name` is empty, on every frame.
- Failure: an operator who clears the name field to retype it has the name,
  principal and labels reset from the stored row on the next frame.
- Fix: an explicit `pending_fill` flag, as Instances has, set by the
  edit/duplicate lambdas and cleared after one fill.
- Size: S

### UR-24 (med) src/render/accounts_window.cpp:860-861
- Defect: an empty `selected_folder` is forced to the root every frame, but
  the browser clears it on a leaf click.
- Failure: with an account selected, the root is selected too. "Add" and
  "Folder" then target the root rather than the account's folder, and both
  rows highlight.
- Fix: default to the root only when both the leaf and the folder are empty.
- Size: S

### UR-25 (med) src/render/instances_window.cpp:1054
- Defect: `draw_dashboard` reads `std::chrono::system_clock::now()` in render,
  although `render_input::wall_now_ns` exists for this purpose.
- Failure: the strip is not a function of `render_input`. Captures and tests
  cannot pin the x-axis, and the two clocks are sampled at different instants.
- Fix: use `input.wall_now_ns`.
- Size: S

### UR-26 (med) src/render/instances_window.cpp:111-126, accounts_window.cpp:79, command_centre.cpp:86
- Defect: capture requests live in file-scope mutable globals that render
  consumes.
- Failure: with two Accounts or Command-centre windows open, whichever renders
  first takes the request. These are also hidden render inputs that no test
  can see.
- Fix: a `capture_requests` struct on `render_input`, keyed by window id and
  filled by hedgr.imgui/src/app/application.cpp.
- Size: M

### UR-27 (med) src/render/shortcut_bar.cpp:147, 164; workspace.cpp:423-427
- Defect: render raises signals on the global `signal::bus()` singleton. That
  is a side channel beside `input.commands`.
- Failure: hover, activation and window close emit global events from inside
  the draw, so render cannot be replayed or tested in isolation. ADR 1 says
  the ui only reads tables and pushes commands.
- Fix: return these as results (`shortcut_bar_result`, a closed-window list)
  and let the frame owner raise the signals after the walk.
- Size: S

### UR-28 (med) src/render/instances_window.cpp:167-226, accounts_window.cpp:82-103, window_forms.hpp `tree_folders`
- Defect: every frame rebuilds the full leaf and folder vectors with one
  `std::format` per row for `search_text`.
  - `status_of` repeats `instances.find(row.id)` for a row it already holds
    and copies the probe `detail` string per instance.
  - Each Accounts window scans every account of every instance.
- Failure: O(W x total accounts) scans plus several allocations per row per
  frame, with W the number of open Accounts windows.
- Fix: return a pointer to the probe detail, index the probe by handle, and
  cache `search_text` by table version.
- Size: M

### UR-29 (med) src/render/instances_window.cpp:1151-1172, 1388-1452; accounts_window.cpp:467-540
- Defect: dead code.
  - Both windows set `model.draw_details`, and browser.cpp:1365 then never
    calls `draw_list`, so `instance_columns`, `instance_row_cells`,
    `instance_row_labels` and the account equivalents never run in
    production.
  - `instance_columns()` is still allocated every frame and per row just to
    read `.size()` (1440, accounts 537).
- Failure: about 150 dead lines, plus a per-row allocation that is pure waste.
- Fix: delete them.
- Size: S

### UR-30 (med) src/render/instances_window.cpp:93-102, 129-148, 229-332
- Defect: Instances keeps private copies of `set_buffer`, `buffer_text`,
  `tree_folders`, `folder_move_allowed`, `labels_of`, `joined_labels`,
  `label_key_for` and `apply_labels`, which window_forms.hpp already provides
  and Accounts uses.
- Failure: the copies have drifted. The Instances `folder_move_allowed` lacks
  the same-tree and same-owner check, and its `tree_folders` lacks the owner
  filter.
- Fix: delete the copies and use `forms::`.
- Size: S

### UR-31 (low) src/render/instances_window.cpp:1254, 1282, 1368
- Defect: the same 10-line capture-element block is pasted three times, while
  `observe_item` (1479) already does it.
- Fix: call `observe_item`.
- Size: S

### UR-32 (low) src/render/window_forms.hpp `apply_labels`
- Defect: the typed label names are not deduped.
- Failure: "prod, prod" pushes `save_label` and `attach_label` twice in one
  frame.
- Fix: dedupe the wanted set.
- Size: S

### UR-33 (low) src/render/instances_window.cpp:821-832
- Defect: the folder-remove modal recounts O(instances x descendants) with
  `std::find` every frame it is open.
- Fix: compute the count once, when Remove is pressed.
- Size: S

### UR-34 (low) src/render/instances_window.cpp:1589
- Defect: `draw_pane_header` copies `instance_icons->directory` into a
  `std::string` every frame for a tooltip.
- Fix: build it inside the hovered branch only.
- Size: S

### UR-35 (low) src/render/shortcut_bar.cpp:385
- Defect: a `std::format` tooltip is built for every entry every frame.
- Fix: guard with `IsItemHovered(ImGuiHoveredFlags_ForTooltip)`.
- Size: S

### UR-36 (low) src/render/panels.cpp:191-223
- Defect: `request_flags` is called per row and per pair.
  - The CRM calls it for every pair (crm:229-232).
  - The series browser calls it for every row, twice when the combo is open
    (series_browser.cpp:619, 651).
  - The same currency pushes `load_image` once per occurrence in one frame,
    because `pending_images` only updates after the runtime drains.
- Failure: the runtime dedupes (instance_manager.cpp:3509). Each redundant
  command still allocates three strings, giving dozens per frame until the
  image lands.
- Fix: request per distinct currency per window, or keep a per-frame
  requested set.
- Size: S

### UR-37 (low) src/render/command_centre.cpp:429
- Defect: `const auto* probe` is unused and shadowed by line 396.
- Fix: delete it.
- Size: S

### UR-38 (low) src/render/command_centre.cpp `draw_overview` "Read" card
- Defect: `now_ns - observed_at_ns` is unguarded, unlike
  instances_window.cpp:1935.
- Failure: a negative age is printed for an observation stamped after
  `now_ns`.
- Fix: the same `now >= observed` guard.
- Size: S

### UR-39 (low) src/render/notice_window.cpp, grid_window.cpp
- Defect: `std::get<notice_state>` and `std::get<grid_state>` throw
  `bad_variant_access` mid-frame if kind and state disagree. The other
  renderers use `get_if`.
- Separately, grid draws fabricated rows (`fake_rows_per_page`) and uses
  `selected_row` as a page index. That is placeholder code still shipped.
- Fix: `get_if` with a fallback message. Mark the grid as a placeholder, or
  remove it from the menu.
- Size: S

### UR-40 (med) projects/hedgr.ui/tests (browser windows)
- Defect: there are no tests for id derivation, duplicate naming,
  `apply_labels`, `edit_has_changes`, the discard guard,
  `forms::folder_move_allowed`, account folder removal, or the drag payload.
  All of it lives in anonymous namespaces inside the window .cpp files.
- Fix: extract the pure parts (see Splits) and test them.
- Size: M

## Smaller render findings

### UR-41 (med) projects/hedgr.ui/tests (market windows)
- Defect: none of the market-data logic is tested. It all sits in anonymous
  namespaces:
  - `level_for` and `candle_plan_for`;
  - `build_candles`;
  - `live_window_start`;
  - both copies of `tenor_rank`;
  - `group_curves`;
  - the CRM status counting and cell dedupe;
  - term-structure primary selection;
  - `parse_iso8601_utc` (data_health_tests covers `as_of_text` and
    `age_text` only).
- Failure: UR-1, UR-3 and UR-7 are each the kind of defect a three-line test
  would have caught.
- Fix: move the pure helpers into `include/hedgr.ui/render/market_math.hpp`,
  as crm_rate.hpp already does for decimal places, and test them.
- Size: M

### UR-42 (low) src/render/series_browser.cpp:153-180 and term_structure_window.cpp:51-78
- Defect: `tenor_rank` is copied verbatim into two files. The
  series_browser copy, along with `curve_group`, `group_curves` and
  `open_curve` (183-241), sits in namespace `hedgr::ui::render` rather than an
  anonymous one, so those are undeclared external symbols. The parse itself
  has two problems:
  - "ON", "TN", "SN" and "1D" all rank 1, so `std::sort` (not stable) orders
    them arbitrarily;
  - "1Y6M" ranks as 1Y.
- Failure: a curve ladder can draw TN before ON, or flip them between frames
  as nodes change. A compound tenor lands out of order.
- Fix: one shared tenor parser with explicit ON < TN < SN ranks, used by both
  windows. Put the browser helpers in an anonymous namespace. See UR-OS
  findings for the ores tenor type.
- Size: S

### UR-43 (low) src/render/panels.cpp:316-328
- Defect: `shows_window` lets chart and crm_matrix draw while the store is
  locked, but hides term_structure and series_browser. All four read only
  session-delivered tables.
- Failure: locking the store hides curve windows and leaves charts and CRMs
  on screen. That is an inconsistent rule, and the comment above the switch
  does not name term structure at all.
- Fix: decide once: market windows follow the session, not the lock. Then
  list all four together.
- Size: S

### UR-44 (low) src/render/crm_window.cpp:198-199 (HEAD)
- Defect: `state.crm_currencies` only ever grows. Nothing clears it, although
  its doc says "since the party was chosen".
- Failure: a currency dropped from the CRM stays as an empty row and column
  for the life of the window. Once the WIP party/CRM picker lands, switching
  CRM keeps the previous CRM's currencies.
- Fix: clear it in `configure_scope` and whenever the party or CRM name
  changes.
- Size: S

### UR-45 (low) src/render/crm_window.cpp:362-368 (HEAD)
- Defect: the double-click chart lookup matches a series by substring:
  `uri.find("/fx/"+base+"?")` and `uri.find("ccy="+quote)`.
- Failure: "ccy=USD" also matches "ccy=USDT" or "ccy=USDC". The first matching
  catalogue row wins, so the wrong chart can open. The scan is also linear
  over the catalogue.
- Fix: match on parsed URI fields (see the UR-OS findings for the ores URI
  helper), or carry the series id on the CRM cell.
- Size: S

### UR-46 (low) src/render/chart_window.cpp:221-226
- Defect: `build_candles` treats an exact 0.0 high, low or open as "no OHLC"
  and substitutes the value.
- Failure: a rate series that crosses zero (EUR short rates did) draws wrong
  wicks at the zero bucket.
- Fix: carry an explicit `has_ohlc` flag on `market_point`.
- Size: S

### UR-47 (low) include/hedgr.ui/render/data_health.hpp:140-163
- Defect: `parse_iso8601_utc` allocates a `std::string`, ignores fractional
  seconds and ignores any UTC offset. It is called every frame on the CRM's
  `observed_as_of` (crm:95).
- Failure: an `observed_as_of` of "2026-10-19T10:00:00+01:00" is read as
  10:00Z, an hour off. With 5 and 15 minute CRM thresholds, that flips
  LIVE to STALE.
- Fix: parse once when the CRM snapshot is applied (in the model) into
  nanoseconds. Use the ores time parser (see the UR-OS findings), which
  handles the offset.
- Size: S

### UR-48 (low) include/hedgr.ui/render/renderer.hpp:155-172, 363-394, 578-598; include/hedgr.ui/render/windows.hpp:135-149
- Defect: doc comments are orphaned.
  - `window_ui_state`'s doc sits above `command_centre_ui_state`'s.
  - `render_input`'s doc is duplicated above `image_texture_source`.
  - `renderer_fn`'s doc sits above `lock_control`.
  - The command-centre doc sits above `render_series_browser`'s, and
    `render_command_centre_window` has none.
- Failure: Doxygen attaches each block to the wrong declaration, and readers
  get misled.
- Fix: move each block to its declaration.
- Size: S

### UR-49 (med) include/hedgr.ui/render/renderer.hpp:200-353
- Defect: `window_ui_state` is one struct carrying every kind's scratch:
  - CRM fields, including a `std::set`;
  - the browser state;
  - eight draft buffers (about 1.3 KB);
  - `instance_edit_state` (about 1.5 KB);
  - command centre tabs;
  - market view.

  `workspace::ui_state_` never erases entries for closed windows
  (workspace.hpp:152).
- Failure:
  - Every chart carries roughly 3 KB of unrelated form buffers.
  - Entries for closed windows accumulate for the life of the process.
  - Adding a kind means editing this shared struct, which contradicts
    CONTEXT.md "Window kind ... changes nothing else".
- Fix: `std::variant` of per-kind scratch structs, indexed like the renderer
  table. Erase the entry when the window row dies (generation mismatch).
- Size: M

### UR-50 (low) include/hedgr.ui/render/market_view.hpp:49-50
- Defect: `chart_span_labels` duplicates the labels already in `chart_spans`.
  `chart_span_index` and `age_text` have no production caller (tests only).
- Failure: adding a span means editing two arrays that must agree.
- Fix: derive the label array from `chart_spans` with a constexpr helper.
  Delete the unused helpers or use `age_text` in the footer, which shows only
  as-of.
- Size: S

### UR-51 (low) src/render/renderer.cpp:91-94, renderer.hpp:640-647
- Defect: `renderer_for` is documented "Never null" but returns nullptr for an
  out-of-range kind, and the walk silently skips such a window
  (workspace.cpp:440).
- Failure: a corrupt kind byte draws an empty window with no diagnostic.
- Fix: assert in debug. Fix the doc.
- Size: S

## Splits

instances_window.cpp (2,210 lines) splits on the seam between pure logic,
which takes `(const render_input&, window_ui_state&)`, pushes commands and
calls no ImGui, and drawing.

1. src/render/labels_edit.{hpp,cpp}. This is window_forms.hpp promoted to a
   translation unit and shared by both windows. It holds `set_buffer`,
   `buffer_text`, `labels_of`, `joined_labels`, `label_key_for`,
   `apply_labels` (deduped), `tree_folders` and `folder_move_allowed`. The
   copies at instances 93-332 are deleted (UR-30).
2. src/render/instance_edit.{hpp,cpp}. Pure functions:
   - `clear_create_draft`;
   - `save_new_instance`, with the collision check;
   - `editing_row`, `fill_edit_drafts`;
   - `begin_instance_edit`, `end_instance_edit`;
   - `edit_has_changes`, `save_edit`, `save_picture`;
   - a `discard_guard` extracted from render_instances_window 2075-2093.

   Unit-testable.
3. src/render/instances_dashboard.cpp. The estate strip (880-1145): state
   colour, labels, `window_minutes`, `format_clock`, `draw_dashboard`. It
   exposes `draw_estate_strip(const render_input&)`, clocked from
   `wall_now_ns`.
4. src/render/instance_pane.cpp. The details pane (1459-2004): `status_of`,
   `edit_card`, `observe_item` (better in widget/capture), header and topic
   drawers, `instance_details`.
5. src/render/instances_dialogs.cpp. `draw_create_form`, `open_create_form`,
   `draw_discard_warning`, `draw_picture_gallery` and the toolbar actions.
   The remove dialog moves into the browser, parameterised by
   `count_leaves_under` and `remove_leaf_command` callbacks, so Accounts gets
   folder removal (UR-22) for free.

instances_window.cpp keeps `render_instances_window` and the browser-model
wiring, about 150 lines.

accounts_window.cpp (952 lines) splits three ways:

1. src/render/account_edit.{hpp,cpp}. `load_draft`, `save_account` (a single
   " copy" and the collision check), `open_create_form`, `open_edit_form`
   with a `pending_fill` flag, and `session_word`.
2. src/render/account_pane.cpp. `instance_state_of` and `account_details`.
   Merge `instance_state_of` with Instances' `status_of` into one
   `probe_of(input, handle)` in panels.hpp.
3. src/render/accounts_dialogs.cpp. `draw_form`, `draw_sign_in_form` and the
   toolbar actions.

accounts_window.cpp keeps `render_accounts_window` and the wiring, about 120
lines. Delete the dead list columns from both windows (UR-29).

Market windows split differently:

1. src/render/market_frame.{hpp,cpp}. The shared scaffold for UR-11 and
   UR-12: session gate, identity, the foot built from health, and RAII
   PushID with the final foot draw.
2. include/hedgr.ui/render/market_math.hpp. Pure helpers (UR-41): level and
   candle plan, candle build, window start, tenor rank, curve primary, CRM
   cell dedupe and counts.
3. Each window then keeps only drawing plus a cached derived view (UR-5, 6,
   8, 10).

Suggested order:
1. UR-18, UR-21, UR-1, UR-4 and UR-14. Each is a few lines with a visible
   bug.
2. labels_edit and the duplicate deletion.
3. market_math with tests.
4. The derived-view caches.
5. The mechanical pane, dialog and dashboard moves.

## OreStudio consistency and reuse (UR-OS)

The OreStudio paths are in /home/marco/Development/OreStudio/ores_dev_solid_dirac
(working tree HEAD 3a49daea81, the tree Hedgr's build CMakeCache names as
ORES_SOURCE_DIR). The external/OreStudio submodule gitlink 718e142dca is not
initialised and has no ores.marketdata, so it cannot be what Hedgr builds
against.

hedgr.ui sees every ores include directory, but links only `ores::logging`.
Adding `ores::utility`, `ores::platform` or `ores::marketdata.api` is a one-line
CMake change. Server `*.core` libraries are not imported.

I re-checked UR-OS-1, 2 and 3 against the headers myself. The rest come from
the delegated pass with the cited lines.

### UR-OS-1 (high) include/hedgr.ui/render/crm_rate.hpp:44; src/render/crm_window.cpp:337-344 (HEAD)
- OreStudio: projects/ores.refdata/client/include/ores.refdata.client/presentation/currency_pair_rate_formatter.hpp:73
  (`format_rate`) and :122 (`reciprocal_decimal_places`). It is header-only.
- Defect: `crm_decimal_places` is a partial copy of the ores formatter.
  - It drops tick snapping (`tick_size * pip_factor`, line 82).
  - It looks the convention up as `{base, quote}`. Conventions are keyed by
    the canonical `pair_code`, so a reciprocal cell (the reverse direction)
    never finds its convention.
- Failure:
  - The JPY/USD reciprocal of a 3-place USD/JPY falls back to 5 places and
    shows "0.00666". The ores formatter gives "0.00666134".
  - A half-pip-tick EUR/USD shows 1.10287 where ores shows 1.10285.
  - The matrix disagrees with OreStudio's own CRM screen for the same batch.
- Fix: look up `{base,quote}`, then fall back to `{quote,base}` with the
  reversed flag. Call `currency_pair_rate_formatter::format_rate`. Delete
  `crm_decimal_places` and its tests.
- Size: S

### UR-OS-2 (med) src/render/crm_window.cpp:162-173, 203-205, 303, 330-336 (HEAD; the WIP footer code too)
- OreStudio: projects/ores.analytics/quant/include/ores.analytics.quant/domain/rate_status.hpp:25-58
  (`fresh, stale, disconnected, unavailable`, header-only). It is written by
  projects/ores.marketdata/service/include/ores.marketdata.service/messaging/crm_handler.hpp.
- Defect: Hedgr matches three string literals and has no case for
  "disconnected". The server's "stale" means lagging, which is Hedgr's
  `delayed`. The uncommitted WIP footer maps it to `data_health::stale` (red
  "STALE"), which on the chart and curve screens means a dead feed.
- Failure:
  - A "disconnected" cell is counted as unavailable but drawn as a grey
    rate, so the counts disagree with the grid.
  - With the WIP, one driver 6 minutes old turns the whole CRM footer red
    STALE, the same signal a chart gives for a dead feed.
- Fix: parse the wire string once, at the session boundary, into
  `rate_status`. Then map with an exhaustive switch: fresh to live, stale to
  delayed, disconnected to stale, unavailable to unavailable.
- Size: S

### UR-OS-3 (med) src/render/series_browser.cpp:153, term_structure_window.cpp:51 (extends UR-42)
- OreStudio: projects/ores.marketdata/api/include/ores.marketdata.api/datum/value.hpp:79
  (`term::parse`; kinds period, date, fx_tenor and continuation; compiled in
  `ores.marketdata.api`). The order is `in_term_order` in
  projects/ores.marketdata/core/include/ores.marketdata.core/service/series_shape.hpp:111
  (core, not linkable).
- Defect: the hand-rolled `tenor_rank` does not follow the server's term
  grammar.
  - ON, TN, SN, c1 and 1D tie.
  - "1Y6M" ranks the same as 1Y.
  - A dated pillar "2026-12-15" ranks as 2026 days, between 5Y and 6Y.
- Failure: curve ladders draw out of order, and nondeterministically on ties
  under `std::sort`.
- Fix: upstream, move the term ranking into `ores.marketdata.api`. Then link
  it and delete both copies. Alternatively, read the ladder through
  `get_series_slice`, whose coordinates the server returns in term order.
- Size: M

### UR-OS-4 (med) src/render/crm_window.cpp:362-368 (HEAD) (replaces the fix in UR-45)
- OreStudio:
  - URI grammar: projects/ores.marketdata/core/include/ores.marketdata.core/datum/oresmd_uri_codec.hpp:56-63.
  - Spot and forward schema rows: projects/ores.marketdata/api/include/ores.marketdata.api/datum/schema.hpp:335-336
    and :426-427.
  - The `parse_oresmd_uri` wire op in operations_protocol.hpp:201.
- Defect: the series is matched by substring and ignores `instrument`. fx_fwd
  and fx_option series share unit_ccy and ccy with spot.
- Failure: double-clicking EUR/USD can open the forward or option series
  instead of spot, whichever sorts first.
- Fix: parse URIs once at the session boundary (promote the codec to the api
  library, or call `parse_oresmd_uri`). Store `instrument`, `unit_ccy` and
  `ccy` on `market_series_row`, and match `instrument == fx_spot` plus both
  currencies.
- Size: M

### UR-OS-5 (med) src/render/panels.cpp:203; src/widget/flags.cpp:42-45; crm_window.cpp:231 (HEAD)
- OreStudio: currency images come from the server through
  `list_currencies` and `list_images`. Domain:
  projects/ores.refdata/api/include/ores.refdata.api/domain/currency*.hpp.
- Defect: `request_flags` is the only issuer of `load_image`. It gets its
  codes by scraping the display label through `currencies_of`, which accepts
  only 27 hardcoded ISO codes. The CRM fabricates a `"FX "+base+"/"+quote`
  label to feed it.
- Failure: a CRM with THB, SAR or AED never loads those flags, although the
  server holds images for them. Any change to the server's label format
  removes every flag.
- Fix: pass currency codes, taken from the CRM cell or the parsed URI fields.
  Gate on `currency_images.image_for(...) != nullptr`, not on the local list.
- Size: S

### UR-OS-6 (low) src/render/series_browser.cpp:200, 459, 462; crm_window.cpp:359 (HEAD)
- OreStudio:
  - projects/ores.marketdata/api/include/ores.marketdata.api/datum/schema.hpp:261-281
    (`enum class asset_class`, `name_of`, `asset_class_named`);
  - asset_class_authorities.hpp:40-79.
- Defect: render branches on "IR" and "FX", a third vocabulary that
  hedgr.session creates by upper-casing the URI authority. The server uses
  "ir" (URI) and "interest_rates" (refdata).
- Failure: if the session's casing or source changes, the IR curve view and
  the CRM chart link silently find nothing, and the code still compiles.
- Fix: store `datum::asset_class` on the row, compare enums, and format only
  for display.
- Size: S

### UR-OS-7 (low) include/hedgr.ui/render/data_health.hpp:140 (extends UR-47)
- OreStudio: projects/ores.platform/include/ores.platform/time/datetime.hpp:58,
  :73. `to_iso8601_utc` writes "YYYY-MM-DD HH:MM:SS.ffffffZ".
  `from_iso8601_utc` accepts T or a space and checks the designator.
- Defect: `parse_iso8601_utc` requires a literal 'T'. It works only because
  crm_handler.hpp:65-68 happens to format `{:%FT%TZ}`, not the project's
  documented wire format.
- Failure: if the handler moves to `to_iso8601_utc`, the parse returns 0 and
  the CRM loses its as-of time silently.
- Fix: parse with `datetime::from_iso8601_utc` at the session or runtime
  boundary (link `ores::platform`), and hand render nanoseconds.
- Size: S

### UR-OS-8 (low) include/hedgr.ui/render/data_health.hpp:44-72; chart_window.cpp:287
- OreStudio: projects/ores.analytics/quant/include/ores.analytics.quant/domain/staleness_policy.hpp:37
  (the same two-threshold shape, header-only).
- Defect: `health_thresholds` duplicates `staleness_policy`, and the chart
  applies FX thresholds to every asset class.
  - In HEAD, `crm_health_thresholds` is used at crm_window.cpp:97.
  - The WIP removes that use, which leaves the constant dead.
- Failure: a daily IR or EQ series in a chart reads STALE after 30 s.
- Fix: choose thresholds per asset class. Reuse `staleness_policy`'s shape,
  and delete the CRM constant if the WIP lands.
- Size: S

### UR-OS-9 (low) src/render/command_centre.cpp:286, workspace_window.cpp:263, instances_window.cpp:1935 vs data_health.hpp:166
- OreStudio: projects/ores.platform/include/ores.platform/time/relative_time_formatter.hpp.
- Defect: age is formatted three ways. Three sites print `"{:.0f} s ago"`
  and `age_text` prints "12m".
- Failure: an hour-old snapshot reads "3612 s ago" in the command centre and
  "1h" in the footer.
- Fix: one helper, either `relative_time_formatter` or `age_text`
  everywhere.
- Size: S

### UR-OS-10 (low) include/hedgr.ui/render/market_view.hpp:40-50; market_view.cpp:116
- OreStudio: value.hpp:69. In the term grammar, "1M" is one month.
- Defect: the chart period labels "1M", "5M" and "15M" mean minutes, and the
  saved token `period=1M` reuses the label.
- Failure: in the same window, "1M" on a curve axis means one month, and in
  the chart period combo it means one minute.
- Fix: relabel to "1m", "5m", "15m", "1h" and "1d". Decouple the saved token
  from the label and accept the old tokens when reading.
- Size: S
