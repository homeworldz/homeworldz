# ADR 0015: Engine-Independent Physics World Boundary

Status: Accepted

Region simulation talks to physics through `homeworldz::physics::World` rather
than engine types. The boundary covers rigid bodies, characters, impulses,
fixed steps, contacts, ray queries, and capture/restore of transferable body
state. Scene entity IDs link the physics mirror to authoritative region state.

Adapters own engine handles behind project-level numeric IDs. Physics state is
a simulation mirror: capture and restore support evaluation and region handoff,
but identity, persistence, ownership, and asset references remain in the scene.

## Behavioural parity is a goal, not a guarantee

Two engines behaving alike **is** worth aiming for: where there is a choice, prefer
the option that makes them agree. It is not a promise, not a hard requirement, and
never a reason to weaken a test or cripple an adapter to reach a lowest common
denominator (operator, 2026-08-05).

The boundary's purpose is that nothing in the **design** is locked to one engine.
Jolt is possibly the only engine Homeworldz will ever use; supporting a second is a
stretch goal whose value is in keeping the architecture honest. Two different
solvers will still differ on contacts, restitution, character support and slope
handling — divergence is *acceptable* rather than desirable, and worth narrowing
when narrowing it is cheap.

Three consequences worth stating, because each was got wrong before this was
written down:

- **A scenario may target one engine** where a capability is genuinely absent, not
  merely inconvenient. `run_common_scenarios` runs every backend,
  so a test needing a capability only one adapter has does not belong there — it
  belongs in an engine-specific test. PhysX has no `create_heightfield` (the base
  returns 0), which makes a heightfield scenario Jolt-only. That is an ordinary
  fact about the adapters, not a gap to close and not a reason to weaken the test.
- **An adapter may leave a capability unimplemented.** The base `World` gives
  `create_heightfield` a default returning 0 precisely so an adapter can decline.
  Declining is a legitimate state, distinct from failing.
- **What must not vary is the boundary.** The interface, the ID discipline, and the
  capture/restore contract are the things a second engine has to satisfy. Results
  are allowed to differ; the shape of the conversation is not.

This is the physics counterpart to the `SimulatorFeatures` rule already stated in
`physics.h`: behaviour must be announced, never inherited. An adapter that cannot
do something should say so rather than approximate it.
