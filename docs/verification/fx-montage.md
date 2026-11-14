# The FX montage

## What it answers

`marketdata.v1.ops.get_fx_montage` takes a base currency and answers with that
base's crosses: one row per pair, a spot per provider, the consensus the
deployment derives, and the move since an instant it states once for the whole
answer. The client half is implemented (`session_client::fx_montage`) and is
exercised by the `[.live-montage]` probe.

## What `prime_origin` answers: nothing

Every base currency, on 2026-11-13:

```
AUD -> the deployment refused the tick read (No provider quotes a cross of AUD at this as-of.)
```

That is the deployment's own sentence, and it is true. The instrument catalogue
lists 26 FX pairs, and **every one of them has no observations at all**:

```
kind='pair' name='AUD/USD' type=fx_spot quote=rate ccy=USD pair=AUD/USD stale=1 as_of=0
  source provider='bloomberg' kind='data_vendor' stale=1 age=0s
```

`as_of=0` is the catalogue saying the series has never been observed. The
montage skips a series with no observation, so it finds no cross of anything.

So the FX screens have no data on this deployment — not a client defect and not
a server defect, a seeding gap. The Rates Ladder on the same deployment answers
seven rungs per currency, because the interest-rate curves *are* seeded; the FX
pairs are defined and never priced.

## Why the window is not built yet

The rule for this project is that a screen is testable locally before it is
shown to anybody. A montage window on this deployment would draw its empty state
on every base currency, which tests the empty state and nothing else: the
provider columns, the consensus, the change and the range would all be
unexercised.

The read is therefore in, and the window is deferred until there is data behind
it. What it will need when it is built:

- one row per pair, one column per provider, mid only;
- the decimal places from the pair's own convention (`currency_pair_conventions`
  is already read for the CRM matrix), not from a rule invented here;
- `change_pct`, `day_high` and `day_low` drawn as absent when the deployment
  says absent — a zero change and "no price at the start of the day" are
  different facts and the response distinguishes them;
- the change instant named on the screen, because a move without the instant it
  is measured from is not a number a desk can use.

## Open question for the deployment

Whether the FX pairs are meant to be priced on `prime_origin` at all. If they
are, the seed needs observations for the `bloomberg` pair series; if they are
not, the catalogue's 26 pairs are reference definitions and the FX screens should
say so rather than offering a montage of nothing.
