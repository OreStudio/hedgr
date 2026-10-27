# Remaining work

Written on 2026-10-24 at version 0.3.1, so nothing agreed in conversation is lost.
Each item says what it is, why, and where to start. Items are in the order I would do
them. "Blocked" names what must land elsewhere first.

## State of the code

- Main is at 0.3.1 and deployed to goedel (`hedgr/current`, restarted).
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

- **Tick history by identity.** `get_series_ticks` takes a typed identity (PR 3711), and
  the identity has no provider, so 65 identities in the test tenant match two series of
  one party (an `ore` and a `bloomberg` series). The server answers `series_ambiguous`
  and no history loads. Brave Hopper will add an optional `provider_code` to
  `resolve_series_identity_request`. When their header carries it, Hedgr's change is in
  `session_client.cpp` (`series_identity`, around the tick call): copy the series' own
  provider (it is on the catalogue row, `market_series_row.provider_code`) into the
  identity before sending. Build against their merged main.
- **Reference providers in the catalogue.** Every FX spot is listed twice, `ore`
  (reference, never ticks) and `bloomberg` (data vendor, ticks). The operator was asked
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
- **Goedel's server** must run OreStudio main with PR 3711 and the provider fix for the
  new Hedgr's history reads. Not checked.

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

## Small items

- The VIEW > Windows panel has no filter; a wall of forty charts is a scroll.
- The catalogue's "add to bar" control and `shortcuts_bar_holds` still say "bar". They
  mean pin to the Accounts list.
- Rename `docs/plans/mini-plots.md` step notes that mention the retired compact layout.
- VIEW > Toolbar and VIEW > Full screen are lit rows, not checkmarks, because
  `menu_row` has no check column.
- The About window is hidden while the store is locked.
- Docked content is not retinted to the reference's content colour (`#191923`); the
  tab hover colour (`#84719F`) is a guess.
- Tabs outside the desk's dock space (floating dock nodes) keep the base font.
- The reference's menu bar mode switcher and window controls are not built.
- Monospace font: JetBrains Mono Regular is bundled. A bold weight is not; the intro's
  "HEDGR." uses the system bold face, so a host without it shows regular weight.
- The tick call's identity lookup, the probe backoff wiring and the market worker's
  silence window have no test beyond the pure rules.
- `docs/review/README.md` status cells are not updated. Older unreviewed items: the
  series table decisions D1 to D7, the session actor decisions D1 to D6, the wording of
  PR #63.
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
