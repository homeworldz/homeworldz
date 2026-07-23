# ADR 0029: Server-Side Appearance Baking

Status: Accepted

Avatars are rendered from **baked textures** — flattened composites of the
layers a user wears (skin, tattoos, clothing) for each body region. Today the
**viewer** does this baking: Firestorm composites the bake layers and uploads
finished JPEG2000 baked textures, and the region only caches them
(`baked_texture_cache`, keyed by an outfit/wearable hash), stores them
content-addressed, re-serves them, and rebroadcasts `AvatarAppearance`. That
design depends on **every client baking**. Thin or headless clients — notably
LibreMetaverse/TestClient — never bake, so they render as gray clouds. Second
Life moved baking to the server ("server-side appearance"); HomeWorldz adopts
the same model so that **any** client rezzes correctly with no client-side
baking.

## Decision

The region bakes avatar appearance itself. The pipeline:

1. **Resolve the Current Outfit** — read the user's Current Outfit folder
   (system type 46) from grid inventory, follow its link items (asset type 24)
   to the worn wearables/body parts.
2. **Fetch layers** — parse each `.bodypart`/`.clothing` (LLWearable) asset for
   its layer texture UUIDs and tint/alpha params, and fetch those textures.
3. **Decode** the layer textures (JPEG2000 → RGBA).
4. **Composite** the Second Life bake slots (head / upper body / lower body /
   eyes / hair / skirt), applying per-layer alpha and color tint at the
   standard bake resolutions.
5. **Encode** each baked slot back to JPEG2000.
6. **Store** the baked texture as a content-addressed asset (grid-registered)
   and record it in `baked_texture_cache` keyed by the outfit hash.
7. **Assemble** the avatar's `texture_entry` with the baked UUIDs, set the
   server-side-appearance flags, and **broadcast `AvatarAppearance`**.

## Coexistence with viewer baking

Server-side baking is **additive, not a replacement**. Baking viewers
(Firestorm) keep uploading their own bakes via the existing
`UploadBakedTexture` path, and the cache is shared and keyed by outfit hash, so
a viewer bake and a server bake for the same outfit are interchangeable. The
region bakes when a connecting client does **not** supply one — in particular
thin/headless clients. This preserves the current Firestorm behavior while
filling the gap for everything else.

## JPEG2000 and imaging

JPEG2000 decode and encode use **OpenJPEG** (BSD-2, C) via vcpkg, compiled into
the C++ region. Compositing operates on raw RGBA buffers (alpha blend + tint),
needing no additional imaging dependency. Cinder Roxley's **CoreJ2K** is .NET —
a reference, not a dependency here; Second Life's open-source server-side
appearance and LibreMetaverse's `Baker` are layout references. To the asset
layer, baked textures remain opaque `image/x-j2c` blobs, exactly as today.

## Reuse vs. new work

**Reused (already in the tree):** `baked_texture_cache` +
`store_baked_texture`/`find_baked_texture`; the content-addressed asset store +
grid registration; `encode_avatar_appearance` + its broadcast loops and the
already-wired (currently `0`) server-side-appearance flags field;
`texture_entry` parse/build helpers; the Current Outfit model and grid
inventory access.

**New:** the OpenJPEG dependency; an RGBA image buffer with J2C decode/encode;
an LLWearable parser; region-side Current-Outfit resolution; the bake
compositor; `texture_entry` assembly + appearance-flag setting; and a bake
trigger (login, and later outfit change).

## Derived data and performance

Baked textures are **regenerable derived data** — cache, never authoritative
content — and are exempt from vault durability (consistent with ADR 0026's
baked-texture exemption). Baking is CPU-bound image work and must run **off the
authoritative scene thread** (a worker), delivering `AvatarAppearance` when the
bake completes.

## Phasing

- **Phase 1:** OpenJPEG + J2C decode/encode; composite the **default
  six-wearable outfit** (opaque layers) into the standard bake slots;
  region-side Current-Outfit resolution; trigger on login; broadcast
  `AvatarAppearance`. Goal: default avatars — including LibreMetaverse bots —
  rez server-side instead of as clouds.
