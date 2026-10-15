# Hedgr context

Glossary only. Implementation decisions live in `docs/adr/`.

## Instance

A deployment of OreStudio that Hedgr can talk to. An instance is identified by
its NATS address and the subject prefix its services listen on. It has a host, a
port, optional TLS material, and labels. An instance is configuration. It does
not change while Hedgr runs, and one Hedgr process holds many.

## Folder

A named container in a tree. Folders nest arbitrarily. A folder belongs to
exactly one tree and holds only that tree's leaves. A folder carries no
configuration of its own.

## Root

The single folder each tree is anchored by. Root has no parent, and it is the
parent of every folder that has no other parent. Root can be selected, and new
folders and entities can be created inside it. Root cannot be renamed, moved, or
deleted.

## Label

A free-text tag attached to an instance or an account. A label has a name, a
colour, and a severity. A label is created the first time it is used, and labels
filter lists.

## Label severity

How a label affects destructive-action confirmation. Severity is none, caution,
or danger. An instance takes the highest severity among its labels, and the
confirmation reads that. This is how a production warning survives without a
fixed set of tiers.

## Account

A login on exactly one instance. An account has a principal, a name, labels, a
tenant, and optionally a stored credential. The same username on two instances
is two accounts.

An account's name is a display string, for example "ops read-only". It is not a
label. A label is an entity with a colour and a severity, attached by a
junction, and the two are never the same field.

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

## Probe

A reachability and readiness check of one instance. A probe opens its own NATS
connection, asks whether the deployment answers, asks whether it has been
bootstrapped, and closes the connection. A probe never uses a session's
connection and never touches a session, so polling every instance cannot disturb
the work an operator has open.

## Reachability

What the last probe found about the broker. Red when no connection could be
opened, green when one could.

## Readiness

What the last probe found about IAM. Ready when IAM reports the deployment
bootstrapped, bootstrapping when IAM reports setup incomplete, and unknown when
the broker answered but IAM did not. Unknown is not bootstrapping. A deployment
whose IAM is down has not said it is bootstrapping, it has said nothing.

## Browser

The widget both the Instances window and the Accounts window are built from. A
browser is a folder tree on the left and a list on the right. The tree is
anchored by Root. The list reports on the selected tree node rather than holding
a second selection.

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
