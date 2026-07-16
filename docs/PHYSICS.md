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

### Mesh collision representation

Rendering meshes do not double as implicit collision meshes. Mesh assets carry
or reference a portable, validated collision source from which each physics
adapter builds a native cached shape. Static scene geometry may use a triangle
mesh. Dynamic objects use convex hulls or compounds of convex hulls, while
basic prims continue to use native analytic shapes.

Cylinders preserve their round circumference as a physical property, not only
as rendering geometry. This is essential when a short, wide cylinder is used as
a wheel or roller. Jolt uses its native analytic cylinder. A backend without an
analytic cylinder must generate a convex cylinder (or an equivalently round
portable shape); it must not substitute a box merely to preserve the object's
axis-aligned bounds. Rounded end-cap error is preferable to losing the rolling
circumference when an approximation is unavoidable.

### Halcyon prim-meshing reference

Halcyon's PhysX integration is a useful behavioral and implementation
reference, but it did not rely on a fixed bundle of pre-cooked shapes. Its
`ShapeDeterminer` selected native PhysX geometry only for an unmodified box or
a uniformly scaled sphere. Other parametric prims passed through
`Meshmerizer`/`PrimMesher`; dynamic objects were then cooked as a single convex
hull or a decomposition of convex hulls, while suitable static objects could
use triangle meshes. High-detail circular profiles used 24 sides. Generated
shapes were cached by a hash of the prim parameters and scale.

HomeWorldz can reuse that architecture and use the BSD-3-Clause Halcyon source
as a compatibility reference when implementing its C++ portable prim mesher.
The authoritative output should be engine-independent vertices and hulls.
PhysX 3 cooked data must not be copied as an asset because cooked formats are
engine- and version-specific; Jolt and PhysX 5 should independently cook and
cache the same portable source. Any directly ported Halcyon code must retain
its required copyright and license notices.

Animated, skinned, and deforming meshes use an immutable collision capture for
each instantiated object. Rigid-body transforms move the capture, but visual
vertex deformation does not rebuild it every frame. Attachments are
non-colliding by default; a future collision-enabled attachment would rigidly
carry the same captured representation. Physics caches are keyed by source and
engine version and remain disposable so Jolt and PhysX can prepare the same
authoritative content independently. See
[`ADR 0023`](adr/0023-portable-mesh-collision-representations.md).

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

## Selected Backend

The comparison gate selected **Jolt 5.5** for HomeWorldz v1. Both engines passed
the shared Windows and Linux scenarios, but Jolt had the lower total suite CPU
cost on both measured platforms, the stronger Windows tick results, and a
smaller packaging surface. The PhysX adapter remains in the lab as a working
alternative. See [ADR 0002](adr/0002-physics-evaluation.md) and the
[recorded results](PHYSICS_RESULTS.md).

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

### Mass and contact materials

The authoritative scene records shape, scale, material, and eventually an
optional per-object physics-density override. It does not implement special
avatar-push forces or calculate collision responses. The physics integration
converts shape volume and density to body mass, supplies mass and the contact
properties to Jolt, and lets the engine calculate inertia and collision response.

