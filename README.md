# HomeWorldz

HomeWorldz is a clean-architecture virtual world server targeting practical
Firestorm compatibility. It is a new implementation informed by Halcyon,
OpenSimulator, and the Second Life viewer protocol without preserving their
internal service boundaries or storage formats.

The intended architecture uses a C++20 region server, a Go grid service,
HTTP/JSON APIs described by OpenAPI, Postgres for central state, and
region-local scene and asset storage.

This repository is currently in its planning and foundation phase.

## Documentation

- [Implementation plan](docs/PLAN.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Physics evaluation](docs/PHYSICS.md)

