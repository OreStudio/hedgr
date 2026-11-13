# Market data: what the screens are, from first principles

**Status.** Proposal, 2026-11-13. Nothing here is built. The current browser is
described in the "what is wrong" section so that the reason for replacing it is
on the record rather than implied.

## What is wrong with the current dialog

It is one window doing four jobs, and the row it draws depends on which filter
is set:

- **The row changes meaning with the asset filter.** With `IR` chosen, a row is
  a *curve* - one row for the whole term structure. With `All assets`, a row is
  a *series* - one rung of one curve. So `AUD` in the filter lists `FX AUD/USD`
  and `IR AUD 1D` under all assets, and `0D` and `AONIA` under `IR`. Both are
  true and neither is the same list. A filter must narrow a list; here it
  changes what a list *is*.
- **Curves and surfaces are not in the list at all.** They are separate combos
  above it, so an instrument that is a curve cannot be found by the same search,
  cannot be filtered by currency, and cannot sit beside the FX pairs it belongs
  with. The operator's question - "why are the surfaces and curves not part of
  the overall list" - is the design telling us that they should be rows.
- **Provider is a column, party is invisible, and both decide whether a number
  exists.** Five copies of `AUD-AONIA` differ only by party; three curves called
  `IR AUD IR_SWAP` differ by provider and forward start. A list whose rows can
  be identical strings is a list with no identity.
- **The same instrument is reachable two ways with different names.** A
  published curve opens from one combo, a tick curve from the list, and neither
  says which it is.

## How a trading system works

A desk does three things, in this order, all day:

1. **Name the instrument.** "EUR/USD spot." "AUD swaps, 3M forward start." "EUR
   EST R." "EUR/USD 1Y vol." An instrument is a thing with a name, not a data
   row: it survives its provider changing, its curve being recalibrated, and the
   session changing party.
2. **Look at its values from the sources that carry it.** A monitor - a
   *montage* for FX, a *ladder* for rates, a *surface* for vol - with the
   provider as the column and the tenor or strike as the row. The party is never
   a column: it is who you are.
3. **Look at it over time or in shape.** A chart by time, or a curve/surface
   drawn.

Two verbs, then: **find** an instrument, and **monitor** it. Everything the
current dialog confuses comes from mixing “find” with “monitor”, and “series”
with “instrument”.

## The three nouns

| Noun | What it is | Example |
|---|---|---|
| **Instrument** | What is quoted, independent of who carries it. | `FX EUR/USD spot`, `IR AUD swap`, `IR EUR EST R`, `Vol EUR/USD SSVI` |
| **Source** | Who carries it: a provider, under the session's party. | `bloomberg`, `demo`, `derived`, `ore` |
| **Values** | Where the numbers live: a price, a ladder by tenor, a curve by term, a surface by expiry × strike. | 12 rungs, 7 pillars, 7 × 5 grid |

A `series` is an *implementation detail of a source*: one provider's storage of
one rung of one instrument. It is what the deployment publishes, and it should
not be a word on a screen. A **curve** is an instrument whose values are a shape
across a term axis; a **surface** is one whose values are a grid. Both are
instruments, and both belong in the search.

## The screens

### 1. Explorer (the market data window, rebuilt)

One input, one list. The input is a currency, a pair, a curve name, a provider or
any word in the instrument's name. The list holds **instruments, one row each**,
whatever the facets say.

```
[ aud                                    ]  Assets: All ▾   Providers: All ▾
─────────────────────────────────────────────────────────────────────────────
CURVE    IR AUD swap · 0D            demo, bloomberg · 12 rungs · 2s
CURVE    IR AUD AONIA · 0D           ore · 7 rungs · published 1 day ago
PAIR     FX AUD/USD spot             bloomberg · 1.4s
PAIR     FX GBP/AUD spot             bloomberg · 1.4s
SURFACE  Vol AUD/USD SSVI            refdata · 7 × 5 · nothing published
CURVE    IR EUR EST R                ore · 7 pillars · 1 instant
```

- The **kind badge** is what the old separate combos were for. A curve is a row;
  a surface is a row; a pair is a row.
- **Facets narrow, never change the row type.** `Assets: All` really means all.
- **Currency is the primary search**, and it matches the currencies the identity
  names: `AUD` finds `FX GBP/AUD` from the quote side and every AUD rate.
