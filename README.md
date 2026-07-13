# HomeWorldz

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

Create a database and apply the initial migration using your PostgreSQL
installation. For example, from PowerShell:

```powershell
$env:HOMEWORLDZ_DATABASE_URL = "postgres://homeworldz:password@localhost:5432/homeworldz?sslmode=disable"
psql $env:HOMEWORLDZ_DATABASE_URL -f db/migrations/000001_initial.up.sql
```

Run grid tests from `grid/` with `go test ./...`. The grid service listens on
`:42000` by default and reads PostgreSQL from `HOMEWORLDZ_DATABASE_URL`. The
region service HTTP API listens on `:42001` by default.

Configure and build the region service with CMake presets:

```powershell
cmake --preset default
cmake --build --preset default
```

## Documentation

- [Implementation plan](docs/PLAN.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Physics evaluation](docs/PHYSICS.md)
