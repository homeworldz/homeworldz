<div style="text-align: left; width: 320px; margin: 0;">
  <img src="homeworldz.svg" alt="HomeWorldz logo" width="320" style="display: block; margin: 0;">
</div>

HomeWorldz is a clean-architecture virtual world server targeting practical
Firestorm compatibility. It is a new implementation informed by Halcyon,
OpenSimulator, and the Second Life viewer protocol without preserving their
internal service boundaries or storage formats.

The intended architecture uses a C++20 region server, a Go grid service,
HTTP/JSON APIs described by OpenAPI, Postgres for central state, and
region-local scene and asset storage.

This repository is currently in its planning and foundation phase.

## Development

Central grid hosts require Go 1.21 or newer and a reachable PostgreSQL 16 or
newer installation. PostgreSQL 18.4 is recommended for new installations.
Region builds require a C++20 toolchain with CMake 3.24 or newer. PostgreSQL may
run locally or on another reachable host; HomeWorldz does not require Docker.

Runtime configuration uses ordinary INI files in `config/`. Start from the
matching files in `config/examples/`: `grid.ini` controls grid server and client
settings, `db.ini` contains central database access, and `region.ini` controls
one region instance. Files directly under `config/` are ignored by Git because
they may contain credentials. Environment variables override INI values.

After installing PostgreSQL, grid operators bootstrap the application role,
database, all pending migrations, and ignored `config/db.ini`. The cross-platform Go
command securely prompts for the PostgreSQL administrator password and the
password to assign to the `homeworldz` application role:

```text
go run ./grid/cmd/bootstrap-grid
```

Use `-host`, `-port`, or `-admin-user` when PostgreSQL is remote or uses
non-default connection settings. The command uses the Go PostgreSQL driver and
does not require `psql` on `PATH`.

Region operators prepare region-local scene, asset, and log storage separately:

```powershell
.\scripts\bootstrap-region.ps1
```

Region operators do not need PostgreSQL credentials. The region will use its
local storage and authenticated grid APIs once those components are implemented.

Run grid tests from `grid/` with `go test ./...`. The grid service listens on
`127.0.0.1:42000` by default and reads `config/grid.ini` and `config/db.ini`.
The region HTTP and viewer UDP services also bind to `127.0.0.1` by default, on
ports `42001` and `42002` respectively. Loopback defaults avoid exposing local
development services or requiring Windows Firewall exceptions.

Firestorm discovers the local grid from `http://127.0.0.1:42000/` through the
standard `/get_grid_info` endpoint. `HOMEWORLDZ_GRID_PUBLIC_URL` overrides the
URLs advertised to viewers when the grid is exposed at a different address.

PostgreSQL lifecycle tests run when `HOMEWORLDZ_TEST_DATABASE_URL` is set and
otherwise skip cleanly. CI supplies a disposable PostgreSQL service and runs
the region lease, identity/session, and presence cleanup suites against it.

On Windows with Visual Studio, install the pinned vcpkg dependencies and build
the region service with:

```powershell
.\scripts\build-region.ps1 -Test
```

On Linux, install the SQLite development package and use the CMake presets:

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
```

To enable development registration with a running grid, set
`HOMEWORLDZ_GRID_SERVICE_TOKEN` to the grid service token. Optional region
overrides are `HOMEWORLDZ_GRID_URL`, `HOMEWORLDZ_REGION_NAME`,
`HOMEWORLDZ_REGION_GRID_X`, `HOMEWORLDZ_REGION_GRID_Y`,
`HOMEWORLDZ_REGION_PUBLIC_ENDPOINT`, and `HOMEWORLDZ_REGION_LEASE_SECONDS`.
`HOMEWORLDZ_VIEWER_PORT` selects the viewer UDP listener and defaults to `42002`.
Set `HOMEWORLDZ_REGION_BIND_ADDRESS` or `HOMEWORLDZ_VIEWER_BIND_ADDRESS` to an
explicit IPv4 address (or `0.0.0.0`) only when clients on other machines must
connect; configure narrowly scoped firewall rules separately for that deployment.
Without a service token, the region runs without grid registration.

## Documentation

- [Implementation plan](docs/PLAN.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Physics evaluation](docs/PHYSICS.md)
