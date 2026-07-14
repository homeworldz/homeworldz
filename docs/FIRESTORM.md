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

## Personal Inventory Mutation

`InventoryAPIv3` supports authenticated rename, description update, move, and
delete operations for personal inventory items. Item updates preserve the
original asset UUID and creator UUID, and moving an item updates both source
and destination folder versions. The shared Library remains read-only through
both its dedicated capability and the compatibility reads exposed through the
personal AIS endpoint. Folder creation and rename use the same AIS-first model;
legacy viewer mutations are not a second source of inventory truth. AIS batch
link creation is atomic and supports viewer-managed Current Outfit replacement
without having the grid reapply the default outfit to established avatars.

## Ordinary Texture Upload

The region advertises `NewFileAgentInventory` for the initial ordinary-upload
slice. Firestorm sends texture metadata as LLSD, receives a one-shot uploader
URL, converts the selected source image to JPEG2000, and posts the binary asset.
HomeWorldz validates the active viewer session and JPEG2000 signature, records
the authenticated uploader UUID as asset creator provenance, and asks the Grid
to create a texture inventory item in the requested folder owned by that same
user. The completion response contains distinct new asset and inventory-item
UUIDs. Texture upload cost is always zero. The UDP economy response advertises
that zero price, while `SimulatorFeatures.OpenSimExtras.currency` identifies
the grid's viewer-facing currency as credits (`C$`). Sounds, animations,
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

The first ordinary texture upload passed on 2026-07-14 while logged in as
`HomeWorldz Library`. Firestorm uploaded `terrain-island5.png` virtually
instantly and created `Terrain Island 5 Heightmap` in the account's personal
Textures folder. The region stored a 3,990-byte JPEG2000 asset, returned
distinct asset and inventory-item UUIDs, and recorded the stable Library UUID
`00000000-0000-0000-0000-000000000002` as both authenticated uploader and
creator provenance. The initial button showed `OS$-1` because the region had
not yet answered `EconomyDataRequest`; the subsequent zero-price economy
response was accepted and changed the button to `OS$0`. Firestorm obtains the
currency symbol separately from the region's `SimulatorFeatures` capability;
the subsequent capability response was accepted and the preview button showed
`Upload (C$0)`.

The first personal AIS item rename passed on 2026-07-14. While logged in as
`HomeWorldz Library`, Firestorm renamed the uploaded `terrain-island5` texture
to `Terrain Island 5`; both viewer PATCH requests returned success and the
stored item immediately reported the new name. A subsequent clean viewer login
showed `Terrain Island 5`, and Firestorm opened the texture successfully. A
second rename from `terrain-paw` to `Terrain Paw` was also accepted immediately;
its persistence across the next clean login remains to be confirmed.

AIS Current Outfit link creation passed on 2026-07-14. An earlier incomplete
outfit replacement had left the `HomeWorldz Library` account's Current Outfit
empty while its four body parts and two clothing items remained intact. After
adding atomic AIS link creation and compatibility handling for legacy null
creator metadata, Firestorm successfully wore all six personal items. Each
Current Outfit POST returned success, the cloud cleared, and the baked avatar
appeared without a manual viewer rebake. This recovery was explicitly initiated
by the avatar; the grid did not reapply defaults automatically to an established
account.

The packaged login-logo update passed on 2026-07-14. After rebuilding and
restarting the grid, Firestorm's login screen displayed the swapped-color
HomeWorldz SVG. An automated test also requires the grid's embedded SVG to
remain byte-for-byte identical to the project-root logo.

The AIS v3 Library-outfit flow passed on 2026-07-14 with `Jim Tarber`.
Firestorm copied `Library/Clothing/Initial Outfits/Default Avatar` into a new
personal Clothing subfolder through `LibraryAPIv3` HTTP `COPY`, then replaced
the Current Outfit through `InventoryAPIv3` HTTP `PUT .../links`. All six
personal wearables immediately showed as worn, the shirt and pants rendered,
and Current Outfit showed six valid links. Copying the Library outfit again
intentionally creates another personal folder; duplicate folders observed
during development reflected multiple explicit test attempts.

