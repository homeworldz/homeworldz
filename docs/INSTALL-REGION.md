# Install a HomeWorldz Region

This guide is for a **region owner** connecting a region to an existing
HomeWorldz grid. You do not need PostgreSQL, its credentials, or access to the
grid database. If you operate the central grid too, complete the
`INSTALL-GRID.md` included in the grid package first.

Development snapshots can now be assembled as release ZIPs. Public release
publication and an installer remain future work.
Archive names, service metadata, and the packaged `VERSION` file are derived
from the repository's root `VERSION` source when the package is built.

## Information to obtain from the grid operator

Before installation, ask for:

- The grid API URL reachable from the region host.
- The grid public URL advertised to viewers.
- The secret region service token.
- The approved region name and X/Y grid coordinates.
- The public hostname or address viewers will use for this region.
- Approved HTTP/TCP and viewer/UDP ports.

Treat the service token as a password. It permits authenticated region API
calls and must not appear in screenshots, public logs, or source control.

## Expected region package

The `homeworldz-region-<version>-windows-x64.zip` or future
`homeworldz-region-<version>-linux-x64.tar.gz` will contain a prebuilt region
program and its static assets:

```text
homeworldz-region/
  INSTALL-REGION.md
  INSTALL-GRID.md         # reference for operators hosting both services
  VERSION
  homeworldz-region[.exe]
  *.dll                 # Windows runtime libraries, when required
  config/
    examples/
      region.ini
      region-personal.ini
      region-cloud.ini
  assets/
    region/
      terrain/plateau-square.raw
      ...
  docs/
    FEATURES.md
    ROADMAP.md
```

Extract the package into a dedicated directory. The packaged region will not
require Go, Node.js, Visual Studio, CMake, vcpkg, PostgreSQL, or a compiler.

## Prepare data and backups

Create a private, writable data directory outside replaceable program files
when possible. The current default is `var/region`. At first startup the region
creates:

```text
var/region/
  region.db
  assets/
  logs/
  scene/
```

Every region process requires a different data directory. `region.db`, the
content-addressed `assets` tree, and `scene/snapshot.json` together constitute
the local persistent state; back them up as one consistent set while the
region is stopped.

## Configure the region

Copy the packaged example matching the grid deployment to the live, private
configuration file. Use `region-personal.ini` with `grid-personal.ini`, or
`region-cloud.ini` with `grid-cloud.ini`:

```cmd
copy config\examples\region-personal.ini config\region.ini
```

Edit `config/region.ini` with the values supplied by the grid operator. The
region executable reads this file directly. In particular, set the region
name and coordinates, its viewer-reachable public endpoint, the grid URLs, and
the private service token.

Runtime configuration is file-only. The public grid URL is used in
viewer-facing responses, and the public region endpoint must be reachable by
viewers. Service managers should pass a different file explicitly with
`--config`; they do not construct process environments from individual
settings.

Without the service token, the process can start for isolated health testing,
but it does not register and cannot accept authenticated viewer circuits.

## Start and stop

Start in a terminal for the first run so registration or storage errors remain
visible:

Windows:

```cmd
homeworldz-region.exe --config config\region.ini
```

Linux:

```sh
./homeworldz-region --config config/region.ini
```

Run it from the extracted package directory so relative asset and terrain paths
resolve. Stop it with Ctrl+C. A future installer is expected to add
Windows-service and systemd integration, but neither is implemented yet.

The startup log should report terrain loading, local storage initialization,
successful grid registration, and both listening ports. When a service token
is configured, the process exits if registration fails.

Default endpoints:

- Region capabilities and operational HTTP: `127.0.0.1:42001/tcp`
- Viewer circuit: `127.0.0.1:42002/udp`
- Liveness: `http://127.0.0.1:42001/ping`
- Readiness: `http://127.0.0.1:42001/ready`
- Version: `http://127.0.0.1:42001/version`

Verify locally with `curl.exe --fail http://127.0.0.1:42001/ready` on Windows
or `curl --fail http://127.0.0.1:42001/ready` on Linux. Ask the grid operator to
confirm registration at the assigned coordinates before inviting viewers.

## Runtime settings

| INI setting | Purpose | Default |
| --- | --- | --- |
| `region.http_port` | Region HTTP port | `42001` |
| `region.viewer_port` | Viewer UDP port | `42002` |
| `region.bind_address` | Region HTTP IPv4 bind address | `127.0.0.1` |
| `region.viewer_bind_address` | Viewer UDP IPv4 bind address | `127.0.0.1` |
| `region.name` | Registered region name | `My Region` |
| `region.grid_x` | Assigned grid X coordinate | `1000` |
| `region.grid_y` | Assigned grid Y coordinate | `1000` |
| `region.public_endpoint` | Viewer-reachable region HTTP URL | `http://localhost:42001` |
| `region.lease_seconds` | Registration lease (10–300 seconds) | `60` |
| `grid.url` | Grid API base URL | `http://localhost:42000` |
| `grid.public_url` | Viewer-facing grid base URL | Grid URL |
| `grid.service_token` | Transitional region authentication secret | Empty |
| `region.data_path` | Local scene and uploaded-asset state | `var/region` |
| `region.asset_path` | Static assets imported at startup | `assets/region` |
| `region.terrain_path` | 256×256 byte RAW terrain | `assets/region/terrain/plateau-square.raw` |

Bind addresses must be IPv4 addresses. Invalid numeric values fall back to
their defaults.

## Firewall and remote viewers

The loopback defaults work only when the viewer is on the region host and avoid
firewall prompts. For remote viewers:

1. Set both bind addresses to the host's specific IPv4 address, or deliberately
   use `0.0.0.0` when all interfaces are intended.
2. Open the chosen region HTTP **TCP** port and viewer circuit **UDP** port.
3. Forward both ports through NAT when applicable.
4. Put the reachable host and HTTP port in the public region endpoint.
5. Restrict firewall rules as narrowly as the deployment permits.

Do not add PostgreSQL access or broad executable exceptions for a region host.
Voice is not initially provided by HomeWorldz; a viewer may still prompt for
its own voice executable, but that is separate from the two region ports.

The range `42010`–`42099` is reserved for additional local regions and
development services. Each additional region needs unique HTTP and UDP ports,
name, grid coordinates, public endpoint, and data path.

## Upgrades and restoration

Before upgrading:

1. Notify the grid operator and log out viewers.
2. Stop the region cleanly.
3. Back up the entire configured data directory as one set.
4. Back up the region settings and service token securely.
5. Retain the old program package for rollback.

Extract the replacement package separately, restore the settings, point it at
the existing data directory, and start it. Verify `/version`, `/ready`, grid
registration, terrain, and an avatar login before deleting the old package.
Never overwrite the only copy of the data directory during an upgrade.

To restore, stop the process, move the damaged data directory aside, restore
the complete stopped-state backup, and restart. Coordinate any rollback that
changes identity or grid coordinates with the grid operator.

Building development snapshots from source is documented in the repository
README and is intentionally outside this region-owner installation guide.
