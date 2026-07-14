# HomeWorldz Feature Differences

This document tracks major intentional differences between HomeWorldz and the
Second Life, OpenSimulator, and Halcyon models. It is a design ledger, not a
general implementation checklist. Missing compatibility work belongs in
[`docs/PLAN.md`](docs/PLAN.md); protocol observations belong in
[`docs/FIRESTORM.md`](docs/FIRESTORM.md).

## Implemented differences

### Asset creation provenance

Every HomeWorldz asset metadata record has a required `creator_id` UUID. For a
viewer-created asset, this is the authenticated UUID of the user whose upload
created the asset record. Bundled or migrated assets whose original creator is
unknown use the zero UUID as an explicit system/unknown value.

The creator is asset provenance, not current ownership:

- Transferring an inventory item does not change the asset creator.
- Creating another inventory item that references an existing asset does not
  change the asset creator.
- Re-registering an existing viewer-facing asset UUID does not replace its
  original creator.
- Inventory creator and owner fields may still be retained independently where
  the viewer protocol requires them.

This differs from compatibility models that can keep creator and ownership
primarily on inventory items or scene objects without requiring authenticated
uploader provenance on every stored asset mapping. HomeWorldz enforces the
provenance rule at the asset-storage boundary regardless of what metadata a
viewer exposes. Second Life's internal asset-store schema is not public, so
this comparison concerns observable/protocol semantics rather than a claim
about its private implementation.

### Region-local, content-addressed assets

HomeWorldz stores immutable asset bytes as region-local SHA-256-addressed blobs.
Viewer UUIDs map to those blobs in SQLite, allowing different viewer IDs to
share bytes without duplication. This deliberately does not preserve the
central or pluggable legacy asset-service boundaries commonly used by
OpenSimulator and Halcyon, and it does not attempt to reproduce Second Life's
private backend layout.

### Free texture uploads

HomeWorldz does not charge users to upload textures. Regions advertise a zero
upload price through the viewer economy protocol and identify the grid's
currency as credits (`C$`) for viewer interfaces that insist on displaying a
currency beside zero. Credits may support other features later, but texture
upload pricing is not reserved as an economy mechanism.

### Internal service boundaries

HomeWorldz uses a C++20 region server, Go grid services, HTTP/JSON internal
APIs, PostgreSQL central state, and region-local SQLite/filesystem state. It
does not preserve OpenSimulator or Halcyon internal APIs, database schemas,
WHIP/Aperture boundaries, C#/.NET implementation constraints, or protobuf as
an architectural default. Viewer-protocol compatibility remains a goal even
when the server internals differ.

### Pluggable physics engines

HomeWorldz keeps simulation behind an engine-independent physics plugin
boundary rather than making one third-party engine part of the scene model.
[Jolt Physics](https://github.com/jrouwe/JoltPhysics) is the initial target and
default engine. NVIDIA PhysX 5.x is also intended to become a supported plugin;
its current adapter and shared acceptance lab remain the foundation for that
eventual production support. A region selects one engine implementation while
the authoritative scene, persistence, and transfer contracts remain common.

## Planned differences

### Variable-sized regions

HomeWorldz plans to support exactly three OpenSimulator-style region sizes:
1x1 (256 by 256 metres), 2x2 (512 by 512 metres), and 4x4 (1024 by 1024
metres). Larger sizes and arbitrary whole multiples are intentionally out of
scope. This differs from Second Life's public 256-by-256-metre region model
while preserving the most useful OpenSim content convention. The first
implementation remains fixed at 1x1, and larger regions must not be simulated
as loosely joined independent regions.

### AIS-first viewer inventory

HomeWorldz requires Second Life AIS v3 for supported viewer-facing inventory
mutation workflows. Legacy inventory capabilities and UDP messages may remain
as compatibility shims for OpenSimulator, Halcyon, Firestorm, and other
viewers, but feature completion does not require a parallel legacy path. All
adapters use the same grid-owned inventory model rather than creating separate
legacy and AIS stores. See
[`ADR 0018`](docs/adr/0018-ais-first-viewer-inventory.md).

Read-only legacy inventory browsing is a stretch compatibility goal for older
viewers. It is best effort, may remain incomplete, and does not imply support
for legacy mutation workflows or a commitment to any particular older viewer.

## Maintenance rule

Add an entry here when a behavior is an intentional product or architecture
difference that operators, developers, or content creators could observe or
must account for. State whether it is implemented or planned, and avoid listing
temporary gaps as features.
