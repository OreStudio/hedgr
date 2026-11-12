# Remaining work

Written on 2026-10-24 at version 0.3.1, so nothing agreed in conversation is lost.
Each item says what it is, why, and where to start. Items are in the order I would do
them. "Blocked" names what must land elsewhere first.

## State of the code

- Main is at 0.3.3 and deployed to goedel (`hedgr/current`, restarted).
- Merged since 0.2.0: mini plot cards (fixed size, snap grid), chart header strip and
  purple palette, tab look, top menu bar with FILE, VIEW, INSTANCES, TOOLS and HELP,
  floating toolbar, Instances panel, Provider combo, launch intro with sound, the UX
  language (`docs/plans/ux-language.md`), fast failure of unanswered reads, probe
  backoff, the cool bar's retirement, VIEW > Full screen.
- Nothing recent has been seen in a real window by the agent that built it. The
  operator has seen the menu bar, tabs, mini plots and marks. The Accounts menu,
  the VIEW > Windows panel and the bottom status strip are new and have only
  headless frame tests and frame-cost tests behind them.

## Six-item list (menu system)

Plan: `docs/plans/menu-system.md`. All six are done.

1. Done: CRM Mini plots button (confirms above 30 cards).
2. Done: floating toolbar.
3. Done: Instances panel.
4. Done: **Accounts menu.** A top-level ACCOUNTS menu, after INSTANCES, lists
   each environment that holds an account, sorted, and opens that environment's
   accounts beside it. The accounts are grouped under their folder (a heading,
   not a row) and sorted by name. A click signs in at once when the store holds
   the password, and otherwise opens a password form with "Remember this
   password", owned by the menu bar. TOOLS > Accounts is gone.
   `account_sign_in_command` is shared with the Accounts window.
5. Done: **Windows list.** VIEW > Windows lists the open windows under a heading
   per kind. The title raises through the focus request; the x closes through
   the close command. "Close all mini plots" sits at its foot
   (`render/menu_bar.cpp`).
6. Done: **Status bar.** A 24 px bottom strip through
   `BeginViewportSideBar(ImGuiDir_Down)`: the message ladder at the left, the
   connection state of the sessions at the right. The work area is now inset by
   both bars (`render/status_bar.cpp`).

Follow-ups noticed while building the Instances panel, both done:

- Done: a tile click raises the Instances window and selects that instance; the
  selection goes through `request_instance_selection`, because the window's
  selection is private.
- Done: tiles sort by name, folded, with the id breaking a tie
  (`widget::instance_less`).

## Blocked on OreStudio (Brave Hopper session)

