# The party a session acts as

## What changed

A sign-in now lands in the party the operator chose last time, and a login that
has never chosen is asked once, straight after signing in.

The deployment signs a login in as whatever its default is. On `brave_hopper`
that used to be a system party: `tenant_admin@acme_corporation` may act as five
parties and signed in on System Party, so every window read System Party's data
as though it were the desk's. The confusion this caused was never a data
problem; it was a session acting as a party nobody chose.

## What a party choice is stored as

`account.party_id`, added by store schema version 9. It is a choice, not a
secret, so it sits on the row a list reads and the sign-in prompt can say where
the session will land before it signs in. Empty means nothing has been chosen,
which is what makes a new account ask.

A version 8 store is migrated forward by one `ALTER TABLE account ADD COLUMN
party_id TEXT NOT NULL DEFAULT ''`, in one transaction, and every existing row
reads back as an account that has no party yet. A version below 8 is refused
rather than guessed at, as the store's version policy says.

## Where the choice is made

1. **After the sign-in answers.** A login holding more than one party and
   remembering none shows a prompt: the title "Act as a party", one row
   "Acting as <combo>" seeded on the party IAM signed the login in as, and two
   buttons "Continue" and "Not now". Nothing else: the explanation of what a
   party decides and that it is remembered lives on the combo's tooltip, where a
   reader who wants it finds it. The prompt is drawn after the windows the
   sign-in opened, because at the moment Sign in is pressed the parties are not
   known - they arrive with IAM's answer.
2. **From a window's foot.** Every market-data window already carries an "Acting
   as" control; that path is unchanged and writes the same remembered party.
3. **Next run.** The runtime moves the session to the remembered party as soon as
   IAM answers and before anything in that run reads. A remembered party the
   login may no longer act as is not asked for: the runtime logs it and stays on
   the login's default, so a party removed on the deployment does not produce a
   refusal on every sign-in.

The choice is written when the instance *confirms* the party change, never when
it is asked for, so a refused party never becomes the remembered one. Choosing
the party the session already acts as is treated as a confirmation: it is
remembered without a round trip to the instance.

## What the operator sees

- A login with one party is never asked: the deployment's answer is the only
  answer.
- An account that has chosen is never asked again, because the runtime moves it
  to that party.
- "Not now" is an answer for the run. The prompt says the party can be changed
  later from a window's status bar.

## Which party call a fresh login accepts

The transport's own docstring said a multi-party login signs in with a single-use
token that only `select_party` may present. Measured against `brave_hopper` on
2026-11-13, the opposite is true: `select_party` refuses a fresh login's token
("Invalid or expired token", `authentication_rejected`) and `switch_party`
accepts it. The session actor has always used `switch_party`, which is why the
party control works; `hedgr.session.tests "[.live-party]"` checks both calls
against the deployment and prints which was accepted, so the docstring can be
corrected against evidence rather than argument.

## The prompt's first bug, and the log that found it

The prompt re-seeded its combo from the session's own party on every frame, so
the operator's selection snapped back to the deployment's default before it could
be confirmed - the "combo box always goes back to system party" report. It is
seeded once now, and a test draws four frames after moving the selection and
checks it stayed.

Nothing about any of this was in the log, which is what made the report hard to
act on. Three lines now appear at INFO or WARN, and every refusal reaches the log
whether or not it reached the bar:

- `Asking account '<id>' on '<instance>' which party to act as: the login holds
  N and none is remembered` - the prompt opening;
- `Session '<instance>' / '<account>' asked to act as party '<id>', from
  '<current>'` - the runtime taking the ask, and either the actor's `now acts as`
  or its `was refused` line following it;
- `Condition raised (<source> '<subject>'): <message>` - every refusal, logged
  where the bar's red is raised.

## Verified

- `hedgr.store.tests`: a version 8 store migrates with its instance and account
  rows intact, the account's party reads back empty, and `set_party` writes and
  reads a choice; a version 7 store is refused unmigrated and keeps its stamp; a
  version 10 store (this build's future) is refused.
- `hedgr.model.tests`: the serialised party list is read as ids and not as names,
  and an empty list is zero parties rather than a login with none.
- `hedgr.runtime.tests`: a sign-in whose account remembers a party asks for it;
  one that remembers none asks for nothing; a remembered party the login no
  longer holds is not asked for; a confirmed party change is remembered and
  re-reads the catalogue; choosing the party already held remembers it without
  asking the actor.
- `hedgr.ui.tests`: a multi-party session with no remembered party needs the
  question; a single-party session does not; an account that has chosen does
  not; a session that has not signed in does not; the prompt lists the parties,
  starts on the acting one, declines for the run, and stays away once the account
  remembers a party.
- Against `brave_hopper` on 2026-11-13: `hedgr.session.tests "[.live-shape]"`
  prints the party count and every party name for a principal, which is how the
  five-party login and its System Party default were confirmed.
