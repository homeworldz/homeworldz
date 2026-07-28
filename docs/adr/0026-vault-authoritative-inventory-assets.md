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

## Completeness is transitive, at both ends (2026-07-28)

"Stored centrally" means the full chain — the inventory item row, the asset
record, and the blob — and *asset* means the item's whole reference closure,
not just the UUID on the row. Inventory-to-asset is 1:N. An object asset names
the textures on its faces and the assets in its task inventory, a nested
object is itself an asset with a closure of its own, wearables name textures,
gestures name animations and sounds, and notecards can embed items. An
inventory item is only durable when every asset in that closure is vault-held,
so the commit invariant gathers: ingest the direct asset, parse it by type,
recurse over its references. Parsing happens grid-side, on the vault's own
copy of the bytes — ADR 0028 forbids trusting a region-supplied reference
list, and verifying one means parsing anyway. Reference UUIDs that name no
registered asset are treated as external (viewer built-in textures, plain
colors, cross-grid content) and recorded rather than fatal: failing the commit
would block every object wearing a stock texture, and the grid cannot fetch
what was never registered with it. The adoption backfill must walk the same
closure, since object assets vaulted before this section existed cover only
their own bytes.

The mirror requirement holds region-side, for content rezzed into the world: a
rezzed object is only region-durable when the region *locally* holds its
transitive closure — the scene object, its referenced assets, its task
inventory items' assets including unrezzed nested objects, and all their
blobs — so that a backup of the region's own storage reconstructs it without
reaching any other server. A region-local blob store separate from the task
inventory record is the expected shape; what matters is locality, not layout.
Lazy materialization on first read does not meet this: rez, arrival from
another region, and adding an item to an object's contents must each
materialize the closure.

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

  Reaffirmed and extended 2026-07-28. The cost argument is sharper than the
  original text made it: these cases are a small fraction of any inventory, and
  the blob behind a no-copy item is usually already vault-held through someone
  else's copyable reference (typically the creator's), so retention normally
  adds only metadata — an asset link and an item record — not bytes. And bytes
  alone are not enough: once the rez deletes the inventory row, the item's
  metadata (name, permissions, creator, asset link) survives only in the
  region's task inventory, so a region bug, total data loss, or a malicious
  region would leave the vault holding bytes nothing can reconstitute. The
  grid therefore also keeps a **hidden retained record** of a no-copy item
  that leaves inventory into the scene. A retained record is not inventory:
  it is excluded from inventory views, viewer fetches, and inventory exports
  by default — the item genuinely left the inventory, and an export must not
  include it. It exists "just in case": recovery from it is a deliberate
  (operator or explicit-user) action, and an ordinary take-back through the
  live region path clears it. Retained records fold into the conservative
  garbage-collection posture below: kept until proven dead.
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
are already unfetchable.

## Implementation note: the grid pulls, 2026-07-28

Written as evidence, not intent. Before this shipped, four of a test avatar's
five worn wearables had no surviving bytes on any of the four regions the
registry named as their origins, and the vault held nothing at all. The viewer's
own cache had been the last copy, so clearing it is what exposed the loss. That
is the failure this ADR describes, observed.

The invariant is therefore enforced by the grid **fetching** the bytes at commit
time — from a location the registry already records — rather than waiting for a
region to write them through. Same act as the backfill, one mechanism: a grid
that ran without a vault repairs itself as inventory is touched. It also makes
the durability property literally independent of region cooperation, which the
paragraph above only asked for: no region needs upgrading, shutting down
cleanly, or choosing to participate. A region write-through remains worthwhile
as an optimization, since the region has the bytes in hand, and is not what
durability rests on.

Enforcement wraps the inventory store rather than each handler. Eight call sites
commit inventory references today across ordinary copies, AIS, and the outfit
paths, and an invariant that each new one must remember is not an invariant.
Links are exempt: a link's asset_id names the item it points at, not any bytes.

The vault's HTTP surface is addressed by asset UUID, not by digest, because that
is what a region knows — blob_id is grid-internal by [ADR 0027](0027-asset-blob-instance-separation.md)
— and because a bare digest cannot say which registered blob an ingest is meant
to vouch for. Neither the checksum nor the length is taken from the request:
both come from the blob registration, so bytes that disagree with what the grid
already believes the asset to be are refused rather than stored. A decommission drain that evacuates scene-only assets
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