- **Later:** full wearable coverage (tattoos, stacked clothing, alpha layers),
  per-layer tint/alpha, all bake slots, re-bake on outfit change, and
  cache-invalidation on wearable edits.

## Relationship to other ADRs

- **ADR 0014 / 0026 / 0027** — baked textures are blobs: derived, cache-tier,
  and vault-exempt.
- Complements the thin-client capability work (`FetchInventoryDescendents2`,
  etc.) but largely **removes** the appearance dependency on client-side
  inventory/asset fetch, since the region produces the finished bakes.

## Phase 1 status (2026-07-22/23)

Implemented and deployed to the cloud grid (all four regions). Live-tested with
a LibreMetaverse bot (which cannot bake at all — it fails to enumerate its
wearables) plus a Firestorm avatar.

**Working:**
- Full bake pipeline, each stage unit-tested: J2C codec wrapper (OpenJPEG),
  LLWearable parser, compositing engine, bake-slot compositor, `TextureEntry`
  encoder, orchestrator, and the 253-param visual-params assembly.
- The bot rezzes with **no cloud and no skirt cone**, at the correct height
  (geometry derived from the default shape and applied to the seeded avatar).
- Key correctness fixes found live:
  - **Viewer asset/texture caps** must accept the slashless `?<type>_id=<uuid>`
    query form, not only `/?…` — otherwise every wearable/texture fetch 404s.
  - Unbaked bake slots must use **`IMG_DEFAULT_AVATAR`** (`c228d1cf-…`), the
    viewer's "never rendered" sentinel. `IMG_WHITE` (5748decc) draws a solid
    grey mesh (skirt cone); `IMG_INVISIBLE` (3a367d1c) is treated as an
    unfinished bake and leaves the avatar a cloud.
  - Never substitute the server bake into a *real* client's appearance stream:
    a baker mid-bake briefly presents zero-creator textures, and re-marking
    them oscillates the avatar between its own shape and the default. The seed
    covers headless clients; real bakers relay untouched.
  - Server-side appearance is signalled by **`appearance_version = 1`** in the
    `AppearanceData` field, which must agree with visual param **11000**
    (`llvoavatar.cpp`: `setIsUsingServerBakes(appearance_version > 0)`).

**Fixed after live debugging:**
- **Empty/white in-server body bake.** The region baked a ~204-byte body
  (upper/lower) while the standalone `homeworldz-bake-diag` produced valid
  ~228KB skin from the same sources. Root cause: the default shirt/pants
  reference **`IMG_WHITE`** (`5748decc`, "Blank") for their texture; the region
  has that asset (the diag's local file set does not), so it was composited
  **opaque over the skin**, turning the body solid white — which encodes to a
  tiny (~204-byte) J2C. Fix: the bake skips blank/placeholder texture UUIDs
  (`IMG_WHITE` / `IMG_INVISIBLE` / `IMG_DEFAULT` / `IMG_DEFAULT_AVATAR` / null),
  so a "Blank"-textured layer no longer paints over the skin. The region now
  bakes valid ~228KB skin for all body slots.

**Open (blocks the bot's body skin from actually rendering):**
- **Delivery.** The viewer parses the baked UUIDs from the `TextureEntry`
  (confirmed via Firestorm's `DebugAvatarAppearanceMessage` dump) but never
  requests them (0 fetches), rendering the stock `IMG_DEFAULT_AVATAR` instead.
  Tied to the legacy-vs-server-bake path (`llvoavatar.cpp`:
  `setIsUsingServerBakes(appearance_version > 0)`); getting `appearance_version
  1` reliably applied without disturbing real bakers (a baker mid-bake briefly
  presents zero-creator textures) is unresolved. Compare against OpenSim's
  server-side-appearance delivery.

Until delivery is resolved, the region relays client appearances untouched and
seeds headless clients on join; the bot rezzes but its body renders as the
stock default avatar (solid white), a reasonable bot fallback.
