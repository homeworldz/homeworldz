# HomeWorldz Physics Evaluation

## Context

HomeWorldz is considering a clean architecture where region servers run
distributed simulations with local asset management and central grid services
for users, region registration, presence, and coordination. The region server
language is C++20. Physics still requires an explicit evaluation because the
engine choice affects integration boundaries, determinism, deployment, and the
shape of region handoff.

The primary compatibility target is Firestorm-first viewer behavior, with the
Second Life viewer protocol remaining an important reference point. The physics
engine does not need to duplicate Halcyon's current implementation exactly, but
avatar movement, object interaction, vehicles, scripted forces, terrain
collision, and cross-region behavior must feel coherent to users familiar with
Second Life/OpenSim-style worlds.

See [PLAN.md](PLAN.md) for the broader HomeWorldz implementation plan.

## Evaluation Criteria

The physics engine should be evaluated against the following criteria:

- License compatibility with an open HomeWorldz server.
- Long-term maintenance health and upstream activity.
- Native interop cost from likely host languages.
- Impact on server language and runtime selection.
- Avatar movement, including walking, flying, jumping, sitting, ramps, stairs,
  edge cases, and character/object interaction.
- Terrain, sculpt, mesh, and primitive collision support.
- Dynamic object behavior, including stacking, sleep/wake, collision events,
  mass, friction, restitution, and scripted impulses.
- Vehicle and constraint support.
- Behavior under server tick load and many active objects.
- Determinism or replay stability sufficient for debugging and handoff tests.
- Serialization of physical state for cross-region transfer and crash recovery.
- Observability, profiling, and operational debugging.
- Deployment complexity on Windows and Linux.

## PhysX 5

PhysX 5 is NVIDIA's current open-source physics SDK. It is a natural candidate
because Halcyon historically used a patched PhysX.NET wrapper around an older
PhysX generation.

### Strengths

- Current SDK with active NVIDIA stewardship.
- BSD-3-Clause licensing is compatible with open-source server development.
- Mature rigid body simulation, collision detection, vehicles, constraints,
  character-controller support, and broad production history.
- Good continuity with Halcyon's historical PhysX direction.
- Strong feature ceiling if HomeWorldz later needs advanced simulation features.
- C++ core can be embedded in a native physics worker or wrapped for another
  host language.

### Risks

- Integration weight is significant if the HomeWorldz server is not C++.
- A maintained binding layer would be required for Rust, .NET, Go, or another
  host language unless physics runs as a separate native process.
- Behavior will not automatically match Halcyon's old PhysX 3-era integration.
  Compatibility tuning is still required.
- The SDK may be more complex than HomeWorldz needs for region-scale virtual
  world simulation.
- GPU features should not be assumed for v1 server physics because distributed
  region hosts may not have suitable hardware.

### Best Fit

PhysX 5 is best if HomeWorldz values continuity with Halcyon's PhysX history,
expects complex physical simulation needs, and is willing to own a serious
native integration layer.

## Jolt Physics

Jolt is a modern C++ physics engine designed for games and VR applications. It
is a strong candidate for a clean HomeWorldz architecture because it is
multicore-friendly, permissively licensed, and focused on real-time gameplay
simulation.

### Strengths

- MIT license with low legal and operational friction.
- Active modern C++ project with a practical game-simulation focus.
- Strong multicore design, useful for busy regions with many independent
  objects.
- Character-controller support appears well aligned with avatar movement
  evaluation needs.
- Smaller and easier to reason about than a full PhysX integration.
- Good candidate for a dedicated region physics worker process or native module.

### Risks

- Less historical continuity with Halcyon and Second Life physics behavior.
- Requires compatibility tuning for avatar movement, vehicles, prims,
  constraints, and scripted object behavior.
- Existing HomeWorldz/Halcyon assets and expectations may reveal edge cases that
  Jolt does not match by default.
- Bindings may still need project ownership depending on the final server
  language.

### Best Fit

Jolt is best if HomeWorldz wants a modern, open, game-oriented physics engine
with a lower integration burden than PhysX and is willing to tune behavior
against Firestorm/OpenSim expectations.

