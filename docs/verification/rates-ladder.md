# The rate ladder

## What it answers

`marketdata.v1.ops.get_rate_ladder` returns one currency's ladder across its
term axis. Measured against `prime_origin` on 2026-11-13, for `AUD discount`:

```
AUD discount -> 7 rungs, 1 providers, conventions 'RATES_SPOT_FORWARD' / 'A365F'
  3M   2027-02-14 yf=0.2521 consensus=0.042931 (median, 13)
  6M   2027-05-14 yf=0.4959 consensus=0.042934 (median, 13)
  12M  2027-11-14 yf=1.0000 consensus=0.042824 (median, 13)
  2Y   2028-11-14 yf=2.0027 consensus=0.042551 (median, 13)
  5Y   2031-11-14 yf=5.0027 consensus=0.041395 (median, 13)
  10Y  2036-11-14 yf=10.0082 consensus=0.040256 (median, 13)
  30Y  2056-11-14 yf=30.0219 consensus=0.034178 (median, 13)
```

Every currency with a discount curve answers the same shape: 7 rungs, the same
dates, `RATES_SPOT_FORWARD` and `A365F` stated rather than assumed. The dates
and year fractions are the deployment's, which is the point — a client that
derived a maturity from `10Y` would be inventing a calendar.

## The problem: thirteen quotes from one provider

`provider_count` is **1**, and every rung carries **thirteen** quotes, all with
`provider_code = "ore"`:

```
quotes: ore=0.043015(11078s) ore=0.042597(10301s) ... ore=0.042315(401s)
```

The ages run from 401 s to 11 078 s in steps of about 900 s: those are
observations of the same series over the last three hours, not thirteen
providers. The contract says a provider that does not quote a rung has no
entry, which makes one entry per provider the shape a screen can draw as
columns. As implemented, the ladder returns one entry per *observation*.

Two consequences:

1. **The consensus is not what its name says.** It is documented as "the value
   the server derives across the quoting providers" and states `median` over
   13 — but those 13 are one provider's recent history. A desk reading
   `median (13)` would believe eight providers agreed.
2. **The screen cannot draw one column per provider.** It would draw one column
   called `ore` holding thirteen values, and choosing one of them is the exact
   client-side reduction this client is not allowed to make.

This is a server-side question, not something to paper over in the window: either
the operation returns the newest quote per provider (which is what `provider_count`
and the "one column per provider" design assume), or it says in the response that
it is stating observations and names the reduction it applied. Until it is
settled, the window draws the deployment's consensus column, and a provider
column only when that provider contributed a single quote — a rung whose
provider contributed many shows the count rather than a number the client chose.

## What it costs a client

- `maturity_date` and `year_fraction` arrive computed, so the screen never owns a
  calendar or a day count.
- `tenor_convention` and `day_count_convention` travel with the rungs, so the
  conventions bar the design asks for is a read of the response, not a second
  call and not an assumption.
- Rungs arrive in maturity order, so no tenor ranking happens in the client.
