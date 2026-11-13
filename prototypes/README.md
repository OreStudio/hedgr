# Prototypes

Throwaway HTML mockups of screens, drawn to agree what a screen *is* before it is
built in ImGui. Each one answers a question about layout, vocabulary or flow; none
of it is shipped, and nothing here is a source of truth for behaviour - the code
and `docs/` are.

| File | Question it answers |
|---|---|
| `aud_aonia_curve_dialog_prototype.html` | What an IR curve screen is: pillars, bootstrapped points, zero rate / implied forward / discount factor, conventions, staleness |

## Capturing one

```
prototypes/capture.sh prototypes/<name>.html        # writes <name>.png beside it
```

The script installs Playwright under `build/proto-tools` on first use (a
developer aid, not part of the product) and launches the system Chrome by
channel. It was written that way because the machine's Playwright browser cache
holds a build this Playwright release does not expect, and `channel: 'chrome'`
sidesteps the download.

The HTML is self-contained apart from Tailwind and Google Fonts, which are loaded
from the network. A capture on a machine with no network gets the layout without
the styling; keep a PNG checked in beside each prototype so the design can be
read offline.

## Adding one

1. Name the file for the instrument and the question: `rates_ladder_providers.html`.
2. Use the same chrome as the existing prototype - title bar with the instrument
   and its flag, a conventions/meta row, the table, the chart, a foot with the
   session's identity - so the screens can be compared side by side.
3. Capture it and commit both the HTML and the PNG.
4. Say in `docs/plans/market-data-ux.md` what the prototype establishes and what
   it costs, so the plan moves rather than the mockup standing alone.
