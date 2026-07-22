# LibreMetaverse TestClient — HomeWorldz test harness

Using the LibreMetaverse (LMV) **TestClient** as an external, headless load- and
movement-testing client for a HomeWorldz grid. TestClient is an independent
implementation of the same viewer-facing LLUDP + capabilities + XML-RPC login
protocol HomeWorldz targets, so it doubles as a second-client compatibility
probe. Its `ClientManager` can drive many bots at once (mass login via
`--file`), which is the load-testing path.

- Upstream: <https://github.com/cinderblocks/libremetaverse> (BSD-3-Clause).
- The clone lives at `tools/libremetaverse/` and is **gitignored** (reproducible
  external dependency, not vendored):
  `git clone --depth 1 https://github.com/cinderblocks/libremetaverse.git tools/libremetaverse`

## Build requirement: .NET 10 SDK

LMV's build uses Roslyn **source generators** (`SourceGenerators/*`) that emit
`PacketType`, `ObjectUpdatePacket`, `VisualParam`, grass/tree/skeleton types,
etc. from `data/message_template.msg` and the `linden/**` XML. Those generator
projects pin **`Microsoft.CodeAnalysis.CSharp` 5.6.0** (Roslyn 5.x).

- Under **.NET SDK 9** the bundled compiler is Roslyn 4.x, which **silently
  skips** analyzers built against the newer Roslyn — so nothing is generated and
  the build fails with ~74 `CS0246` "type not found" errors. This is **not**
  fixable by trimming target frameworks (that was a dead end).
- **Use the .NET 10 SDK** (matching Roslyn 5.x). Then the generators run and the
  build succeeds. Install machine-wide, or per-user without admin:
  `Invoke-WebRequest https://dot.net/v1/dotnet-install.ps1 -OutFile dotnet-install.ps1; ./dotnet-install.ps1 -Channel 10.0` (installs to `~/.dotnet`).

Build (with a .NET 10 `dotnet`):

```sh
dotnet build -c Release tools/libremetaverse/Programs/examples/TestClient/TestClient.csproj
```

Note: this clone's `.csproj` files had `net10.0` and `net481` removed from
`<TargetFrameworks>` during SDK-9 investigation; harmless under .NET 10 (net9.0
still builds). Re-clone for a pristine tree.

## Local HomeWorldz stack (verified working)

- Login URI: `http://127.0.0.1:42000/login`.
- Provisioned dev regions come from `config/regions.json`: **Welcome**
  (`11111111-1111-4111-8111-111111111111`, key `homeworldz-local-primary`) and
  **Sandbox** (`22222222-…`, key `homeworldz-local-secondary`).

```sh
# grid on :42000 (uses config/{grid,db}.ini + regions.json; does not auto-migrate)
go run ./grid/cmd/grid -config config

# region "Welcome" on :42001/:42002 — use a CURRENT region build.
# build/default and build/vcpkg (Jul 13) are stale and DO NOT register with the
# grid; build/verify-crossed-region (Jul 18) registers correctly. When in doubt
# rebuild with scripts/build-region.ps1.
build/verify-crossed-region/region/homeworldz-region.exe --config config/region.ini \
  --region-id 11111111-1111-4111-8111-111111111111 --access-key homeworldz-local-primary
```

Verify the region is online (should list Welcome with a lease):
`curl -H "Authorization: Bearer homeworldz-local-development" http://127.0.0.1:42000/api/v1/regions`

Create a viewer account (internal API needs the service token). Username splits
to first/last at login, so `test.bot` → first `test`, last `bot`:

```sh
curl -X POST http://127.0.0.1:42000/api/v1/users \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer homeworldz-local-development" \
  -d '{"username":"test.bot","password":"testpass123"}'
```

Run TestClient (once built; `<dll>` = `.../bin/Release/net9.0/TestClient.dll`):

```sh
dotnet <dll> --first test --last bot --pass testpass123 \
  --loginuri="http://127.0.0.1:42000/login" --nogui
```

`--nogui` logs the bot in and idles (no console prompt). CLI also supports
`--file userlist.txt` (many bots — the load path), `--startpos "sim/x/y/z"`,
`--scriptfile <cmds.txt>`. Movement commands include `goto`, `moveto`,
`forward`, `jump`, `fly`, `location`; a scriptfile ending in `quit` drives a run
to completion.

## Login compatibility analysis (static, LMV ↔ HomeWorldz)

`grid/internal/httpapi/viewer_login.go` vs LMV `LoginResponseData.cs`:

