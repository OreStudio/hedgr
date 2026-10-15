# 5. Probing never touches a session

**Status.** Accepted.

## Context

The Instances window has to show, per instance, whether the deployment answers
and whether it has been bootstrapped. A probe is a connect and a request. The
operator asked for polling rather than a manual action, so probes run
continuously against every known instance while sessions are open.

That is the risk. A session is a working connection carrying an operator's
requests. A naive probe would reuse the session's client, and a slow probe would
then delay the operator's own work. It would also report the session's health
rather than the instance's, which is a different question.

The runtime has one executor whose size bounds how many round trips can be in
flight, as ADR 2 says. A probe holds a thread for the length of a connect
timeout. Putting probes on that pool spends the session budget on probes.

## Decision

A probe is isolated from every session, in connection, in scheduling, and in
state.

- Every probe gets its own `hedgr.session` client, built for that probe and
  destroyed when it finishes. No probe shares a client with a session, and no
  two probes share one either.
- Probes run on their own executor, separate from the session pool. The probe
  pool defaults to two threads, so a slow probe cannot starve session work and
  at most two connects are in flight at once.
- A probe never reads or writes the session table. It receives the instance's
  connection settings as a copy, and it posts its result as an event. The rule
  from ADR 2 still holds, so the frame loop stays the only writer.
- Probe results never change a session's status. A green probe says the
  deployment answers. It says nothing about whether an operator's sign-in still
  works.
- Each instance polls on its own cadence, and a failure backs the next attempt
  off. An unreachable deployment is not retried every tick.
- A probe is cancelled when its instance is removed or the application stops.

## Consequences

Probe results are runtime state, not configuration, so they are not persisted.
They need somewhere to live that the frame loop can read every frame.

The clean answer is a small parallel table holding one row per instance: its
reachability, its readiness, when it was last probed, and the last error. That
keeps `instance` a cold value type, which is what makes it cheap to load and
store.

That table indexes by instance, which means `instance_table` needs stable
indices. Today it is a plain vector and removal compacts it, so a probe result
would follow the wrong row after a delete. `instance_table` therefore moves onto
the same slab and generation-tagged handle the session and window tables
already use. This is the concrete cost of parallel probe state, and it is worth
paying once, before the probe table exists.

Two threads against many instances means a full sweep takes several rounds. With
a connect timeout of a few seconds, twenty unreachable instances take tens of
seconds to settle. That is acceptable because the status is a glance rather than
a gate, and the operator can refresh a single instance by hand.

## Alternatives considered

**Probe on demand only.** Rejected by the operator. A list whose status is stale
until you ask is a list that lies by omission.

**Probe on the session executor.** Rejected. A probe occupies a thread for a
connect timeout and consumes the budget ADR 2 sized for the operator's own
round trips.

**Reuse a live session's client when the instance has one.** Rejected. It
disturbs the session it measures, and it reports the session's health rather
than the deployment's.

**One client for all probes, reused.** Rejected. It serialises every probe
behind every other and mixes their failures together.

**A TCP connect or an ICMP ping instead of a NATS connect.** Rejected. An open
port is not a working broker, and readiness needs IAM regardless, so a cheaper
liveness check would still be followed by the expensive one.

**Store the status in the instance row itself.** Rejected. It would make a cold
configuration type into something written on every poll, and it would persist a
fact about a moment into a file that describes configuration.
