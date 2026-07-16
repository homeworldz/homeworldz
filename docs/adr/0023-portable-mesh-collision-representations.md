# ADR 0023: Portable Mesh Collision Representations

Status: Accepted

HomeWorldz stores mesh rendering data and collision data as separate concerns.
An uploaded mesh may provide a dedicated portable physics mesh or hull set;
otherwise an explicitly selected, validated derivation policy produces one.
The portable source is authoritative. Jolt- or PhysX-cooked shapes are
rebuildable caches keyed by the source hash, engine, engine version, and
relevant preparation settings.

Static scene geometry may use a validated triangle collision mesh. Dynamic
rigid bodies use an analytic shape, convex hull, or compound of convex hulls;
the visual triangle mesh is not implicitly used as a dynamic collision body.
Linksets become compound collision representations assembled from their child
shapes.

Parametric prims that cannot use an exact native analytic shape are converted
to portable vertices and convex hulls before reaching an engine adapter. The
Halcyon `Meshmerizer`/`PrimMesher` and PhysX meshing pipeline are the primary
compatibility reference for this conversion, including shape-key caching, but
HomeWorldz does not treat legacy PhysX-cooked bytes as portable content.

Animated, skinned, or otherwise deforming visual meshes use a static capture
of their collision representation when instantiated. Rigid translation and
rotation move that captured representation, but visual vertex deformation does
not continuously rebuild physics geometry. A deliberate scale or collision
mode change may request a bounded rebuild. Avatar attachments are non-colliding
by default; if attachment collision is supported later, the same captured
shape follows the attachment rigidly.

Region handoff transfers portable collision asset references, collision mode,
scale and compound structure, plus body position, orientation, linear velocity,
and angular velocity. It does not serialize engine pointers or make an
engine-specific cooked blob authoritative. The destination may prefetch and
prepare its cached shape before committing the crossing.

Mesh upload and import enforce limits for triangle count, hull count, vertices
per hull, compound children, memory, and preparation time. Expensive collision
preparation occurs outside the region simulation tick.
