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
  operator has seen the menu bar, tabs, mini plots and marks.

## Six-item list (menu system), items still open

Plan: `docs/plans/menu-system.md`. Items 1 to 3 below are done.

1. Done: CRM Mini plots button (confirms above 30 cards).
2. Done: floating toolbar.
3. Done: Instances panel.
4. **Accounts panel.** An ACCOUNTS menu with a large panel: tenant headings with the
   accounts under each, from the tenant and accounts tables. A click opens the accounts
   window for that instance and account. It replaces the TOOLS > Accounts submenu that
   stands in for it. Use `view_state_of` for empty states, no sentences. Same panel
   look as the Instances panel (`render/instances_panel.cpp`).
5. **Windows list.** A WINDOWS menu listing open windows from `input.windows`, grouped
   by kind. A click raises the window (the `focus_instances` request mechanism), an X
   closes it through the close command. Mini plot actions go here.
6. **Status bar.** A bottom strip as a side bar like the top bar: the message ladder
   from `widget/status_bar.hpp` on the left, the connection state on the right. The
   work area is then inset by the two bars.

Follow-ups noticed while building the Instances panel:

- A tile click raises the Instances window but does not select that instance in it. It
  needs a request command, because the window's selection is private.
- Tiles are in table order. Sort by name.

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
- **Provider kind.** `marketdata.v1.providers.list` returns each provider's `code`,
  `name` and `kind` (`reference`, `data_vendor`). Hedgr does not call it. Reading it
  lets the browser hide reference providers by default and the CRM and Mini plots
  button prefer data vendors.
- **Ticks per party.** The tick table filters by the party in the session's token.
  Only data-vendor series of four parties tick (UK, US, HK, System Party). Their own
  fix for the operator's party (PR 3710) may change this. Re-check after the next
  environment rebuild.
- **Goedel's server** must run OreStudio main with PR 3711 and the provider fix for the
  new Hedgr's history reads. Not checked.

## Design agreed but not built

- **Act as a party.** A session signs in as one party and never changes it. The market
  data browser's Party combo only filters the list, so choosing another party shows its
  series but reads their ticks with the wrong token. Design: a control on the session
  ("Acting as: ..." in the identity bar) that calls the existing `select_party`, swaps
  the token, re-reads the catalogue and moves the live stream. One party at a time per
  session. A session per party is the larger alternative. Plan it, then build it as its
  own PR.
- **All parties** choice in the market data browser's Party combo, with a note when the
  list is narrower than the catalogue.
- **Carry the series identity in the catalogue.** Each tick read now makes one extra
  `list_series_identities` call (up to three per first load) and the reply is the whole
  list. Carry the identity into the series row from the catalogue read instead.

## Small items

- The CRM "Mini plots" button and the CRM double-click take the first catalogue row that
  matches a pair, whatever its provider. It lands on `bloomberg` only because that
  sorts before `ore`. Prefer data-vendor providers once the kind is known.
- The catalogue's "add to bar" control and `shortcuts_bar_holds` still say "bar". They
  mean pin to the Accounts list.
- Rename `docs/plans/mini-plots.md` step notes that mention the retired compact layout.
- VIEW > Toolbar and VIEW > Full screen are lit rows, not checkmarks, because
  `menu_row` has no check column.
- The Accounts child panel has no caret. The About window is hidden while the store is
  locked.
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
