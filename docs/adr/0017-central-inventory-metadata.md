# ADR 0017: Central Inventory Metadata

Status: Accepted

The grid stores inventory folder and item metadata in PostgreSQL because an
avatar's inventory must remain available independently of any one region.
Inventory records reference viewer-facing asset UUIDs but do not place asset
bytes in the grid; immutable content remains region-local under ADR 0014 until
federation and replication are implemented.

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
