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

Run grid tests from `grid/` with `go test ./...`. The grid service listens on
`:8080` by default and reads Postgres from `HOMEWORLDZ_DATABASE_URL`.

Start development Postgres with:

```powershell
docker compose -f deploy/compose.yaml up -d
```

Configure and build the region service with CMake presets:

```powershell
cmake --preset default
cmake --build --preset default
```

## Documentation

- [Implementation plan](docs/PLAN.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Physics evaluation](docs/PHYSICS.md)
