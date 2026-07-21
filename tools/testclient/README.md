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

## Live smoke-test result (2026-07-21)

Built clean with the .NET 10 SDK (generators run), then ran against the local
grid + online "Welcome" region with account `test.bot`. **Login failed:**

```
Login Failed: Expected </methodResponse>
```

Root cause — a genuine protocol mismatch the static field analysis could not
catch:

- **Modern LibreMetaverse uses LLSD login.** `LibreMetaverse/Login.cs` builds an
  LLSD `OSDMap` (`~:1000`), POSTs it as LLSD XML
  (`PerformLoginAsync` → `HttpCapsClient.PostAsync(loginUri, OSDFormat.Xml, …)`,
  `~:1074`), and parses the reply with `OSDParser.Deserialize` (`~:1167`). The
  "Expected `</methodResponse>`" error is LMV's LLSD XML reader
  (`LibreMetaverse.StructuredData/LLSD/XmlLLSD.cs:466`) choking on an XML-RPC
  document.
- **HomeWorldz `/login` implements only legacy XML-RPC `login_to_simulator`**
  (`grid/internal/httpapi/viewer_login.go`) — which is what Firestorm 7.2.4
  (the pinned target) uses, so Firestorm works. LMV, tracking current SL, has
  dropped the XML-RPC login path (no toggle for it).

**Fix / decision for HomeWorldz:** add **LLSD login** support to `/login` —
accept an `application/llsd+xml` `OSDMap` request and return an LLSD `OSDMap`
response carrying the same fields the XML-RPC path already produces. It can be
additive (branch on the request root: `<methodCall>` → XML-RPC as today;
`<llsd>` → LLSD), leaving the Firestorm path untouched. This both unblocks
LMV-based testing and broadens compatibility to modern LLSD-login viewers. It
touches the production login endpoint, so it needs a deliberate decision before
implementing.

Everything else is verified working: TestClient builds, the local grid + region
come up, the region registers/goes online, account creation works, and the
harness reaches the login exchange — only the login transport blocks it.
