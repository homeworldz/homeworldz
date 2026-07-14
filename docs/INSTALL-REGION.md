# Install a HomeWorldz Region

This guide is for a **region owner** connecting a region to an existing
HomeWorldz grid. You do not need PostgreSQL, its credentials, or access to the
grid database. If you operate the central grid too, complete
[INSTALL-GRID.md](INSTALL-GRID.md) first.

Development snapshots can now be assembled as release ZIPs. Public release
publication and an installer remain future work.

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
  homeworldz-region[.exe]
  *.dll                 # Windows runtime libraries, when required
  config/
    examples/region.ini
  assets/
    region/
      terrain/plateau-square.raw
      ...
```

Extract the package into a dedicated directory. The packaged region will not
require Go, Node.js, Visual Studio, CMake, vcpkg, PostgreSQL, or a compiler.

The intended release launcher will translate `config/region.ini` into runtime
settings. That launcher is not implemented yet: the current preview executable
uses the environment variables shown below. This guide does not claim that the
example INI is already consumed by the executable.

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

## Configure the current preview

Set values in the same terminal that will start the program. Windows Command
Prompt example:

```cmd
set HOMEWORLDZ_GRID_SERVICE_TOKEN=replace-with-token-from-grid-operator
set HOMEWORLDZ_GRID_URL=https://grid.example
set HOMEWORLDZ_GRID_PUBLIC_URL=https://grid.example
set HOMEWORLDZ_REGION_NAME=My Region
set HOMEWORLDZ_REGION_GRID_X=1000
set HOMEWORLDZ_REGION_GRID_Y=1000
set HOMEWORLDZ_REGION_PUBLIC_ENDPOINT=http://region.example:42001
set HOMEWORLDZ_REGION_DATA_PATH=D:\HomeWorldz\regions\my-region
set HOMEWORLDZ_REGION_ASSET_PATH=assets\region
```

Linux example:

```sh
export HOMEWORLDZ_GRID_SERVICE_TOKEN='replace-with-token-from-grid-operator'
export HOMEWORLDZ_GRID_URL='https://grid.example'
export HOMEWORLDZ_GRID_PUBLIC_URL='https://grid.example'
export HOMEWORLDZ_REGION_NAME='My Region'
export HOMEWORLDZ_REGION_GRID_X=1000
export HOMEWORLDZ_REGION_GRID_Y=1000
export HOMEWORLDZ_REGION_PUBLIC_ENDPOINT='http://region.example:42001'
export HOMEWORLDZ_REGION_DATA_PATH='/srv/homeworldz/regions/my-region'
export HOMEWORLDZ_REGION_ASSET_PATH='assets/region'
```

`HOMEWORLDZ_GRID_URL` is the grid API destination. The public grid URL is used
in viewer-facing responses. The public region endpoint must be reachable by
viewers and its port must match `HOMEWORLDZ_REGION_PORT` when overridden.

Without the service token, the process can start for isolated health testing,
but it does not register and cannot accept authenticated viewer circuits.

## Start and stop

Start in a terminal for the first run so registration or storage errors remain
visible:

Windows:

```cmd
homeworldz-region.exe
```

Linux:

```sh
./homeworldz-region
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

| Variable | Purpose | Default |
| --- | --- | --- |
| `HOMEWORLDZ_REGION_PORT` | Region HTTP port | `42001` |
| `HOMEWORLDZ_VIEWER_PORT` | Viewer UDP port | `42002` |
| `HOMEWORLDZ_REGION_BIND_ADDRESS` | Region HTTP IPv4 bind address | `127.0.0.1` |
| `HOMEWORLDZ_VIEWER_BIND_ADDRESS` | Viewer UDP IPv4 bind address | `127.0.0.1` |
| `HOMEWORLDZ_REGION_NAME` | Registered region name | `My Region` |
| `HOMEWORLDZ_REGION_GRID_X` | Assigned grid X coordinate | `1000` |
| `HOMEWORLDZ_REGION_GRID_Y` | Assigned grid Y coordinate | `1000` |
| `HOMEWORLDZ_REGION_PUBLIC_ENDPOINT` | Viewer-reachable region HTTP URL | `http://localhost:42001` |
| `HOMEWORLDZ_REGION_LEASE_SECONDS` | Registration lease (10–300 seconds) | `60` |
| `HOMEWORLDZ_GRID_URL` | Grid API base URL | `http://localhost:42000` |
| `HOMEWORLDZ_GRID_PUBLIC_URL` | Viewer-facing grid base URL | Grid URL |
| `HOMEWORLDZ_GRID_SERVICE_TOKEN` | Region authentication secret | Empty |
| `HOMEWORLDZ_REGION_DATA_PATH` | Local scene and uploaded-asset state | `var/region` |
| `HOMEWORLDZ_REGION_ASSET_PATH` | Static assets imported at startup | `assets/region` |
| `HOMEWORLDZ_REGION_TERRAIN_PATH` | 256×256 byte RAW terrain | `assets/region/terrain/plateau-square.raw` |

Bind addresses must be IPv4 addresses. Invalid numeric values fall back to
their defaults. Windows environment changes made through Control Panel are not
inherited by terminals already open; open a fresh terminal before launch.

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
