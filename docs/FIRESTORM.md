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
  `inventory-skel-lib` (initially allowed to be empty);
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
loopback, verifies both readiness endpoints, and securely prompts for a
development-user password. It creates the default `smoke.user` account or
validates the supplied password when that account already exists, then launches
the installed OpenSim-enabled Firestorm with the HomeWorldz login URI. Service
logs are written beneath ignored `var/smoke-test/`, and the services stop when
Firestorm exits or the launcher is interrupted.

Inside Firestorm, log in as first name `Smoke`, last name `User`, using the
password entered at the prompt. Verify initial region entry, avatar movement,
nearby chat, terrain and the welcome primitive. Log out to prove circuit and
session disconnection, then log in again to prove reconnection. Restart the
launcher and repeat once to verify persisted scene restoration and grid region
re-registration. Record the About-dialog version and executable SHA-256 with
the smoke-test result before completing the milestone.

Use `-first` and `-last` for a different development account, or
`-validate-only` to check service startup without prompting or launching the
viewer.
