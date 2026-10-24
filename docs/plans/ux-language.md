# UX language

A window's body says one of four things when it has nothing to draw. Each is a
mark, a tooltip and a foot word, and the meaning is defined once, in
`projects/hedgr.ui/include/hedgr.ui/widget/view_state.hpp`. This page is the
same table for readers outside the code.

## The table

| State | Meaning | Mark | Foot word |
|---|---|---|---|
| none | There is content. | The content. | The window's own, such as LIVE. |
| reading | A connection or a read is in flight. | A spinner. No words. | The updating note. |
| not_connected | There is no usable session: never signed in, no session bound, disconnected, the login session ended, or signed in with no token. | Three broadcast arcs and a dot with a slash through them. The dimmest grey. | OFFLINE |
| error | The session is up or tried, and something failed: a failed connection attempt, a refused or undecodable read, history unavailable, a series not in the catalogue, a call nobody answered. | A ring with an exclamation mark. One grey step brighter than the others. No red. | ERROR |
| no_data | Connected, no errors, and what was read is empty: a series with no points, a CRM with no rates, a curve with no points, an empty list, no match for a filter, nothing installed. | An empty table: a rounded frame cut into a grid of cells, the shape of the Fluent `table` icon. The dimmest grey. | NO DATA |

The marks differ in shape, not in colour alone. Not connected and no data share
the dim grey, and the error mark is the only one in the brighter grey.

## Tooltips

The words live in the tooltip over the mark and nowhere else in the body.

| State | Opening sentence | Second line |
|---|---|---|
| not_connected, never signed in | Not connected | Sign in from the Accounts window. |
| not_connected, the session ended | This session has ended | Sign in again from the Accounts window. |
| error, the connection failed | Connection failed | The session's last error. |
| error, a read failed | Could not read the data | The read's failure text. |
| no_data | No data | What is empty, in the window's own words. |
| reading | none | none |

On the foot, the error's reason is the tooltip over the word ERROR.

## The code

- `view_state_of(view_facts)` is the only judge. It is pure, takes plain values,
  borrows its text, and allocates nothing. The session is judged first, then the
  window's own read, then its failure, then whether it holds data.
- `session_view_facts(input, handle)` fills the session half of the facts from
  the window's binding. The window adds what it reads: `reading`,
  `read_failure`, `has_data` and `empty_what`.
- `draw_view_state(verdict, min, max, glyph_size)` draws the mark and the
  tooltip in any area.
- `draw_view_state_body(status, foot, verdict)` fills a window's body above its
  foot and sets the foot word. `draw_view_state_pane(verdict)` fills a pane with
  no foot. `draw_view_state_inline(verdict, size)` draws in a box, for a popup.
- `draw_session_state(input, handle, status, foot)` is the one call every data
  window makes when its session is not usable.
- `set_view_foot(foot, verdict)` and `view_foot_word(state)` are the one table of
  foot words.
- The colours are `view_quiet_colour` and `view_error_colour` in `theme.hpp`.

## The rule

A new window does not write its own state sentences. It calls `view_state_of`
and `draw_view_state_body`, or `draw_view_state` inside its own area.

Short inline text stays only where a mark would be wrong: a count or a hint
beside a control, a form's validation message, and a developer diagnostic. A
sentence that says the whole body is loading, empty, failed or signed out is not
one of those.

## What a window passes

| Window | reading | read_failure | has_data / empty_what |
|---|---|---|---|
| Chart, Mini plot | The series' first read is outstanding. | The series row's failure. A card whose series is not held says it is not in the catalogue. | The series has points. |
| Cross rates | No batch yet, or a read in flight with no cells. | The batch's error. | The batch has cells. No CRM chosen and a CRM with no pairs are no data. |
| Term structure | No curve yet, or a read in flight. | The curve's error. | The curve holds curves. |
| Market data | The catalogue is unread or reading, and holds no rows. | The catalogue's error. | The catalogue has rows. Filters that match nothing are no data. |
| Command centre | The deployment has not been read. | While reading X: the failure. | The roster has tenants. |
| Workspace | none | none | A workspace with no session has ended. |
