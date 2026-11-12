# 10. The window frame belongs to the desktop

**Status.** Accepted.

## Context

The reference the interface is drawn against has a title bar of its own: the
menu bar carries a mode switcher and, at the right, the window controls of a
conventional application — minimise, maximise, close. Hedgr draws none of them.
Its top-level window carries whatever decoration the compositor gives it, and
the work list has carried "the reference's menu bar mode switcher and window
controls are not built" as an open item waiting on this decision.

The question is whether Hedgr should draw its own window chrome instead: a
frameless toplevel, a title bar painted with ImGui, a drag region that moves the
window, and buttons that minimise, maximise and close it.

Three facts bound the answer.

- **Wayland is the default.** ImGui's GLFW backend reports no platform support
  for viewports there, so ImGui clears `ViewportsEnable` on the first frame and
  every window stays inside the main one; `HEDGR_PLATFORM=x11` is the escape
  hatch that gives real torn-out windows. Client-side decorations on Wayland
  need `xdg-decoration` or the client drawing the whole frame and moving itself
  through `xdg_toplevel.move`, neither of which the GLFW backend exposes.
- **The desktop already does this work.** Snapping, tiling, keyboard
  minimise/maximise, the window menu, accessibility and the task switcher all
  come with the decoration the compositor draws. A client-drawn frame has to
  earn each of them back, per desktop, per platform.
- **What the reference's chrome actually does.** Its mode switcher changes what
  the application shows; its window controls act on the OS window. Only the
  first is an application concern.

## Decision

Hedgr keeps the compositor's decorations on its top-level window, and does not
draw window chrome of its own. The reference's title bar is matched **inside**
the window — the menu bar's own strip gains the mode switcher — and its
minimise, maximise and close are left to the desktop, which is where a person
already looks for them and where they behave the way the rest of that session
does.

A frameless window is not refused for ever: it becomes worth revisiting when
Hedgr has a deployment that runs it full-screen or kiosk on one known platform,
where there is no desktop chrome to borrow and the trade is a different one.

## Alternatives

- **Frameless with a painted title bar, on every platform.** Matches the
  reference pixel for pixel and loses the desktop's own behaviour: on Wayland the
  GLFW backend gives no way to move or resize the window, so the frame would be
  a picture of controls that do nothing; on X11 it works and would need a drag
  emulation, a resize border and a per-desktop list of edge cases to feel
  native.
- **Frameless per platform** — decorations on Wayland, a painted frame on X11 and
  Windows. Honest about the constraint, but it is two window systems in one
  application, and the one an operator sees then depends on how they launched it.
  The reference look would also differ between them.
- **A painted bar under the real decoration**, with the mode switcher and
  duplicate window buttons beside the compositor's. Two sets of controls doing
  the same thing, one of them lying when the compositor decides otherwise.
- **Wait for a toolkit that exposes client-side decorations.** ImGui and GLFW
  are the stack; nothing in the dependency set is about to change that, and the
  question is about what to build now.

## Consequences

- The mode switcher — the part of the reference's title bar that is the
  application's business — can be built on the menu bar without waiting for
  anything, and is the next thing drawn from that reference.
- Window controls stay the desktop's: no code, nothing to test headlessly, and
  no platform matrix to keep true.
- The reference's title bar will not be matched exactly. That is the cost, and
  it is paid where the difference is least visible, in the OS window frame
  rather than in the application's own chrome.
- The work list's "window controls" item is closed as decided-against, not as
  deferred: it is not waiting on anything, and a future kiosk deployment is what
  would reopen it.