- Each row's dim text says what makes it that row and no other: providers,
  shape, and freshness. Two rows cannot read the same.

Opening a row opens the screen for its kind:

| Row kind | Opens |
|---|---|
| `CURVE` | Curve Monitor (the term structure and its ladder) |
| `LADDER` (a currency + instrument) | Rates Ladder (below) |
| `PAIR` | FX Montage, or a Chart |
| `SURFACE` | Surface Monitor |

### 2. Rates Ladder (new)

The screen the operator asked for: one row per maturity, one column per provider.
Reached from `Rates → Ladder` and then a currency, or by opening a `LADDER` row.

```
AUD swaps                                      Provider: ▾ (all shown below)
─────────────────────────────────────────────────────────────────────────────
Rung     bloomberg      demo           derived      ore          Age
ON       4.3120         4.3090         -            -            2s
1W       4.3150         4.3120         -            -            2s
1M       4.3300         4.3280         4.3290       -            2s
…
30Y      4.7600         4.7550         -            -            2s
```

- **Rows are maturities**, in the order a trader reads them (ON, TN, SN, 1W…).
- **Columns are providers.** Provider is the source axis, so it is a column and
  not a filter by default; one dropdown can hide the ones you do not care about.
- **Mid only**, bid/ask columns blank, as agreed: the deployment quotes one side.
  If it ever carries two, they become columns here without a new read.
- **A provider's cell is the newest value it gave for that rung** - a tick if it
  ticks, the curve's point otherwise - and the age column says which.
- Every rung is backed by a curve the catalogue already holds, so this screen is
  a read across curves, not a new protocol.

### 3. Curve Monitor (exists, re-dressed)

One curve: the ladder of its own points over the plot, history overlay, and - new
- a **source selector** when more than one provider carries it, instead of
several rows in the search that look identical. The window's title says the
instrument and the source: `IR EUR EST R · ore`.

### 4. FX Montage (new, later)

The pair's siblings in one table - spot, forwards by tenor - with provider
columns, mid only. The FX twin of the Rates Ladder.

### 5. Surface Monitor (exists)

Unchanged: the surface, its axes, its range. The menu goes to it directly.

### 6. Chart

Any single value over time. It stays what it is; it is opened from a monitor
row, not from the search.

## The window menu

The top bar becomes asset-class led, because that is how the work is divided:

```
FX      Rates      Vol      Credit      Search      VIEW      HELP
```

- `FX` opens the FX Montage, `Rates` the Ladder, `Vol` the Surface list,
  `Credit` the CRM matrix - each with its own selector, so "the AUD swap ladder"
  is `Rates → Ladder → AUD`, two clicks and no search.
- `Search` opens the Explorer, which is the way to jump to something you cannot
  name the asset class of.
- The Explorer is therefore a *jump surface*, not the only door to market data.

## What happens to the tree

The Explorer's folder tree goes. Folders are how the operator organises
*workspaces and accounts*; a market instrument is not filed anywhere - it is
named. A tree of three nodes that only mirrors the facets is chrome, and it costs
a third of the window. If grouping is wanted, group the list by asset class with
a count.

## Migration, in verifiable steps

1. **A row is an instrument.** The view yields one row per instrument in every
   view: a curve for rates (never a rung), a pair for FX, a surface for vol.
   Kind badges. The separate curve and surface combos go. *This alone fixes the
   `All assets`/`IR` contradiction and puts curves and surfaces in the search.*
2. **Currency is a first-class filter** over the identity's currencies, not the
   label. (The matching is in; the control and the facets are not.)
3. **Rates Ladder**: currency + instrument, provider columns, mid only.
4. **Menu by asset class**; the Explorer becomes `Search`.
5. **FX Montage**; surface rows join the Explorer from step 1.

Each step leaves a screen that works, and each can be looked at locally before the
next.

## Decisions I need from you

1. **Menu per asset class plus one Search** - my recommendation - or one
   Explorer window with facets and no menu? The menu is how a desk reaches a
   ladder in two clicks; the Search is how it reaches something it cannot spell.
2. **The tree goes.** Confirm, or say what it is for in market data.
3. **Order**: I would do step 1 first (rows as instruments) because it is the
   bug you just hit, then 3 (the ladder) because it is the screen you asked for.