- Done: **Tick history by identity.** `get_series_ticks` takes a typed identity,
  and Hedgr sends the one parsed from the catalogue URI with the series' own
  provider. It is closed by two runs of `hedgr.session.tests "[.live-providers]"`:
  - on `swift_curie` (OreStudio `6f5f5876`, libraries `0.0.29`) the read answers
    with and without the provider — 339 points for USD/SEK, 340 for USD/ZAR each
    way — so there the party is what scopes the read and the provider adds
    nothing;
  - on `brave_hopper`, rebuilt on 2026-11-12, the provider is what makes the read
    name one series. A `bloomberg` series reads (USD/BRL, **4340 points**); the
    same identity with the provider dropped is refused as `series_ambiguous`
    ("names 2 series of the ticks (1 parties; providers bloomberg, ore)"), and
    the reference twin is refused as `not_subscribed` ("needs a subscription to
    the provider ore that holds display or non-display, and this party holds
    none that does").
  That closes the last "not yet run" line about history reads.
- **Reference providers in the catalogue.** Every FX spot is listed twice, `ore`
  (reference, never ticks) and `bloomberg` (data vendor, ticks). The deployment has now
  said what a reference series is worth to this client: reading one is refused with
  `not_subscribed` unless the party holds a subscription to that provider, so a window
  that lists them offers rows that cannot be plotted. Hedgr's default of hiding them
  (VIEW > Reference providers shows them) is what that supports. The operator was asked
  to choose between: Hedgr filters (done in part: the Provider combo), the server hides
  reference providers by default with an `include_reference` flag (recommended), or the
  `ore` series are no longer published. No answer sent yet. When the server decides,
  adjust the Provider combo's default.
- Done: **Provider kind.** `marketdata.v1.providers.list` is read with every
  catalogue, paged to the deployment's total, and carried on the catalogue as
  `market_provider_row` (`code`, `name`, `kind`). The browser hides `reference`
  providers by default, and VIEW > Reference providers shows them; the Provider
  combo lists only the providers that survive. The CRM's pair lookup and Mini
  plots prefer a non-reference (data-vendor) series and fall back to catalogue
  order when the kinds are unknown. A failed provider read hides nothing.
  `docs/plans/provider-kind.md` has the design.
- **Ticks per party.** The tick table filters by the party in the session's token.
  Only data-vendor series of four parties tick (UK, US, HK, System Party). Their own
  fix for the operator's party (PR 3710) may change this. Re-check after the next
  environment rebuild.
- Done: **Goedel needs no OreStudio of its own.** Its only OreStudio units are the
  compute wrappers, and its Hedgr store names the instances on this host
  (`nats://192.168.1.172:20405` and the rest), so the client there reads the same
  deployments and there is no second server version to align.

## Design agreed but not built

- Done: **Act as a party.** The market browser's header Party combo only filtered the
  list, so choosing another party showed its series and read their ticks with the wrong
  token. The session now has one party at a time, and the control that changes it is an
  "Acting as" foot combo on every session window (shown when the login has more than one
  party). A change pushes a `select_party` command; the runtime leaves the party for the
  session's own worker and wakes its refresh wait, the worker calls the instance's
  live-session `switch_party` operation (`select_party` stays the login's single-use
  exchange), and reports the new token and party. The runtime replaces the session's
  token and party, reopens the live stream for the new party and starts a catalogue
  read, waiting for an in-flight read first. A refusal leaves the session on its old
  party and raises a condition on the strip. The header combo is gone: the window
  follows the session, and the other windows read the party from the session as before.
  ADR 9 records the shared switch object.
- Done: **the "Acting as" control is on every window's identity bar.** The series
  browser's foot logic moved to `render/party_foot.cpp`, and the chart, CRM and term
  structure feet draw through it too. One control builds the request; a choice pushes
  `select_party`, and choosing the party the session already holds pushes nothing.
- The old **All parties** choice is obsolete: the act-as-party work removed the
  browser's Party combo, and a session now holds one party at a time, so there is
  nothing to list across.
- Done: **Carry the series identity in the catalogue.** Each catalogue row now
  carries the typed identity (asset, scope, instrument and quote spelling, party,
  provider, and the schema-order fields) parsed once from its `oresmd_uri` with
  OreStudio's codec. A tick read sends that identity instead of calling
  `list_series_identities` for the whole deployment; a row whose URI the codec
  cannot read falls back to the list, so an old catalogue still works. The extra
  call per read, and the two more of a widened first load, are gone.

## Curves, in the order given (A to D)

- Done: **A, the IR curve.** Choosing `IR` in the browser lists curves rather
  than points, and a curve is every tenor its provider and party list, so one
  click opens the whole ladder; a reference provider's twin copy of a tenor is
  no longer gathered with the vendor's (#174, #175).
- Done: **the published curve, end to end.** The deployment calibrates curves
  and publishes them as one object each; those are now read through the session
  link, parsed at the boundary, carried with the catalogue, listed in the
  browser's IR view beside the tick curves, and drawn by the term structure
  window as the deployment stated them. The curve model was unified to make
  that possible: one columnar `curve_table` holds a curve whichever way it was
  read, `term_structure_table` and its ladder are gone, and a curve's pillars
  are ordered once per read rather than once per frame. Two things recorded for
  the surface slice: the surface reuses this shape (two shared axes and a grid
  of values per instant, see `docs/verification/rates-term-structures.md`), and
  a curve lookup no longer allocates because a frame asks for its curve once.
- Next: **B** is superseded by the published curve - the deployment states the
  curve, so deriving it from member rings is no longer on the path - and **C**
  (the volatility surface protocol probe) is the next slice, followed by **D**,
  the 3D plot.

## Small items

- Done: VIEW > Windows has a title filter at the top of the panel, folded, with a
  "No window matches" row. The "Close all mini plots" action still closes every
  card, not only the filtered ones.
- Done: the catalogue's pin control and `shortcuts_list_holds` (was
  `shortcuts_bar_holds`) say the Accounts list, not "the bar".
