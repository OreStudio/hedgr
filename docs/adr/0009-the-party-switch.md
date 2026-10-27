# 9. The party switch

**Status.** Accepted.

## Context

A session signs in as one party. IAM issues a token scoped to it, and every
read and every tick the session starts presents that token. The market browser
used to offer a Party combo that only filtered rows: choosing another party
listed its series and then read their ticks with the token of the party the
session still held. The filter was the defect, not the remedy.

The design wants one party at a time per session, with a control that changes
it by calling the instance's party operation, swapping the token, reading the
catalogue again and moving the live stream.

The transport belongs to the session's worker coroutine, not to the frame loop.
The frame loop can push commands and apply events, and that is all it can do;
calling the instance from it would block a frame on a network round trip.

## Decision

Add one small shared object per running session, `party_switch`, alongside the
worker's stop source:

- the frame loop writes the requested party into it and wakes the worker's
  idle wait with a stop source the worker arms before each wait;
- the worker takes the request, calls `session_client::switch_party` (the
  instance's operation for a live session; `select_party` remains the login
  flow's single-use exchange), and reports the answer as an event;
- the frame loop applies the event, replaces the session's token and party,
  reopens the live stream for the new party and reads the catalogue again.

The object is serialized by one mutex and carries no other state. It is a new
shared object in the sense ADR 2 means, which is why this ADR exists.

## Alternatives

- **A filter in the browser.** The status quo: rows for a party the session
  cannot read. It answers the wrong question and leaves the wrong-token read in
  place.
- **A session per party.** Correct, and the larger design: every party needs
  its own worker, token, windows and stream. One party at a time is what the
  operator asked for and what the instance's switch operation supports.
- **Stop and reconnect for the new party.** Needs the password again, since a
  typed credential is not kept, and throws away the rest of the session to
  change one field.
- **Call the instance from the frame loop.** Blocks the frame on a round trip
  and breaks ADR 2's rule that the frame loop never does I/O.
- **A command queue per worker.** The session-actor plan's direction, but that
  is a larger seam than this change needs: one party, one wake, one mutex.

## Consequences

- A party change is applied promptly, not at the next token refresh.
- A refusal leaves the session on its old party and token and raises a
  condition; the control returns to the party the session holds.
- The worker and the frame loop now share one object per session besides the
  stop source. The two are keyed by the same running key and cleared together.
- If a catalogue read is already in flight when the party changes, the new read
  waits for that answer and then starts; the pair is remembered for the
  duration.