- HomeWorldz `/login` is the standard XML-RPC `login_to_simulator`: expects
  `first`/`last`/`passwd` (`$1$` + 32-hex MD5), returns `login`, `agent_id`,
  `session_id`, `secure_session_id`, `circuit_code`, `sim_ip`, `sim_port`,
  `region_x/y`, `region_size_x/y`, `look_at`, `seed_capability`,
  `inventory-root`/`-skeleton`, library folders, `login-flags`, `gestures`,
  `buddy-list`.
- LMV consumes exactly this set. Fields HomeWorldz omits (`home`, `agent_access`)
  are tolerated — `ParseHome` only throws on a *malformed* `home`; absent it
  defaults to zero.
- The *field set* matches. But the **transport does not** — see the live
  result below.

## Live smoke-test result (2026-07-21) — PASSING

First run failed at login with `Expected </methodResponse>`: **modern
LibreMetaverse uses LLSD login** (`LibreMetaverse/Login.cs` builds an LLSD
`OSDMap` `~:1000`, POSTs it via `PostAsync(loginUri, OSDFormat.Xml, …)` `~:1074`,
parses the reply with `OSDParser.Deserialize` `~:1167`), while HomeWorldz's
`/login` implemented only the legacy XML-RPC `login_to_simulator` that Firestorm
7.2.4 uses.

**Resolved:** HomeWorldz `/login` now supports **both** — it dispatches on the
request document type (`<methodCall>` → XML-RPC as before; `<llsd>` → LLSD),
sharing all auth/region/inventory logic (`resolveViewerLogin`) and serializing
either format (`grid/internal/httpapi/viewer_login.go` +
`viewer_login_llsd.go`, committed `787bf97`).

With that in place the smoke test **passes end to end**:

```
Login Success: Welcome to HomeWorldz Local
Logged in test bot
CurrentSim: 'Welcome (127.0.0.1:42002)' Position: <178, 161, 25.031677>
```

TestClient logs in over LLSD, establishes the region UDP circuit (:42002), and
holds a live in-world avatar position. Verified that the legacy XML-RPC login
still returns a valid `<methodResponse>` and the httpapi tests pass, so
Firestorm is unaffected.

## Cloud same-grid test + LMV compatibility findings (2026-07-22)

LLSD login was deployed to the OVH cloud grid (`grid.homeworldz.com`; see the
deployment memory), and a same-grid test was run: 4 LMV bots
(`cloudbot0..3.tester`) plus two Firestorm avatars (Jim + Fae) on the **same**
cloud grid.

**What works against HomeWorldz (LMV):** LLSD login; presence; the **People/
Nearby list**; the **minimap** (fed from ObjectUpdate, not CoarseLocationUpdate);
avatar **movement** (control-flag `forward` walked the avatar ~16 m); ~8-bot
concurrent login load; and bots are **visible** to Firestorm viewers on the
same grid (as clouds). (An earlier "bots invisible" scare was purely
**cross-grid** — bots on the local dev grid, viewers on the cloud grid.)

**Gaps to reach full LMV parity (HomeWorldz-side, share-ready for Cinder):**

1. **Inventory-descendents capabilities not advertised** —
   `FetchInventoryDescendents2` / `FetchLibDescendents2` are absent from the
   region seed caps, so LMV's `AppearanceManager` can't enumerate the agent's
   wearables (→ no bake → avatar stays a cloud). HomeWorldz implements **AIS v3**
   (`/caps/inventory/ais/`), which Firestorm uses, but not the older per-region
   descendents caps LMV relies on.
2. **HTTP asset fetch fails for LMV** — `RequestAssetHTTP` throws in a loop; LMV
   can't download wearable/texture assets. The `GetTexture`/`ViewerAsset` cap
   flow doesn't match LMV's expectations.
3. **Logout not handled → ghost avatars** — the region never detects a viewer
   logout/disconnect (0 "departed" events) and never broadcasts `KillObject`, so
   a logged-out avatar lingers, rezzed, in others' views and the People list.
   Affects **all** viewers, not just LMV.

**Recommended direction (Cinder Roxley, LMV maintainer):** implement
**server-side (region) appearance baking**. If the region composites the bake
layers from a user's worn wearables and serves baked textures, then *every*
client — headless LMV bots, thin viewers, anything — rezzes correctly with no
client-side baking, which **supersedes** chasing gaps #1/#2 for appearance. It's
a natural C++ region job (HomeWorldz already has inventory, asset blobs, a
`baked_texture_cache`, and legacy-baking groundwork); output needs JPEG2000
encoding (C++ **OpenJPEG**; Cinder's .NET **CoreJ2K** and SL's open-source
server-side-appearance "Sunshine" are references). Also noted for the future:
**WebRTC voice** (SL's current path; Firestorm also supports Vivox).
