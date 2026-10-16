# 8. Named signals, with sound as one subscriber

**Status.** Accepted. Implemented.

## Context

The operator wants two things that arrive together: a generic way to raise a
named event from anywhere in the application, and a sound played when an event
has one registered against it. The first use named is a deployment changing
state, for example when it stops being ready or drops into bootstrapping.

Hedgr already has an event channel. `model::event::event` carries a closed
`event_kind` from the workers to the frame loop, and it is the mechanism that
keeps the state tables single-writer. That is a fact channel between threads,
and it is not the same thing as what is being asked for here.

Three things are wrong with stretching it to cover this:

- Its kinds are a closed enum in the model, so every new trigger would edit the
  model, which is the one component everything depends on.
- It carries facts about sessions and probes, so a sound would be coupled to
  those two subjects and to nothing else.
- Nothing can subscribe to it. The frame loop drains it and applies it, which is
  a single consumer by construction.

## Decision

**A named signal is a separate thing from a model event.**

A signal is a fact with a name, raised by any part of the application, and the
name is the whole interface:

```cpp
struct signal final {
    std::string name;      // for example "instance.readiness.degraded"
    std::string detail;    // the human sentence, for a status bar or a log
    std::int64_t at_ns = 0;
};
```

- **Names are dotted and lowercase**, `subject.condition.cause`, so that a
  registration can match a prefix and so that a name reads as what happened
  rather than as a code.
- **A signal bus** carries them. It is raised from anywhere and drained by the
  frame loop, so it follows the same discipline as the event channel: a worker
  never calls into the interface, and the frame loop stays the only place that
  acts. The bus holds a `std::mutex` for a swap and nothing else.
- **Signals are raised on a change, not on a poll.** A deployment that is
  unreachable on twenty consecutive probes has changed once. The probe table
  already holds the previous result, so the transition is detected where the
  result is applied.
- **Subscribers are registered against a name.** Sound is the first subscriber
  and it is not special: it reads a mapping from name to a sound file, and plays
  one when it matches. A status bar is expected to be the second subscriber,
  which is why the detail sentence travels on the signal rather than being
  reconstructed by each listener.

**Sound is played off the frame loop.** Mixing audio on the frame loop would
make a sound cost frames. The player owns its own thread, and the only thing
that crosses is the name of a sound to start.

**The backend is miniaudio**, which is in Hedgr's vcpkg at 0.11.25. It is a
single header, has no dependencies, and its licence is public domain, so it adds
nothing to the dependency graph that a sound effect is worth arguing about. The
alternatives are `openal-soft` and `portaudio`, both of which are a library and a
threading model for a job that is "play a short file".

## The sound pack

Kenney's UI and interface packs are CC0 and are mirrored on GitHub, for example
`Calinou/kenney-ui-audio` and `Calinou/kenney-interface-sounds`. CC0 means no
attribution is required, which is why they are the obvious choice.

**Vendor a curated subset, not the pack, and not a submodule.** A pack is dozens
to hundreds of files and the application will use perhaps six. Vendoring those
six under `assets/sounds/` with the licence text beside them keeps the checkout
self-contained, keeps the repository small, and matches how the Font Awesome
face was handled.

A submodule was considered and is the right answer only if the whole pack is
wanted for browsing. It is not, so it would add a clone dependency, a second
place that can fail to initialise, and a hundred unused files, in exchange for
nothing this application needs. If the pack is wanted later, adding it then is a
submodule and a copy step.

## Consequences

**Sounds are registered by default while the application is in development.**
The operator asked for them on rather than off, because a sound is how a state
change is noticed without watching the window, and noticing is the whole point of
raising a signal at all. That is a development decision and it is deliberately a
temporary one: the operator intends to make the registration configurable, at
which point the default becomes a matter of configuration rather than of code.

Until then the mapping stays small, because every sound is a noise heard
repeatedly by everyone in the room and a busy deployment can raise a signal on
every poll where it is failing. A handful of distinct, short sounds is the whole
budget. When the registration becomes configurable, an operator who wants silence
gets silence by configuring it rather than by rebuilding.

The signal names become an interface. Once a name is registered against a sound
or shown in a status bar, renaming it silently breaks that registration, so the
names belong somewhere one can see them, in the way the icons are one file.

A signal that nobody subscribes to is cheap and should stay legal. The
alternative, where raising a signal requires a listener, would make the
framework a coupling rather than a decoupling.

## Alternatives considered

**Extend `model::event::event` with more kinds.** Rejected for the three reasons
above. It would put sound in the model's dependency graph and give the sound a
choice of two subjects.

**A general-purpose publish and subscribe framework with topics and lifetimes.**
Rejected as more than the job needs. A name, a detail and a timestamp, drained
once per frame, covers the requirement, and a bus with subscription lifetimes
would be a framework to maintain rather than a mechanism to use.

**Play sounds from the frame loop with a short non-blocking write.** Rejected.
It works until a sound is long or the disk is slow, and the failure appears as a
stutter in the interface rather than as a missing sound.

**A submodule for the sound pack.** Rejected in favour of a curated subset, for
the reasons given above.
