# 4. Stored credentials behind a master password

**Status.** Accepted. Supersedes [ADR 3](0003-a-store-without-credentials.md).

## Context

ADR 3 decided the store would hold no credential at all. The three-window design
changed the requirement. The Accounts window is where an operator keeps the
logins for one instance, and the operator expects to open a workspace without
retyping a password for every account on every instance. The previous Qt client
did store them, encrypted, so the friction would be a regression.

The value of ADR 3 was that the local database was worth nothing to an attacker.
That value is given up here deliberately, and the mitigation is that the file is
useless without a secret that is never written down.

## Decision

An account may carry a stored credential. It is encrypted, and it is the only
secret in the file.

- Encryption is AES-256-GCM under a key derived from the master password. Hedgr
  uses `ores.security::crypto::encryption` rather than writing its own. That is
  the in-house implementation, it is already PBKDF2-SHA256 at 600,000
  iterations, and its stored format is `base64(salt(16) || iv(12) || tag(16) ||
  ciphertext)`. Rolling a second implementation would be the mistake.
- The master password is never stored. The store proves it by decrypting a known
  blob, exactly as the Qt manager's `verify_master_password` did.
- The store has a locked state and an unlocked state. Locked is a state the
  interface renders, not an error it reports. The Instances window works while
  locked, because instances carry no credentials.
- Unlock runs as a coroutine on the runtime's pool. Key derivation is slow on
  purpose, and running it on the frame loop would stall the interface for the
  length of the derivation. This is the same rule as every other slow call.
- A decrypted secret exists only for the length of the sign-in attempt. The
  buffer is cleansed with `OPENSSL_cleanse` before it goes out of scope, and it
  is never logged, never placed in an event, and never written to the database
  in the clear.
- The schema gains a `credential` column on the account and a `master` table
  holding the KDF parameters and the verification blob.

## Consequences

`hedgr.store` gains a dependency on `ores.security`. The dependency graph stays
one way, and the component still talks to SQLite directly.

The first launch on a new store has no master password. The interface must offer
to create one, and must also allow running with no stored credentials, because
an operator who only reads from development instances should not be forced into
a secret they will lose.

Losing the master password loses every stored credential. There is no recovery
and there should not be one. The instances, folders and labels survive, so the
loss is bounded to the secrets.

Key derivation cost becomes a real number in the budget. Unlocking on a slow
machine takes a noticeable fraction of a second, and the interface must show
that it is working rather than appear frozen.

## Alternatives considered

**Keep ADR 3 and store nothing.** Rejected by the operator. The friction of
retyping a password for every account on every instance is what the Accounts
window exists to remove.

**Store credentials unencrypted.** The old client allowed this when the master
password was left blank. Rejected as a default. Plaintext secrets in a file the
operator will copy between machines is not a default worth shipping.

**Auto-unlock from an environment variable.** The old client read
`ORES_CONNECTIONS_MASTER_PASSWORD`. Rejected as a default, because it puts the
secret in the process environment where every child process inherits it and
where a process listing can expose it. It may return as an explicit opt-in for
automated runs.

**Delegate to the operating system keyring.** Deferred rather than rejected. It
is the right answer on a workstation and a portability problem across the three
platforms Hedgr targets. The encryption seam is narrow enough to add it later.

**One password per credential.** Rejected. N prompts for N accounts is the
problem this decision removes.