HomeWorldz follows [Second Life's legacy `PRIM_MATERIAL_*` defaults](https://wiki.secondlife.com/wiki/LlSetPhysicsMaterial): all eight
materials initially have density 1000 kg/m3, while their friction and
restitution differ. Stone, metal, glass, wood, flesh, plastic, rubber, and the
deprecated light material therefore do not have different default weights.
Distinct object weights belong to the explicit physics-density property used by
Extra Physics and `llSetPhysicsMaterial`, whose SL-compatible range is
1-22587 kg/m3. Adapter-level mass limits protect solver stability.
The viewer-facing Extra Physics block is persisted with each scene object:
physics shape type, density, friction, restitution, and gravity multiplier.
Density, friction, and restitution are applied when creating the physics body;
the production Jolt adapter also applies gravity multiplier through Jolt's
per-body gravity factor. PhysX parity for per-body gravity scaling remains
pending. `PhysicsShapeType.None` removes the object's collision body while
leaving the scene object itself present.
HomeWorldz configures Jolt to average the two bodies' restitution values, which
matches PhysX's default combination policy. Jolt's native maximum-value policy
made a wood prim with restitution 0.5 rebound from zero-restitution terrain as
if the complete contact also had restitution 0.5; averaging makes that contact
0.25 while preserving 0.5 for wood against wood.
Dynamic viewer updates include the physics body's authoritative linear velocity
alongside position and rotation. This lets Firestorm dead-reckon between the
region's 10 Hz object updates instead of easing each update from an incorrectly
reported zero velocity, which otherwise makes the last part of a fall look
artificially cushioned.
Live Firestorm acceptance on 2026-07-15 confirmed that streaming authoritative
linear velocity removed the pronounced slow, sponge-like deceleration at the
end of a physical prim's fall.

This distinction is also the basis for vehicles: scripted forces, impulses,
motors, and constraints act on physics-engine mass and inertia rather than a
parallel approximation in region or script code.

Jolt's virtual-character controller separates character mass from maximum
horizontal contact force: mass contributes downward force when standing on an
object, while the force ceiling bounds how strongly a non-rigid character can
affect horizontal contacts. HomeWorldz derives that ceiling from character mass
and a configured maximum push acceleration rather than adding synthetic
region-side impulses. The initial 70 kg character and 30 m/s2 ceiling produce a
maximum 2100 N contact force. In the adapter regression scenario this moves a
125 kg sliding cube about 5 cm, while a 5000 kg body moves less than 0.002 mm.
Jolt still solves the response from object mass, friction, inertia, and contact
geometry. A future region option may disable avatar-driven dynamic-object
motion, and a rigid-body character remains a possible evaluation path.

Static and dynamic obstacles take different stair paths. A low static obstacle
within the avatar's step height uses Jolt stair walking. A dynamic body remains
in the ordinary contact solver instead of being automatically climbed, allowing
mass-relative force transfer before the avatar can pass it. This mirrors the
practical Halcyon distinction between traversable steps and blocking or
movable physical objects without placing object-specific logic in the region.
Live Firestorm acceptance on 2026-07-15 confirmed that a 0.5 m physical cube
can be pushed roughly a metre by sustained avatar contact without being
launched or destabilized, while a 1 x 1 x 1 m physical cube remains effectively
stationary and causes the avatar to bounce off or walk around it.

Viewer editing and grabbing have separate physics behavior. Selecting an owned,
modifiable object temporarily mirrors it as a non-dynamic body, so it cannot
fall or react to collisions while its edit controls are open. Enabling Physical
during that edit records the new state but defers dynamic activation until
deselection; selecting an already-Physical object follows the same rule.
`ObjectGrabUpdate` mouse drags apply a bounded,
mass-scaled impulse through the physics interface toward the viewer's grab
target. The controller converts Firestorm's object-local initial grab offset
to world space on every update, so it moves the originally clicked point rather
than incorrectly pulling the object origin to that point. It uses the same
shape-volume mass supplied to the physics body; using box mass for a pyramid,
for example, would overdrive its one-third-box-volume body by a factor of three.
This explicit grab controller is independent of ordinary avatar contact,
which remains entirely within Jolt's character/body solver.
Live Firestorm acceptance on 2026-07-15 confirmed all three behaviors: an
edited physical prim remained suspended until its edit form closed, mouse-hand
dragging moved it through the grab controller, and resizing Dynamic4 to
1 x 0.5 x 0.75 m made ordinary avatar collisions move it as expected from its
reduced mass.
The Physical-during-edit transition passed live acceptance on 2026-07-16: the
prim remained suspended when Physical was enabled and began falling only after
the edit selection closed.

Before a Physical object is reactivated on edit deselection, the region tests
the bottom of its rotated world-space bounding box against the highest terrain
sample beneath that box. A penetrating object is raised to terrain plus a small
clearance and its stale velocity is cleared before its dynamic body is restored.
This prevents rotation or resizing in the non-dynamic edit state from waking a
body embedded in terrain and provoking a violent solver correction.

An initial pyramid mouse-drag test on 2026-07-16 exposed both grab-controller
errors above: the small pyramid gained excessive momentum and could move away
from the drag direction. The controller correction has automated coverage and
awaits repeat live acceptance with a rotated pyramid.

The Phase 1 region has no neighbors on any border. Physical body origins are
therefore constrained to `0..256` on X and Y, with velocity that still points
out through a crossed edge cancelled. Avatar movement already applies the same
single-region bounds. Once neighbor discovery exists, an accepting neighbor at
that border changes the response from containment to a crossing handoff; a
missing neighbor continues to contain the entity.
Persisted Physical objects found outside those bounds during startup are
constrained before normal simulation resumes. If an escaped object is below
terrain, the region also raises it with the rotated-bounds clearance rule and
clears its stale velocity, then persists the recovered state.
The 256 terrain samples span the complete `0..256 m` physical region using
`256 / 255 m` between samples. Treating the sample spacing as exactly one metre
would end Jolt's heightfield at coordinate 255, leaving the final metre without
terrain support even though capsule and object origins may validly approach
the 256 border.

Canonical sphere prims use native analytic sphere shapes rather than triangle
collision meshes. Their volume-based mass uses the ellipsoid volume implied by
the viewer scale; the initial native collision radius is half the smallest
scale dimension, giving exact collision for uniformly scaled spheres and a
conservative collision shape for non-uniform ellipsoids. Live Firestorm
acceptance on 2026-07-15 confirmed sphere creation, dynamic collision, Take,
and re-rez with shape and Physical state preserved. A subsequent full restart
retained the physical sphere and its uniform `1.46018 m` scale.

Canonical cylinder prims use a Z-axis native Jolt cylinder and cylinder-volume
mass. Live Firestorm acceptance on 2026-07-15 confirmed that a Physical
cylinder's falling, contact, and rolling behavior appeared correct. Copying
`Physical Cylinder` in Inventory as `Physical Cylinder 2` and re-rezzing the
copy preserved both its cylinder geometry and Physical state. A subsequent
complete viewer and region restart retained the rezzed copy as a Physical
cylinder.

Firestorm's canonical Prism preset is a square/line prim with its top X ratio
collapsed to zero and sheared to one side (`pathScaleX=200`, raw
`pathShearX=0xce`). HomeWorldz preserves the complete classic prim parameter
block and builds a matching six-point convex wedge. Live acceptance on
2026-07-16 confirmed correct rendering and collision on every face, including
the slope. Taking `Prism1` into Inventory and re-rezzing it preserved both its
wedge geometry and Physical state. A subsequent complete viewer and region
restart retained the rezzed Prism as wedge-shaped and Physical.

Firestorm's canonical Pyramid preset uses raw path scale `[200,200]`, collapsing
both top axes to an apex. HomeWorldz mirrors it as a five-point convex hull and
uses one-third-box volume for mass. Live acceptance on 2026-07-16 confirmed
correct rendering and physical collision on all four sloped faces.

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

The initial shared-lab measurements are recorded in
[`PHYSICS_RESULTS.md`](PHYSICS_RESULTS.md).
