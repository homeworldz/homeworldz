# ADR 0026: Vault-Authoritative Inventory Assets

Status: Accepted

The grid operates a single durable asset vault holding the bytes behind every
asset referenced by user inventories. Storage is at the blob layer (ADR 0027):
the vault holds blobs, and assets and inventory items reference them
indirectly. The vault is a replica-only location: it never originates assets,
never hosts agents, and is never in the viewer data path. Regions remain the
primary asset stores and serve all viewer traffic.

The core invariant: an inventory item row may only be committed when the vault
already holds the verified blob for the asset it references. The trigger is the
creation of an inventory *reference* to an asset — whether by an upload that is
kept, take-to-inventory, give, or purchase — and each writes the referenced
blob through to the vault, verified against its length and integrity checksum
(ADR 0027), before the item commits. Enforcement is grid-side, so no durability
property depends on region cooperation, replication lag, or orderly shutdown —
necessary because independently operated regions may disappear without
warning. Vault unavailability fails inventory writes; asset reads never depend
on the vault.

Bytes that never become inventory-referenced carry no vault obligation:
content referenced solely by rezzed scene objects, and any upload that never
produces an inventory item. The vault's obligation begins the moment an asset
gains its first inventory reference. (Whether a viewer can upload for in-region
preview and iteration *without* creating an inventory item — avoiding vault and
inventory churn for rejected attempts — is a separate, unresolved question; the
invariant only cares whether an inventory reference exists, not how the bytes
arrived.)

Region blob stores are reframed as two tiers. Blobs the vault holds are a
cache: evictable at will and re-fetchable on demand, which unblocks the
region-side unreferenced-blob collection deferred by ADR 0014. Blobs
referenced only by rezzed scene content are region-owned and live and die with
the region, consistent with the scene state itself; region backups, not the
vault, preserve scenes.

Vault retention is decided at the blob layer by ADR 0027 reference counting: a
blob is vault-durable while any live asset — and through it any inventory item
or scene instance — references it. Two transitions need care, because ordinary
user actions cross the inventory/scene boundary:

- **No-copy items rezzed or embedded into the scene stay vault-durable.**
  Rezzing a no-copy item, or dropping one into an object's contents, removes
  its inventory row, but the rezzed or embedded instance becomes the user's
  *only* copy. Treating it as disposable region-owned content would let region
  loss permanently destroy an irreplaceable asset — exactly the loss the vault
  exists to prevent. So a no-copy asset that leaves inventory into the scene
  remains vault-durable, its bytes retained against the live scene reference.
  This is a **present durability invariant, not a deferred garbage-collection
  nicety**: it must hold the day the vault ships, because a no-copy item rezzed
  today must be take-back recoverable even before any collection exists.
- **Copy content whose inventory master is deleted while a rezzed copy
  remains** transitions cleanly from vault-cache to region-owned. The user
  discarded the master by choice, and the remaining copy is ordinary scene
  content under region and scene-backup durability. The only requirement is
  that the vault never evict bytes out from under a still-live scene reference:
  when the last inventory reference drops while a scene reference remains,
  ownership of the bytes transfers to the region, which materializes its own
  durable (non-cache) copy at that moment.

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
storage is not a registry location, and the vault trusts it to return the bytes
it stored, as with any storage layer.

Vault garbage collection remains deferred. When defined it must be
conservative: rezzing a no-copy item removes its inventory row while the asset
bytes remain needed for take-back to inventory, so "was ever
inventory-referenced" stays sticky until an asset is proven dead.

Adoption requires a one-time backfill that walks inventory-referenced asset
UUIDs, ingests each from any live registered location, and reports assets that
are already unfetchable. A decommission drain that evacuates scene-only assets
from a region being retired remains a useful operator courtesy, but no
durability property depends on it.

## Considered and rejected: user-selected region storage

Letting a user nominate one of their own regions — instead of the vault — as
the durable home for their inventory assets was evaluated as a way to
distribute storage responsibility, and deliberately rejected. It trades away
the deterministic availability this ADR exists to provide.

A region is not durable and not continuously available by the grid's
definition: it can be lost permanently, and it is unreachable during ordinary
restarts, crashes, and network interruptions. Inventory assets are precisely
the mobile ones — attachments, vehicles, items rezzed elsewhere — that must be
fetchable from every region at any time. Self-hosting them therefore creates an
availability failure that surfaces on a *third-party* region: when an owner
arrives somewhere new with an item that region has never cached and the owner's
home region is momentarily offline, the item fails to rez, and the failure is
visible to an operator who did nothing wrong. That breaks the core promise that
no user's experience depends on any region staying alive.

The variants that preserve availability all negate the feature's purpose:
forcing vault ingest on transfer, or keeping the vault as a permanent backstop,
means the vault still stores the bytes, so no storage responsibility is
actually distributed. The only variant that genuinely offloads storage is the
one that exports failures to uninvolved third parties. We prefer deterministic
assets and keep the vault authoritative for all inventory-referenced bytes.
