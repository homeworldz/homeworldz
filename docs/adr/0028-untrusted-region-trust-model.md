# ADR 0028: Untrusted-Region Trust Model

Status: Accepted

HomeWorldz exists so that mostly-untrusted users can run their own regions —
at home or on a cloud VPS — and still participate in one grid. That goal, not
a single-operator deployment, is the trust model the rest of the system is
designed against. A region may disappear without warning, be operated by
someone hostile, or serve incorrect bytes; the design assumes all three.

## The grid is the trust anchor; regions are not

The grid (the operator's central services) is trusted and authoritative for
identity, inventory structure, asset and blob metadata, creator provenance, the
permissions of record, and durable storage (the vault, ADR 0026). Regions are
untrusted for all of it: they serve viewer traffic and host live scenes, but
nothing durable or authoritative depends on a region being present, honest, or
secure.

Concretely, the mechanisms in the asset ADRs are defenses for this model:

- **Durability is grid-owned** (ADR 0026) because a stranger's home region
  cannot be trusted to preserve anyone's inventory. This is also why
  user-selected region storage of inventory bytes was rejected (ADR 0026).
- **Serving is verified** (ADR 0020 / ADR 0027): a region that fetches a blob
  from another region verifies the bytes against the checksum the grid
  recorded, so a hostile or buggy region cannot substitute content.
- **Metadata is grid-owned**: the immutable `asset_id → blob_id` binding,
  creator provenance, and permission records live at the grid. A region
  operator cannot rewrite authorship, remap a UUID to different content, or
  launder permissions through the grid.

## Per-owner federation tokens

Federation authorization is **per owner**, not one shared grid-wide secret.
Each owner holds a single grid service token used by every region they operate,
scoped to that owner's regions' federation and asset operations. Region startup
identity remains per-region (per-region access keys, ADR 0024); the owner token
authorizes the cross-region operations layered on top.

The property this buys is **containment**: a leaked or malicious owner token
compromises only that owner's regions, never the grid or other owners' regions.
It is also **operationally simpler** than a per-region token: an owner running
several regions reuses one token across their region configuration files rather
than managing a distinct secret for each. This replaces ADR 0020's single
shared service token, which was acceptable only for the interim single-operator
deployment and never the intended end state. Issuance, rotation, and revocation
mechanics are implementation details left to the federation-auth work; the
invariant is one token per owner, scoped to that owner.

## Owner control and durability are complementary

Running your own region means real control, not a hollow shell over
grid-dictated state. Owners control their regions, their prims, and their
inventory, and keep **local copies** of their content — all of it, and
necessarily any no-copy item currently rezzed in their region — so they can
take their own **local, file-based backups** and operate independently. Those
backups are raw region state — the region's SQLite databases and blob store —
a recovery safety net, not a portable interchange format and not
permission-filtered; portability is a separate concern served by IAR/OAR export
(below). The grid's vault adds authoritative durability and security *on top
of* that local control; the two are complementary. Neither the grid nor the
owner is a single point that can strand the other's copy.

## Security posture: raise the cost of extraction

Content a region must render or serve is, in the limit, accessible to whoever
runs that region — much as any viewer already has access to what is streamed to
it. We do not assume this class of extraction can be fully prevented, and we do
not build features whose correctness depends on preventing it.

That does not mean doing nothing. Raising the effort an attacker must spend is
worthwhile, and there are steps within reach — for example encrypting or
obfuscating a region's local SQLite records and blob store so casual access
yields nothing and a determined extraction takes real work. How far to take
this is not yet designed and is left open here.

What is firm is where enforcement is reliable: permissions and creator
attribution express and enforce creator and owner intent at the boundaries the
grid controls — inventory operations, transfers, and IAR/OAR export — which are
authoritative regardless of what any region does locally.

## Scripts: p-code crossings, source disclosed by permission

Scripts execute rather than render, so running one on another region needs only
its compiled form. Crossings and teleports carry compiled Falcon **p-code
(bytecode)**, not source (ADR 0021, ADR 0025, VM.md), so hosting or executing a
script on an untrusted region does not disclose its source to that region's
operator. Source is disclosed where standard permissions grant it — a
modifiable/editable script is provided as source, exactly as in Second Life.
Because a script's executable form is separable from its source, script authors
get protection that passive media — textures, meshes, sounds, delivered in
renderable form — cannot match.

## Portable, permission-aware export (IAR / OAR)

Owners get reliable portability and backup of their content through inventory
archives (IAR) and region archives (OAR). Export is an **enforcement
boundary**, not a bypass:

- Only content the owner is entitled to export leaves — export requires the
  asset's export option to allow it **and** the exporting instance's
  permissions to permit it (ADR 0027).
- No-transfer / no-export status and creator attribution are preserved in the
  archive.
- The detailed asset and instance metadata (creator, export option, provenance,
  permission masks) lets export filter **intelligently** — including or
  excluding content according to the owner's rights — rather than being an
  all-or-nothing dump.
- Archives are the owner's portable interchange and cross-grid path, distinct
  from the raw local file backups above.

The permission- and creator-aware behavior is a requirement on the IAR/OAR work
(Phase 6), not optional polish.

## Future: creator-signed provenance

Today "creator" is a UUID the grid records. A future extension is
**creator-signed assets**: the creator signs the asset's creation-time facts
(content reference plus creator identity), at the asset layer, so authorship is
cryptographically verifiable without trusting the grid or the serving region.
This would strengthen export/IAR/OAR authenticity and reduce how much a
participant must trust the operator — an operator could not forge "created by
X."

It is deliberately out of scope for now. Open questions: binding creator
identity to a public key (realistically the grid as identity provider, so trust
shrinks rather than vanishes), exactly which creation-time facts are signed
(never the per-instance permission masks, which are mutable), and key rotation
and revocation. It is recorded here as direction, not specification.

## Relationship to other ADRs

- **ADR 0020 (Asset Origin And Replication)** — replaces the single shared
  service token with per-owner tokens; verified fetch is a defense for
  untrusted serving.
- **ADR 0024 (Provisioned Region Identity)** — per-region access keys remain
  region startup identity; per-owner tokens authorize federation on top.
- **ADR 0026 (Vault-Authoritative Inventory Assets)** — grid-owned durability
  is the response to untrusted, ephemeral regions.
- **ADR 0027 (Asset, Blob, and Instance Separation)** — grid-owned metadata,
  verified fetch, and export = asset-allows AND instance-permits.
- **ADR 0021 (Script Runtime Boundary) / ADR 0025 / VM.md** — crossings carry
  p-code, not source, so executing a script elsewhere does not disclose its
  source; source is not part of a crossing and can be retrieved only where
  permissions grant it.
