# Provider kind

**Status.** Built. The browser control ended up in VIEW rather than the filter
row: a new widget there shifted the series-browser layout and broke the
mini-plot icon test's coordinate scan.

## What

OreStudio's `marketdata.v1.providers.list` returns each provider's `code`,
`name` and `kind`. `kind` is one of `data_vendor`, `exchange`, `broker` or
`reference`. Every FX spot is listed twice: an `ore` reference series that never
ticks, and a `bloomberg` (or other data vendor) series that does. Hedgr does not
call the endpoint, so the browser cannot tell the two apart and the CRM's "mini
plots" picker takes whichever sorts first.

The goal:

1. read the provider list once per session and keep it with the catalogue;
2. hide `reference` providers in the market browser by default, with a control
   to show them;
3. make the CRM picker and double-click prefer data-vendor providers;
4. leave the server's eventual `include_reference` policy free to change the
   default later.

## Where it lives

- `market_provider_row` in `hedgr.model/domain/market.hpp`: `code`, `name`,
  `kind`, copied as text. The deployment's own words, no mapping.
- `series_catalogue` gains `std::vector<market_provider_row> providers`.
  The catalogue is already read per session and already drawn by the browser;
  a new state table would be a second owner of the same deployment facts.
- `session_client::list_providers()` pages `list_providers_request` until
  `total` is reached and returns the rows. It is a read on the session's token,
  like the catalogue.
- The market worker's catalogue read fetches the providers in the same job and
  puts them on the `series_catalogue` event. A provider read that fails leaves
  the list empty and logs at warning; the browser then shows every row rather
  than hiding the wrong ones.
- `read_service` copies `ev.providers` into the catalogue when it applies the
  event.

## Browser

- Build the set of `reference` codes from the catalogue's providers once per
  catalogue generation.
- `series_browser_query` gains a borrowed `std::span<const std::string>
  hidden_providers`, and `update_series_browser_view` drops rows whose provider
  is in it before the other filters.
- The filter row gains a "Reference" checkbox, off by default; the hidden set
  is empty when it is on. The Provider combo lists only providers that survive
  the choice.
- A catalogue with no providers (read failed, or an old event) hides nothing.

## CRM

- The picker that opens a chart or mini plot for a pair must choose among
  matching rows. Rank `data_vendor` first, then `exchange`, `broker`, then
  `reference`; within a rank keep the catalogue order. A pure helper so the
  choice is testable.
- The provider kind comes from the session's catalogue providers.

## Tests

- The view drops a hidden provider and keeps the rest.
- The CRM choice prefers a data vendor over an earlier-sorting reference row.
- A catalogue event with providers stores them; an empty one stores none.
- The browser shows no reference row by default and shows it after the toggle.

## Notes

- Providers are system-tenant rows, readable by every tenant; a session of any
  tenant can read them.
- The reference/provider decision on the server is still open; this makes
  `ore` series invisible by default locally, and the server's flag can later
  replace the local default with an authoritative one.