## Bullet

Bullet is a long-running open-source physics SDK with a permissive zlib license
and a history in games, robotics, simulation, and virtual world projects.

### Strengths

- Mature and widely known.
- zlib license is open-source friendly.
- Many language bindings and examples exist.
- Familiar to parts of the OpenSim-era ecosystem.
- Useful as a behavioral or implementation reference when comparing alternatives.

### Risks

- Older architecture compared with Jolt.
- Not the strongest greenfield default if HomeWorldz is trying to reduce legacy
  assumptions.
- Quality depends heavily on integration choices and tuning.
- It is likely to offer less upside than Jolt for a new region simulation core.

### Best Fit

Bullet should be treated as a fallback or reference candidate, not the default
HomeWorldz v1 physics choice.

## Havok

Havok is historically relevant because Second Life used Havok server-side for
physics. It remains mature commercial middleware.

### Strengths

- Very mature physics middleware with deep production history.
- Historically close to Second Life server behavior.
- Strong commercial support model for teams that need it.

### Risks

- Proprietary licensing conflicts with HomeWorldz's open architecture goals.
- Commercial terms add cost and distribution constraints.
- It would make community contribution and reproducible open builds harder.
- It risks making the physics layer unavailable to smaller independent region
  operators.

### Best Fit

Havok should be excluded as the default. It should only be reconsidered if
HomeWorldz later makes a deliberate commercial licensing decision.

## Recommendation

Shortlist **Jolt Physics** and **PhysX 5**.

Use Bullet as a fallback reference and exclude Havok from the open-source v1
path. The choice should be made by prototype results, not by preference alone.

The likely decision shape is:

- Pick **Jolt** if it provides acceptable avatar, vehicle, and scripted-object
  behavior with lower integration cost.
- Pick **PhysX 5** if Jolt cannot meet compatibility or stability needs, or if
  PhysX's richer feature set proves important enough to justify the integration
  cost.
- Keep the HomeWorldz physics API independent of either engine so a future
  backend can be swapped without rewriting region simulation code.

## Physics Lab

Before committing the playable region to a physics backend, build a small
standalone physics lab that exercises both Jolt and PhysX 5 against the same
scenario set.

The lab should be independent of the final HomeWorldz server. It can be a native
C++ harness, or a thin host-language harness that calls native engine adapters.
The important requirement is that both physics engines run the same scenarios,
emit comparable traces, and expose the same region-facing API shape.

The shared C++ `homeworldz::physics::World` boundary defines that shape. It
covers body and character creation, impulses, fixed steps, contact retrieval,
ray queries, and capture/restore of transferable body state. Engine adapters
must keep their native handles and configuration behind this boundary.

### Required Scenarios

- Avatar walking on flat terrain, slopes, ramps, stairs, and mesh surfaces.
- Avatar jumping, falling, flying transitions, sitting, standing, and edge
  recovery.
- Character interaction with dynamic objects.
- Terrain collision and large static mesh collision.
- Primitive and mesh object stacking.
- Sleep/wake behavior for quiet and disturbed object piles.
- Scripted impulses, velocity changes, rotation changes, buoyancy-like effects,
  and collision event generation.
- Simple and compound constraints.
- Vehicle-style movement with wheels or equivalent constraints.
- Object crossing from one region boundary to another.
- Serialization and restoration of physical state.
- High-load region tick tests with many inactive objects and a smaller number of
  active physical objects.

### Measurements

Each scenario should report:

- Simulation tick time and worst-frame spikes.
- CPU and memory cost.
- Contact stability and visible jitter.
- Object tunneling or missed collisions.
- Avatar controller correctness.
- State serialization size and restore accuracy.
- Cross-region handoff accuracy.
- Determinism or replay drift over repeated runs.
- Implementation complexity and adapter code size.

### Acceptance Gate

The physics engine decision should not be finalized until both shortlisted
engines have run the lab and the results are documented. A chosen engine should
pass the avatar movement, terrain collision, object stacking, scripted impulse,
and region handoff scenarios well enough to support a first playable
HomeWorldz region.
