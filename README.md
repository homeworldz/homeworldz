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

Prerequisites are Go 1.21 or newer, a C++20 toolchain with CMake 3.24 or newer,
and PostgreSQL 16 or newer. PostgreSQL 18.4 is recommended for new
installations. PostgreSQL may run locally or on another reachable host;
HomeWorldz does not require Docker.

Runtime configuration uses ordinary INI files in `config/`. Start from the
matching files in `config/examples/`: `grid.ini` controls grid server and client
settings, `db.ini` contains central database access, and `region.ini` controls
one region instance. Files directly under `config/` are ignored by Git because
they may contain credentials. Environment variables override INI values.

Grid operators bootstrap the application role, database, and initial migration
from PowerShell. The script prompts securely for the PostgreSQL administrator
password and the password to assign to the `homeworldz` application role:

```powershell
.\scripts\bootstrap-grid.ps1
```

Use `-HostName`, `-Port`, `-AdminUser`, or `-PsqlPath` when PostgreSQL is remote
or `psql` is not discoverable automatically.

Region operators prepare region-local scene, asset, and log storage separately:

```powershell
.\scripts\bootstrap-region.ps1
```

Region operators do not need PostgreSQL credentials. The region will use its
local storage and authenticated grid APIs once those components are implemented.

Run grid tests from `grid/` with `go test ./...`. The grid service listens on
`:42000` by default and reads `config/grid.ini` and `config/db.ini`. The region
service HTTP API listens on `:42001` by default.

Configure and build the region service with CMake presets:

```powershell
cmake --preset default
cmake --build --preset default
```

## Documentation

- [Implementation plan](docs/PLAN.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Physics evaluation](docs/PHYSICS.md)
