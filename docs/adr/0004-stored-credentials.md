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

- Encryption is AES-256-GCM under a key derived from the master password, with
  PBKDF2-SHA256 at 600,000 iterations, in the stored form `base64(salt(16) ||
  iv(12) || tag(16) || ciphertext)`. This ADR originally said Hedgr would reuse
  `ores.security::crypto::encryption` for it. That class no longer exists, so
  Hedgr implements the two primitives itself, on OpenSSL, in one file. See
  [ADR 6](0006-hedgr-owns-its-cryptography.md), which amends this one and
  records why the reuse was impossible.
- The master password is never stored. The store proves it by decrypting a known
  blob, exactly as the Qt manager's `verify_master_password` did.
- The store has a locked state and an unlocked state. Locked is a state the
  interface renders, not an error it reports. The whole store is behind the
  password in the interface, so the Instances window is drawn only while the
  store is unlocked and is replaced by the prompt otherwise. The gate is in the
  interface only. On disk only the credential is encrypted, and the catalogue
  (instances with their broker URLs and TLS paths, folders, labels and account
  principals) is stored in the clear. An earlier revision of this ADR said
  the Instances window would work while locked because instances carry no
  credential. The operator chose the whole-store gate instead, because an
  instance list names real hosts and tiers and is not public either.
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

The first launch on a new store has no master password, so the first surface an
operator meets is the create prompt. Startup is gated on it. An operator may
still store no credentials at all, but they set a master password regardless,
because the interface gates the instance catalogue and the folders behind it
too. That gate does not protect the file. A copy of the database reveals the
catalogue without the master password, and only the credentials stay sealed.

Losing the master password loses the credentials, and it locks Hedgr out of the
instances, the folders and the labels, which the interface no longer shows.
Those rows are still in the file in the clear. There is no recovery inside Hedgr
and there should not be one. This is wider than the original decision, which
bounded the loss to the secrets, and it is the price of the whole-store gate.

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
