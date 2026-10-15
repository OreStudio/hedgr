# 6. Hedgr owns its cryptography

**Status.** Accepted. Amends [ADR 4](0004-stored-credentials.md).

## Context

ADR 4 said Hedgr would encrypt stored credentials with
`ores.security::crypto::encryption` rather than writing a second
implementation. That premise turned out to be false.

OreStudio deleted the class on 26 September 2026 in `9a00394350`, "[security]
Delete the dead encryption surface and the umbrella stub". The commit message
gives the reason: it was the component's only AES-256-GCM and PBKDF2 surface,
274 lines across two files, and no file anywhere in the tree included it. Its
own test was its only consumer. It is on no branch, and the pre-built
`libores.security.so` that Hedgr links exports none of its symbols.

So there is nothing to reuse. The choice ADR 4 assumed it was making was not
available.

## Decision

Hedgr implements the cryptography it needs, in `hedgr.store/crypto/secret_box`,
on top of OpenSSL's EVP API.

- PBKDF2-SHA256 at 600,000 iterations for the derivation, then AES-256-GCM
  through `EVP_CIPHER_CTX`.
- The stored form is base64 of a 16-byte salt, a 12-byte iv, a 16-byte tag and
  the ciphertext, which is byte-for-byte the format the deleted class used. A
  store written today opens if OreStudio restores its class, and the reverse.
  That compatibility is deliberate and is the reason for a format a reader would
  otherwise find arbitrary.
- A fresh salt and a fresh iv per seal. An iv is never reused.
- The tag is verified by `EVP_DecryptFinal_ex`, which compares in constant time.
  No hand-written comparison.
- Every intermediate, including the derived key, is cleansed with
  `OPENSSL_cleanse` before it leaves scope.
- All of it lives in one translation unit. That file is the seam, so replacing
  it with a call into a restored OreStudio class later is a local change.

This is not inventing cryptography. It is calling OpenSSL for two standard
primitives, which is what the deleted class did.

## Consequences

Hedgr carries roughly a hundred lines of security-sensitive code that it would
rather not have carried. It is confined to one file with the format and the
cleansing rules written down beside it.

The dependency on OreStudio's security component goes away, and with it the
requirement to rebuild OreStudio before every Hedgr build that touches the
store. Hedgr already links OpenSSL directly, so no new dependency appears.

If OreStudio ever restores its class, the swap is a change to one file and a
change to one line of `CMakeLists`. The byte format was chosen so that no
existing store has to be migrated.

## Alternatives considered

**Restore the class in OreStudio and link it.** Rejected. It would put back a
public surface OreStudio deleted on purpose, for a single consumer in another
repository, and it would make every Hedgr build depend on an OreStudio rebuild.
The code belongs in the repository that needs it.

**Use a third-party crypto library.** Rejected as unnecessary. OpenSSL is
already a dependency, and adding a second one for two primitives would be a
larger surface, not a smaller one.

**Stop and leave unit 4 unbuilt.** Rejected. Nothing about the blocking finding
changes what the unit is for. Only the source of two primitives changed.