The copied-outfit persistence and cleanup checks passed in the same acceptance
session. After a clean viewer restart, all six wearables remained linked in
Current Outfit and rendered correctly. Firestorm moves personal folders to
Trash with legacy UDP `MoveInventoryFolder` even when AIS v3 is available;
the region's compatibility adapter forwarded two inactive `Default Avatar`
folder moves to the authoritative grid inventory service. Both moves returned
success, and a subsequent clean login showed only the active copy in Clothing
and both inactive copies in Trash without disturbing the worn outfit.

AIS v3 Trash purge also passed on 2026-07-14. Firestorm sent HTTP `DELETE` to
`InventoryAPIv3/category/{trash-id}/children`; the grid recursively removed the
two inactive outfit folders and their items while retaining the protected
Trash folder. The request returned success, Trash remained empty after a clean
login, and the active Clothing folder, six Current Outfit links, worn state,
and rendered avatar were unchanged.

Personal item moves to Trash passed on 2026-07-14. Firestorm moved an uploaded
texture with legacy UDP `MoveInventoryItem`, encoding its optional `NewName`
field with zero bytes. The region compatibility adapter accepted that encoding
and forwarded the move to the authoritative grid inventory service, which
returned success. On a clean login the texture remained in Trash and was absent
from Textures; Jim Tarber's clothing, Current Outfit links, and rendered avatar
were unchanged.

The complete personal-item Trash lifecycle passed in the same session.
Firestorm restored the texture from Trash to Textures through another
authoritative `MoveInventoryItem`, moved it back to Trash, and then emptied
Trash through AIS v3. The purge returned success and a direct AIS lookup of the
item returned `404`. After a clean login both Trash and Textures remained free
of the purged item, while clothing, Current Outfit, and avatar rendering stayed
correct.

The packaged-region configuration regression passed on 2026-07-14. After the
region gained direct `config/region.ini` support, the extracted Release package
started from its packaged example and app-local runtime files on isolated
ports. The relinked Debug service then completed a normal Jim Tarber login:
terrain and Library loaded, the clothed avatar and six Current Outfit links
were correct, Trash and Textures remained empty, and the purged test texture
did not return.

The first user-created primitive acceptance passed on 2026-07-14. Jim Tarber
created two standard boxes with Firestorm's Build tool; the region assigned
distinct object UUIDs and local IDs, recorded Jim's owner UUID, persisted their
0.5-metre scales and wood material, and restored both after a complete service
and viewer restart. A follow-up box used authoritative terrain height for a
land click: its stored centre was 22.25 metres, placing its lower face exactly
on the plateau's 22-metre surface rather than at the viewer ray endpoint.

Object-surface placement passed on 2026-07-14. Firestorm created a fourth
standard box on the upper face of the terrain-aligned box, preserving the
clicked horizontal offset. The stored centres were 22.25 and 22.75 metres
respectively for two 0.5-metre boxes, so their adjoining faces met exactly at
22.5 metres without overlap or a gap.

Initial object ownership, permissions, and delete-to-Trash acceptance passed
on 2026-07-14. Scene snapshots retained Jim Tarber's UUID independently as
both the original creator and current owner of his new boxes. Recipient-aware
object update flags and standard object-property replies made Firestorm show
the expected edit rights without its earlier non-owner confirmation warning.
The grid-backed UUID-name reply resolved creator and owner UUID
`169221cb-f2c9-48b3-a79d-c1d3e01d3723` to `Jim Tarber` in object details and
the avatar profile. Deleting the upper stacked box removed it from the
authoritative scene snapshot and created a `Primitive` object item in Jim's
Trash. A clean restart subsequently passed: the deleted upper box remained
absent, the `Primitive` item remained in Trash, and Firestorm again resolved
both Creator and Owner as `Jim Tarber`.

Viewer primitive transform and name editing passed its live-session acceptance
on 2026-07-14. Firestorm moved a box to `202, 144, 23`, resized it to
`1 x 2 x 3` metres, and renamed it from `Primitive` to `Prim1`. The region
decoded the standard `MultipleObjectUpdate` and `ObjectName` messages, checked
Jim Tarber's ownership and modify mask, persisted all three values, and sent
authoritative state back to the viewer. Closing and reopening Firestorm's Edit
panel retained `Prim1`; clean-restart persistence remains to be confirmed.
