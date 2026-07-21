# ADR 0026: Vault-Authoritative Inventory Assets

Status: Accepted

The grid operates a single durable asset vault holding the content-addressed
bytes of every asset referenced by user inventories. The vault is a
replica-only location: it never originates assets, never hosts agents, and is
never in the viewer data path. Regions remain the primary asset stores and
serve all viewer traffic.

The core invariant: an inventory item row may only be committed when the vault
already holds verified bytes for the asset it references. Every
inventory-creating operation — upload, take-to-inventory, give, purchase —
writes the asset through to the vault, with SHA-256 and size verification,
before the item commits. Enforcement is grid-side, so no durability property
depends on region cooperation, replication lag, or orderly shutdown —
necessary because independently operated regions may disappear without
warning. Vault unavailability fails inventory writes; asset reads never depend
on the vault.

Region blob stores are reframed as two tiers. Blobs the vault holds are a
cache: evictable at will and re-fetchable on demand, which unblocks the
region-side unreferenced-blob collection deferred by ADR 0014. Blobs
referenced only by rezzed scene content are region-owned and live and die with
the region, consistent with the scene state itself; region backups, not the
vault, preserve scenes.

Region-to-region fetch (ADR 0020) is demoted from durability mechanism to
optimization; the vault is always a valid fetch location for
inventory-referenced assets. ADR 0020's mechanics — content addressing,
idempotent UUID registration, creator provenance, fail-closed verification,
registered locations — are unchanged and apply to vault ingest and vault fetch
equally.

Baked appearance textures are regenerable derived data and are exempt from
vault ingest.

The vault may internally tier blobs by access age onto slower storage such as
S3-compatible object storage. Tiering is a vault-internal concern: tier-2
storage is not a registry location, and rehydrated bytes are re-verified
against their hash before serving.

Vault garbage collection remains deferred. When defined it must be
conservative: rezzing a no-copy item removes its inventory row while the asset
bytes remain needed for take-back to inventory, so "was ever
inventory-referenced" stays sticky until an asset is proven dead.

Adoption requires a one-time backfill that walks inventory-referenced asset
UUIDs, ingests each from any live registered location, and reports assets that
are already unfetchable. A decommission drain that evacuates scene-only assets
from a region being retired remains a useful operator courtesy, but no
durability property depends on it.