- Done: `docs/plans/mini-plots.md` opens with a partly-superseded note, and its two
  former compact-layout steps are titled as superseded.
- Done: VIEW's switches (Full screen, Toolbar, Reference providers) carry a check in
  a reserved column rather than the chosen-row bar.
- Done: HELP stays on the bar while the store is locked, so About is reachable.
- Done: a docked card's body is the reference's flat content colour (`#191923`),
  sampled at (400, 300) of the tab reference, instead of the floating card's
  gradient. The tab hover colour (`#84719F`) is still a derived guess: the
  reference shows no hovered tab.
- Done: tabs in floating dock nodes use the tab face too. Their host windows are
  submitted inside `ImGui::NewFrame`, so the app starts a frame through
  `theme::begin_frame_with_tab_font()`, which makes the tab face the default for
  that call and restores the base face before any window body draws.
- The reference's **window controls** are decided against: the desktop draws the
  window frame, per ADR 10 (`docs/adr/0010-the-window-frame.md`). Its **menu bar
  mode switcher** is the part of that title bar which is the application's own,
  and is still to be built once the chart has modes.
- Done: **the bold monospace weight ships.** JetBrains Mono Bold is bundled
  beside the Regular and found by the same lookup, and it is the emphasis face
  everywhere: the intro's wordmark, a table's captions and a tab's badge count.
  It is preferred over the host's own bold, which used to be the only source and
  could be absent, in which case emphasis drew at the regular weight with nothing
  to show that it was meant to stand out. The host's bold of the base face stays
  as the fallback when a package carries no bold file. Two tests: the file is
  found by the same rule as the Regular in both install layouts, and a painted
  frame shows the bold covering more of the page than the Regular.
- Done: the probe service's deadline wiring is tested against the backoff rule
  (`probe_backoff_tests.cpp`), the tick identity parse is tested, and a tick read
  inside the silence window is tested by driving the read directly over the fake
  link (`tick_silence_tests.cpp`), since a market read no longer has a spawn of
  its own to go through.
- Done: **the session actor's step 6 has landed** — supervision creates the actor
  at the connect command, the actor owns the whole lifecycle (sign-in, sealed
  credential, refresh with a backoff until expiry, party change, logout on a
  sign-out), and `session_worker`, `party_switch` and `deadline_wait` are deleted.
  A session now has one connection for reads, ticks, the lifecycle and the
  logout.
<<<<<<< HEAD
- Done: **the step 6 live check is complete.** Against the `brave_hopper`
  deployment on this host (broker `localhost:20405`, mTLS, monitor
  `localhost:20406`), a real session ran 1440 s, the actor's refresh fired and
  replaced the token, the session held **one** connection across it (`connz` read
  25 before, 26 throughout, 25 after the sign-out) and signed out cleanly.
  `hedgr.runtime.tests "[.live-refresh]"` is the check and
  `"[.live-refresh-now]"` its nine-second form. Only a real broker restart is
  still unchecked.
