# ADR 0001: Initial System Shape

Status: Accepted

HomeWorldz uses a C++20 region server and a Go grid service in one repository.
Internal service calls use versioned HTTP/JSON contracts documented with
OpenAPI. Central state uses Postgres; region-local metadata uses SQLite and
immutable asset data uses content-addressed filesystem blobs.

