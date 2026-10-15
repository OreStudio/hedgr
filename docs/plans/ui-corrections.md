# UI corrections

The operator's list, recorded verbatim where the wording matters. Nothing here
is done yet. It is one unit of work plus several defects, and it is deliberately
written down before it is started so that none of it is lost.

## Defects

1. **The edit form shows blank fields.** Clicking edit on an instance, for
   example Brave Hopper, opens a form with every field empty rather than
   populated from the row. The form is not reading the row it is editing.
2. **Accounts appears ten times in Brave Hopper and every one is empty.** The
   expectation is the tree that was asked for: `Accounts / Privileged /
   Super Admin`, `Accounts / Privileged / ACME Tenant Admin`, `Accounts /
   Non-Privileged / User123`. Ten empty copies says the accounts window is being
   opened once per instance when it should be one window, and that it is showing
   accounts that do not exist.
3. **The Accounts window cannot be closed.**
4. **Double clicking an entry does not open it.** It should open the entry the
   way the open action does.
5. **The Instances window cannot be closed.**

## Modal screens

6. **Screens must not become modal.** A trading system has many things happening
   at once and a modal stops everything. Every dialog and every form becomes
   non-modal. This includes the create and edit forms and the master password
   prompt.
7. **Save and cancel buttons** on the forms, explicitly.

## Toolbars

8. **There are two toolbars and there should be one**, the one next to the tree.
9. **Probe every instance moves out of the main toolbar** into the instance
   toolbar only.
10. **Remove refresh from every window.** There will soon be many windows and a
    refresh on each is clutter.
11. **Remove the fullscreen icon from the toolbar.**
12. **Add an instances icon to the toolbar**, so the Instances window can be
    brought back once it is closable.

## Status and warnings

13. **An unreachable host must be very visible and must show the message.** The
    probe already records the low-level cause and it is currently only in a
    tooltip. Consider a status bar. The message must be readable without
    hovering.
14. **Unreachable entries render red**, so the state is visible in the list
    without reading anything.

## Visual

15. **The row icon carries the state.** Instead of a separate ready-or-not
    glyph, the instance icon itself is red when unreachable, amber when
    bootstrapping, and green when ready.
16. **Remove the other columns.** The reachability and readiness columns go,
    since the icon now carries both.
17. **Labels use a much smaller font.** The pills are too large.
18. **The dashboard is always visible and is not collapsible.**
19. **The dashboard becomes a plot**, collecting the counts over the polling
    time rather than showing only the current numbers. This means the runtime
    must keep a short history, not just the latest probe result.

## The master password warning

20. **The massive yellow warning is not idiomatic.** Being removed. It is
    replaced by a transient status bar entry, see item 21.

## Status bar

21. **The Instances window gets a status bar** for short messages describing
    something that just happened. "The store was opened with the password from
    the environment" is the first such message, and it disappears after a little
    while rather than staying for the session.
22. **The unreachable warning belongs in the same bar** but is a different kind
    of thing: it describes a current condition, not an event, so it persists
    while the condition holds and does not fade. Item 13 and this are the same
    surface, and the bar has to carry both kinds.

Neither the prompt nor a banner carries the environment notice any more. The
prompt is about the password and nothing else, and no screen in this application
is modal.

## What this implies beyond the interface

- A **history** of probe results per instance, or of the aggregate counts, so
  the dashboard can plot over time. The probe table holds only the latest
  result today.
- The **Accounts window is one per instance** but the defect says ten are
  appearing, so the identity used to deduplicate them is likely wrong, or the
  window id is derived from something that differs per account.
- **Non-modal forms** means the form's state has to live in interface scratch
  keyed by what it is editing, rather than being a single global draft. The
  forms currently draw as windows; several will be open at once once they stop
  being modal.

## Queued separately: signals and sound

Not part of the interface corrections. Recorded here so the queue is in one
place.

- A generic **named signal** raised from anywhere, with **sound as one
  subscriber** and a status bar expected to be the second. See
  `docs/adr/0008-named-signals-and-sound.md`.
- The first signals are **transitions**, not polls: a deployment that stops
  being ready, drops into bootstrapping, or becomes unreachable, and the inverse
  of each. A signal fires on the change, so twenty consecutive failed probes are
  one signal rather than twenty.
- Sounds come from Kenney's UI and interface packs, which are CC0. A **curated
  subset is vendored** into `assets/sounds/`; a submodule was considered and
  rejected because the pack is mostly files this application will never use.
- The backend is **miniaudio**, already in Hedgr's vcpkg.
- The default registration should be **no sound at all**. Every sound is a noise
  heard repeatedly in a room where other people work, so an operator opts in.