- Done: **the stale-library skew that hid the check, and what it taught.** The
  first attempt failed with `std::bad_alloc` from every authenticated request,
  because Hedgr compiled against OreStudio headers from 2026-11-01 while linking
  libraries from 2026-10-20, and `nats_client` — which `session_client` holds by
  value — had gained a member in between. The operator's sprint 29 rebuild
  (`0.0.29` libraries, 2026-11-02) and a Hedgr relink fixed it. Two things to
  keep: the bundler copies OreStudio libraries into `publish/lib` and will
  re-use the stale copy unless they are removed first, and a successful sign-in
  proves nothing about reads because `authenticate` swallows its own read.
=======
- Done: **the session actor's step 7 has landed** — a session's commands *and*
  its facts are supervised by `session_service`. `connect`, `disconnect` and
  `select_party` moved first; then the session cases of
  `instance_manager::apply` and the `attempts_` map that drops a fact from a
  replaced attempt, so the facade no longer looks a session up by an event's
  pair. The effect the service returns now describes a landing window, a rebind,
  a feed attach or detach, a watch refresh on a party change, a read to start,
  and a line in the bar with its removal.
>>>>>>> main
- Done: **the step 6 live check, in part.** Against the `jolly_knuth` deployment
  on this host, a real mTLS sign-in as `super_admin` held **one** connection for
  the whole session and gave it back on the sign-out (the broker's list read 25,
  26, 25), and the refresh was armed at 1440 s of the token's 1800 s. Still
  unchecked: a real broker restart. `hedgr.runtime.tests "[live-probe]"` repeats
  the probe with `HEDGR_LIVE_*` set, as its comment says.
- Done: **`docs/review/README.md` status cells are current.** The session actor's
  decisions D1 to D6 and the series table's D1 to D7 each record where they came
  out — D1, D2, D3, D5 and D6 as recommended, D4 still undecided, D7 not done
  and its trigger unfired — and the SR-6, C-5, SR-12, SR-15 and SR-24 notes say
  what they stand on. Bundle 7's wording is corrected: the history read that
  bundle and #63 built through the observation endpoint has since been replaced
  by the ticks call, and the record now says so.
- Done: **SR-12 is closed.** A window kind with no read has a zero cadence and a
  parked deadline at all four moments, so an idle desk no longer sends a refresh
  per such window per second for an event that named the window and carried
  nothing. `read_kind::placeholder`, that event (`window_data`) and
  `read_service`'s event-queue reference went with it, and the instance-manager
  window — built outside the traits table — now takes its cadence and deadline
  from the table.
- The operator's old `imgui.ini` layout was never copied to `.cache/hedgr/imgui.ini`.
  Old dock nodes may carry the hidden-tab-bar flag.

## Platform and deploy

- Wayland is the default. `HEDGR_PLATFORM=x11` is needed for windows torn out of the
  main window. The default-platform decision and the seven-point X11 checklist
  (`docs/plans/mini-plots.md` step 6) are the operator's.
- Deploy with `build/scripts/deploy.sh` (goedel by default). It builds the package,
  copies it, unpacks beside the previous version, moves `current` and restarts the
  client as a user unit. Deploy only when asked.

## How to work in this repo (the operator's rules)

- Edit with the Edit and Write tools and read with Read. No Python heredocs or `sed -i`
  to change source. Bash is for builds, tests, git and gh.
- Do small changes directly, not through an agent.
- Do not change code unrelated to the task and do not remove helpful debug logging.
- Build only when the OreStudio tree is not being built: Hedgr compiles against
  `/home/marco/Development/OreStudio/ores_dev_brave_hopper`, which is read-only and moves.
  Ask the Brave Hopper session when a header changes under you.
- One agent at a time, `ninja -j4`. Never run `hedgr.imgui` or open windows from a
  session. Headless frame tests are fine.
- Commit trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. PR bodies
  end with the Claude Code attribution line. Merge on request.
- The operator's own screenshots are in `/home/marco/Pictures/Screenshots` and the
  converted references in `docs/samples`.
