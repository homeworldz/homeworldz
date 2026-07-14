# Firestorm Compatibility Target

## Pinned Viewer

HomeWorldz targets the OpenSim-enabled **Firestorm 7.2.4** viewer for the first
playable vertical slice.

- Official source branch: [`Firestorm_7.2.4`](https://github.com/FirestormViewer/phoenix-firestorm/tree/Firestorm_7.2.4)
- Pinned source commit: [`10bd3c9f930c76e1427ddd4ecece6cdf36b4406d`](https://github.com/FirestormViewer/phoenix-firestorm/commit/10bd3c9f930c76e1427ddd4ecece6cdf36b4406d)
- Firestorm version file: `7.2.4`
- Upstream viewer version file: `26.1.1`
- Build flavor: 64-bit OpenSim-enabled release build

The source commit, not a moving download URL, is the protocol reference. A
repeatable smoke-test record must additionally capture the installed viewer's
full About dialog version and installer checksum when the binary is introduced
into the manual test workflow.

Firestorm's official repository advises downstream users to use official
release branches rather than `master`, preview, or nightly builds. HomeWorldz
therefore upgrades this pin deliberately and reruns the login smoke test instead
of silently following new viewer releases.

## Minimum Login Request

Firestorm adds HomeWorldz using the base grid URI
`http://<grid-host>:42000/`. It discovers viewer metadata from
`GET /get_grid_info`, including the grid name, login URI, welcome page, and
helper URI. The default login endpoint is `http://<grid-host>:42000/login`.

The OpenSim-enabled viewer posts a `login_to_simulator` request to that URI. The
wire encoding is the viewer-compatible XML-RPC/LLSD login envelope; it is an
edge protocol and does not become an internal HomeWorldz service protocol.

The request parameters relevant to the first slice are:

- credentials: `first`, `last`, and `passwd`;
- destination: `start`, using `home`, `last`, or
  `uri:<region>&<x>&<y>&<z>`;
- viewer identity: `version`, `channel`, `platform`, `platform_version`, and
  `address_size`;
- device/session metadata: `mac`, `id0`, `host_id`, `last_exec_event`,
  `last_exec_duration`, and `last_exec_session_id`;
- protocol controls: `agree_to_tos`, `read_critical`, `extended_errors`, and
  the requested `options` array.

Firestorm 7.2.4 requests inventory roots and skeletons, initial outfit,
gestures, buddy data, UI/login flags, global textures, map/voice settings, and
OpenSim extensions such as currency and maximum groups. HomeWorldz may return
empty optional collections, but it must preserve their expected LLSD types.

## Minimum Successful Response

The login endpoint returns a successful viewer login envelope with at least:

- `login`: `true`;
- identity: `agent_id`, `session_id`, `secure_session_id`, `first_name`, and
  `last_name`;
- destination circuit: nonzero `circuit_code`, `sim_ip`, `sim_port`,
  `region_x`, `region_y`, and `seed_capability`;
- placement: `start_location`, `look_at`, `region_size_x`, and `region_size_y`;
- startup text/time: `message` and `seconds_since_epoch`;
- inventory shapes: one `inventory-root` entry, one `inventory-lib-root` entry,
  one `inventory-lib-owner` entry, plus array-valued `inventory-skeleton` and
  `inventory-skel-lib`;
- array-valued `login-flags`, `gestures`, and `buddy-list` (initially allowed to
  be empty where Firestorm tolerates it).

Failures return `login: false`, a stable `reason`, and a human-readable
`message`. Passwords, session IDs, and secure session IDs must not be logged.

## Startup Sequence After Login

The minimum observable flow is:

1. Firestorm posts `login_to_simulator` to the grid login URI.
2. The grid authenticates the development user, resolves `start`, creates the
   viewer session, and selects an online region lease.
3. The grid returns the session IDs, destination UDP circuit, and region-local
   seed-capability URL.
4. Firestorm creates the destination region and requests the seed capability;
   the region returns the capability map needed by the slice.
5. Firestorm sends reliable UDP `UseCircuitCode` with circuit code, session ID,
   and agent ID. The region acknowledges it.
6. Firestorm and the region exchange `RegionHandshake` and
   `RegionHandshakeReply`.
7. Firestorm sends `CompleteAgentMovement`; the region replies with
   `AgentMovementComplete` and begins required event/object delivery.
8. The smoke test passes when the viewer leaves the startup screen, renders the
   destination region, and can move the avatar and use nearby chat.

The region loads `assets/region/terrain/plateau-square.raw` as its initial
256-by-256 metre terrain and advertises the four matching Library terrain
textures in `RegionHandshake`. `HOMEWORLDZ_REGION_TERRAIN_PATH` may select a
different raw heightmap; invalid or missing input falls back to flat terrain.

## System Library

HomeWorldz presents a grid-owned, read-only inventory root named `Library`.
Its initial viewer-facing structure follows the familiar Second Life layout:
`Library / Clothing / Initial Outfits / Default Avatar`, with a standard
top-level `Body Parts` category. The default-avatar folder contains the same
shape, skin, hair, eyes, shirt, and pants used to clothe a newly created user.
`Library / Textures / Terrain` contains the licensed default sand/dirt, grass,
mountain, and rock textures used by the compatible terrain protocol.

Firestorm reads this shared catalog through the separate `LibraryAPIv3`
capability when supported. Firestorm 7.2.4 may instead request known Library
folder UUIDs through `InventoryAPIv3`; HomeWorldz recognizes those UUIDs and
returns the same read-only catalog. Library data is not copied into personal
inventory and never changes an existing outfit merely because the viewer
reads it.

The stable `homeworldz.library` service identity owns this catalog and is
shown to viewers as `HomeWorldz Library` when identity names are available.
Its UUID, `00000000-0000-0000-0000-000000000002`, is also recorded as creator
for bundled Library inventory items and as uploader for their region asset
mappings. The account is created locked; an administrator must explicitly set
a password before using it for interactive content preparation.

## Ordinary Texture Upload

The region advertises `NewFileAgentInventory` for the initial ordinary-upload
slice. Firestorm sends texture metadata as LLSD, receives a one-shot uploader
URL, converts the selected source image to JPEG2000, and posts the binary asset.
HomeWorldz validates the active viewer session and JPEG2000 signature, records
the authenticated uploader UUID as asset creator provenance, and asks the Grid
to create a texture inventory item in the requested folder owned by that same
user. The completion response contains distinct new asset and inventory-item
UUIDs. Upload cost is zero in the local development grid. Sounds, animations,
snapshots, mesh, objects, and bulk or variable-price uploads remain outside
this first slice.

Each region registers its viewer UDP port with the grid. Login advertises that
stored port, while `HOMEWORLDZ_VIEWER_PORT` controls the matching region
listener (default `42002`). The first reliable `UseCircuitCode` datagram is
accepted only when its circuit code, session, agent, and destination region all
match the grid's live session record.

The region returns `EventQueueGet` from the authenticated seed capability. Its
first poll delivers `EstablishAgentCommunication`; later polls return an empty
event array with a monotonically increasing queue ID. UDP startup enforces
`UseCircuitCode`, `RegionHandshakeReply`, then `CompleteAgentMovement` before
returning `AgentMovementComplete`.

After movement completes, authenticated `AgentUpdate` packets drive the
authoritative avatar controller. It retains viewer camera axes and draw
distance, resolves walking and running relative to body orientation, applies
jump gravity and grounding, and supports fly-mode vertical controls.

Once movement completes, the region streams all 256 flat 16-by-16 terrain
patches in bounded reliable batches and sends a full static volume-primitive
update. Channel-zero viewer chat is rebroadcast reliably to avatars within the
viewer whisper, normal, or shout radius.

The seed also advertises a session-scoped `GetTexture` capability. Texture
UUIDs are validated, resolved through the region's SQLite mapping, read from
the immutable SHA-256 blob store with hash verification, and returned as
`image/x-j2c`; absent or corrupt mappings return `404`.

The grid supplies the system inventory skeleton, item metadata, four required
default body parts, a default shirt and pants, and Current Outfit Folder links.
The region imports the bundled Halcyon-compatible wearable and source-texture
assets and serves them through `ViewerAsset`. The complete default outfit is
seeded only when an account has no established initial outfit; later deployment
changes do not add clothing or alter the COF of existing avatars.

For an uncached outfit, the region answers `AgentCachedTexture` with explicit
misses. Firestorm locally composites the five legacy bakes and uploads them
through the authenticated `UploadBakedTexture` capability. The region stores
the JPEG2000 assets with the uploader's creator UUID and serves Firestorm's
host-bake refetches through reliable, throttled UDP `ImageData` and
`ImagePacket` transfers. The final `AgentSetAppearance` associates each
wearable hash and texture index with its baked asset in persistent SQLite.
Later logins return only exact hash/index matches; changed outfit inputs remain
misses and trigger a new bake.

The pinned viewer source constructs the request in
[`lllogininstance.cpp`](https://github.com/FirestormViewer/phoenix-firestorm/blob/10bd3c9f930c76e1427ddd4ecece6cdf36b4406d/indra/newview/lllogininstance.cpp#L155),
consumes the circuit response in
[`llstartup.cpp`](https://github.com/FirestormViewer/phoenix-firestorm/blob/10bd3c9f930c76e1427ddd4ecece6cdf36b4406d/indra/newview/llstartup.cpp#L4748),
and starts the UDP circuit with `UseCircuitCode` in the same
[`llstartup.cpp`](https://github.com/FirestormViewer/phoenix-firestorm/blob/10bd3c9f930c76e1427ddd4ecece6cdf36b4406d/indra/newview/llstartup.cpp#L2455).

## Windows Smoke Test

After bootstrapping PostgreSQL and building both services, run:

```bat
scripts\firestorm-smoke-test.cmd
```

The launcher reads ignored local configuration, starts the grid and region on
loopback, and verifies both readiness endpoints. Put reusable local credentials
in `config/smoke-test.ini`:

```ini
[user]
first_name = Jim
last_name = Tarber
password = your-local-test-password
```

When the file or password is absent, the launcher prompts for a password. It
creates the configured `jim.tarber` smoke-test account or
validates the supplied password when that account already exists, then launches
the installed OpenSim-enabled Firestorm with the HomeWorldz login URI. Service
logs are written beneath ignored `var/smoke-test/`, and the services stop when
Firestorm exits or the launcher is interrupted.

Inside Firestorm, log in with the configured name and password. Verify initial
region entry, avatar movement,
nearby chat, terrain and the welcome primitive. Log out to prove circuit and
session disconnection, then log in again to prove reconnection. Restart the
launcher and repeat once to verify persisted scene restoration and grid region
re-registration. Record the About-dialog version and executable SHA-256 with
the smoke-test result before completing the milestone.

Use `-first` and `-last` to override the configured account, `-config` to select
a different credentials file, or `-validate-only` to check service startup
without prompting or launching the viewer.

## Milestone 3 Smoke-Test Result

Milestone 3 passed on 2026-07-13 using the Windows OpenSim release executable:

- About/log version: `Firestorm-Releasex64 7.2.4.80712 [10bd3c9f93]`;
- executable: `FirestormOS-Releasex64.exe` (54,033,256 bytes);
- executable SHA-256:
  `9c891171eecab1c24f2eec47aa77f26b5e7f559dd2da72b49dd0e3db6de45bb1`.

The viewer discovered and authenticated against HomeWorldz, entered the region,
rendered the complete 256-by-256 terrain, water, environment lighting, shadows,
and the welcome primitive, and accepted avatar movement. The UDP circuit
remained connected for more than 30 minutes without a heartbeat timeout. A
normal logout received an immediate server reply, cleared grid presence,
revoked the viewer session, and stopped both development services. A subsequent
launcher run restored the scene and reconnected successfully; the avatar
returned to its persisted position of approximately `(140.144, 142.803, 25)`.
Repeated pre-fix avatar entities were consolidated to the one persisted entity.

The follow-on appearance acceptance passed on 2026-07-13. Firestorm fetched and
wore the four required default body parts, automatically created and uploaded
the five legacy baked textures without a manual rebake, refetched those bakes
over simulator UDP, and rendered the avatar. A subsequent clean launcher run
returned five server-side wearable-cache hits, zero misses, and made no baked
upload requests. Firestorm displayed the avatar immediately and logout again
cleared presence and revoked the viewer session normally.

The read-only system Library acceptance passed on 2026-07-14. Firestorm showed
the shared `Library / Clothing / Initial Outfits / Default Avatar` hierarchy
and all six default wearable items. This Firestorm build requested the Library
folder UUIDs through `InventoryAPIv3`; HomeWorldz returned the shared catalog
without copying it into Demo Avatar's personal inventory or changing the worn
outfit.

The terrain-texture extension passed in the same Firestorm version on
2026-07-14. The viewer showed all four entries beneath
`Library / Textures / Terrain` and opened the bundled JPEG2000 asset for visual
inspection without a missing-asset or decode error.

The first shaped-terrain implementation passed on 2026-07-14. Firestorm
reconstructed the user-supplied `terrain-island5.png` heightmap with the
intended island/channel geometry, correct water separation, and visible
blending among the four advertised terrain textures. No manual terrain or
texture refresh was required. It was subsequently replaced as the development
default because its vertical relief was too dramatic. Experimental terrain
authored from the rendered `Image-island3` preview established the correct
north/south orientation but was not accepted as a default because rendered
texture and lighting do not make a clean heightmap.

The final default-terrain acceptance passed on 2026-07-14. Firestorm rendered
the project-generated square plateau at the intended approximately
250-by-250-metre above-water footprint, with water visible on every side,
slightly softened corners, and a calm 22-metre surface above the standard
20-metre waterline. This large square terrain is the development default; the
separate bundled 200-metre round plateau remains an optional alternative.
