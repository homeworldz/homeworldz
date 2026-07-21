# ADR 0017: Central Inventory Metadata

Status: Accepted

ADR 0026 supersedes this ADR's claim that asset bytes never live in the grid:
the grid asset vault durably stores the bytes of every inventory-referenced
asset, and an inventory item may only be committed once the vault holds
verified bytes for it. The metadata model below is unchanged — the vault holds
bytes, not inventory structure, and inventory remains region-independent.

The grid stores inventory folder and item metadata in PostgreSQL because an
avatar's inventory must remain available independently of any one region.
Inventory records reference viewer-facing asset UUIDs. The original decision
kept asset bytes out of the grid entirely, with immutable content region-local
under ADR 0014 until federation and replication existed; ADR 0026 later made
the grid vault the durable home for inventory-referenced bytes so that
inventory content, not just structure, survives the loss of any region.

Each user has one root folder and at most one folder for every nonnegative
system-folder type. User-created folders use type `-1`. Folder mutations
increment a monotonic version so viewer descendants and capability responses
can detect changes. Items retain explicit viewer asset/inventory types,
permission masks, sale metadata, and flags rather than hiding protocol-relevant
state in an opaque document.

The initial schema permits an asset UUID without a durable region locator.
Asset authorization, origin tracking, replication, and federation lookup are a
later milestone and will extend the reference without moving blob ownership
into PostgreSQL.
