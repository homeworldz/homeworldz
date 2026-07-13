# ADR 0015: Engine-Independent Physics World Boundary

Status: Accepted

Region simulation talks to physics through `homeworldz::physics::World` rather
than engine types. The boundary covers rigid bodies, characters, impulses,
fixed steps, contacts, ray queries, and capture/restore of transferable body
state. Scene entity IDs link the physics mirror to authoritative region state.

Adapters own engine handles behind project-level numeric IDs. Physics state is
a simulation mirror: capture and restore support evaluation and region handoff,
but identity, persistence, ownership, and asset references remain in the scene.
