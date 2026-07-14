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

### Internal service boundaries

HomeWorldz uses a C++20 region server, Go grid services, HTTP/JSON internal
APIs, PostgreSQL central state, and region-local SQLite/filesystem state. It
does not preserve OpenSimulator or Halcyon internal APIs, database schemas,
WHIP/Aperture boundaries, C#/.NET implementation constraints, or protobuf as
an architectural default. Viewer-protocol compatibility remains a goal even
when the server internals differ.

## Planned differences

### AIS-first viewer inventory

HomeWorldz treats Second Life AIS v3 as the primary viewer-facing inventory
mutation protocol. Legacy inventory capabilities and UDP messages may remain
as compatibility shims for OpenSimulator, Halcyon, Firestorm, and other
viewers, but new mutation workflows are not required to implement a legacy
path before their AIS path. All adapters use the same grid-owned inventory
model rather than creating separate legacy and AIS stores. See
[`ADR 0018`](docs/adr/0018-ais-first-viewer-inventory.md).

## Maintenance rule

Add an entry here when a behavior is an intentional product or architecture
difference that operators, developers, or content creators could observe or
must account for. State whether it is implemented or planned, and avoid listing
temporary gaps as features.
