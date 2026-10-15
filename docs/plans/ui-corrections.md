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
   Pulled forward into the unit in flight, because the operator hit it on the
   Accounts window. The browser draws a strip and each window draws another; they
   collapse to one beside the tree, and it carries what both carried, including
   sign-in on Accounts and add instance and probe on Instances. Fixed in the
   browser rather than per window, so both hosts are fixed and a third would be.
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
    glyph, the entry's own icon is red when unreachable, amber when bootstrapping,
    and green when ready. Being done in the unit in flight.
16. **Remove the other columns.** The reachability and readiness columns go,
    since the icon now carries both. Being done in the unit in flight.

    The operator has been explicit that this is **one icon and not two**, and that
    it is **the same icon the tree already draws** for the entry. Two surfaces
    showing the same entry should show the same glyph, tinted, rather than each
    inventing its own or the list adding a second marker beside the first.
17. **Labels use a much smaller font.** The pills are too large. Being done in
    the unit in flight, because it lands in the same rows.

    Small enough to read as a badge rather than as text, and the padding, corner
    radius and row height scale with the text rather than staying at the size
    they were, which would give a large box with small writing in it.
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
- Sounds are **registered by default while in development**, as the operator
  asked, so a state change is noticed without watching the window. The
  registration becomes configuration later; until then the mapping stays small
  because a failing deployment can raise a signal on every poll.

## The lock UX, designed by the operator

This supersedes what ADR 4 and the earlier brief said about the prompt being the
surface. The window is not replaced; its content is blanked.

1. The Instances window is drawn with its contents obscured while locked. The
   shell, the toolbar and the status bar stay visible. Nothing is modal.
2. The lock icon on the status bar carries the state and is the only control for
   it.
3. Clicking the lock opens the password entry. It is the only way in.
4. A wrong password returns to locked and stays there. It does not retry and does
   not re-present. The operator clicks the lock to try again, which stops a
   failure becoming a loop.
5. A password taken from the environment that worked is reported on the status
   bar, not in the prompt.
6. Colour carries the state: **green unlocked, amber locked, red for a failure**.
   Amber because being locked is a normal state rather than an error, and red
   belongs to something that has actually gone wrong.
7. **Every state carries text as well as an icon.** "The store is locked",
   "the store is unlocked", and a sentence naming the failure when there is one.
   An icon alone is a shape that has to be learned, so a person reading it for
   the first time learns nothing from a padlock. This applies to everything the
   bar carries, not just the lock.
8. The blanked content still reads as the application. The tree and the list are
   visibly there and waiting; an empty box or a spinner is not the same thing.

## Defect: glyph colours are channel-swapped

Found while verifying items 15 and 16, and not caused by them.

**Untextured drawing is correct and textured drawing is not.** In the same
frame, in the same window, the label pill fills land exactly on their theme
colours, and the row icons land exactly on the red-blue exchange of theirs.

Measured on `.cache/verify/unit11-final.ppm`, by counting exact pixel values and
taking their bounding boxes:

| what | colour written | colour rendered | where it is |
|---|---|---|---|
| label pill fill | `(64,160,255)` blue | `(64,160,255)` exact | x 1082 to 1153, the Labels column |
| row icon, ready | `(0,200,83)` green | `(83,200,0)` swapped | x 611 to 624, the icon column |
| row icon, not ready | `(255,193,7)` amber | `(7,193,255)` swapped | x 611 to 624, the icon column |
| row icon, unreachable | `(255,23,68)` red | `(68,23,255)` swapped | x 611 to 624, the icon column |

Rectangles are drawn without a texture and come out right. Glyphs are drawn
from the font atlas and come out swapped. That is the whole difference between
the two, and it is the thread to pull.

**It is not new.** Older frames show green glyphs swapped too, two hundred and
forty pixels of them in both `one.ppm` and `clean_unlocked.ppm`, where the green
was a status marker. Nobody noticed because green exchanged with itself looks
green. It became visible only now, when a state that should be amber began to be
drawn as a glyph: amber exchanges with cyan, and cyan reads as information
rather than as a warning, which inverts what the colour is for.

**Not yet found.** The OpenGL3 backend for this ImGui version uploads the font
atlas as `GL_RGBA` and asserts `ImTextureFormat_RGBA32`, so the texture format is
not obviously the cause, and the vertex colour path is shared with the
rectangles that render correctly. The next step is to check what is actually
bound while a glyph is drawn, and whether the atlas is being sampled as the
texture the draw expects.

## Rule: every failure reaches the interface

The operator, after a blank screen caused by a store the build refused: "make sure we
throw and we show errors clearly in the UI".

`instance_manager::last_error_` is already the accumulator for every failure the
runtime records: a store that will not open, a save the store refused, removing the
root being declined, a schema mismatch, a certificate that will not open. It is set
in many places and read by no surface at all. A log line is not a surface, and on a
deployed host the log does not even reach the terminal.

So the store-startup case now being fixed is the first instance of a rule rather
than a special case. The rule is that every recorded failure is visible, with what
went wrong and what to do next, on the status bar the operator already reads.

Two things to keep in mind when applying it. A failure must be distinguishable at a
glance from a remark such as "the store was opened from the environment", so the two
do not share a treatment. And an error is closer to a condition than to an event:
it should still be there for an operator who looked away, without sitting there
forever after it has been read.

Errors are `std::expected`. The operator's word "throw" means propagate and
surface, and the operator has confirmed that reading. Every fallible operation at a
component boundary returns `std::expected<T, E>` carrying a typed code and a sentence
written for an operator, and the caller either handles it or records it where the
interface can see it. Exceptions stay reserved for the genuinely unexpected, which is
the project's documented style and OreStudio's, and no error in this list is one.

This is the same discipline the store, the transport and the runtime already follow.
What was missing was only the last step: an error that is propagated to the manager
and then read by nothing is, from where the operator sits, the same as an error that
was never detected.
