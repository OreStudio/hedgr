# Hedgr context

Glossary only. Implementation decisions live in `docs/adr/`.

## Instance

A deployment of OreStudio that Hedgr can talk to. An instance is identified by
its NATS address and the subject prefix its services listen on. It has a tier, a
host, a port, optional TLS material, and labels. An instance is configuration.
It does not change while Hedgr runs, and one Hedgr process holds many.

## Tier

The safety classification of an instance: production, staging, or development.
Tier is the axis every destructive-action confirmation reads. It is never
inferred from an instance's name or its folder.

## Folder

A named container in a tree. Folders nest arbitrarily. A folder belongs to
exactly one tree and holds only that tree's leaves. A folder carries no
configuration of its own.

## Label

A free-text tag attached to an instance or an account. An instance or an account
carries any number of labels. A label is created the first time it is used, and
labels filter lists.

## Account

A login on exactly one instance. An account has a principal, a label, labels, a
tenant, and optionally a stored credential. The same username on two instances
is two accounts.

## Credential

The secret that authenticates an account. A credential is either stored
encrypted under the master password, or typed for a single sign-in. In memory it
exists only for the length of the sign-in attempt.

## Master password

The single secret that encrypts every stored credential. It is never itself
stored. Hedgr holds it in memory only while the store is unlocked, and asks for
it again on the next launch.

## Session

A live connection from one account to one instance. A session is keyed by the
pair, so reconnecting resumes the same session rather than creating a second
one. A session owns its transport, its bearer token, its tenant and party
selection, and its last error.

## Reachability

Whether a NATS connection to an instance can be opened. Reachability is red when
the broker does not answer, and green when it does.

## Readiness

Whether an instance has been bootstrapped. Readiness is bootstrapping while IAM
reports that setup is incomplete, and ready once it is complete. An instance can
be reachable and still bootstrapping.

## Browser

The widget both the Instances window and the Accounts window are built from. A
browser is a folder tree on the left and a list on the right. The list reports
on the selected tree node rather than holding a second selection.

## Instances window

The root surface. It manages every instance and the folder tree they sit in. It
holds no credential. It is always open, because it is the only way to open
anything else. Closing it closes the windows it opened.

## Accounts window

The accounts for one instance. One Accounts window exists per instance, and
several can be open at once. It carries the sign-in action that opens a
Workspace.

## Workspace

The live session for one account on one instance. A Workspace shows what that
account's permissions allow. Its title is the principal and the instance.
Several can be open at once.

## Window

A floating, movable panel that shows one thing. Each window has a stable id, a
kind, and a piece of kind-specific state. Windows are opened from the Instances
or the Accounts window and closed independently. A window outlives the session
it points at, and renders the session-loss state rather than closing.

## Window kind

The discriminator that selects a window's renderer and the shape of its state.
Adding a kind adds a variant alternative, a renderer entry, and a menu entry,
and changes nothing else.

## Handle

A stale-safe reference to a row in a state table, made of an index and a
generation counter. Handles replace pointers between state rows.
