# ADR 0006: Operator Bootstrap Boundaries

Status: Accepted

Grid and region operators use separate bootstrap workflows. Central grid hosts
require PostgreSQL 16 or newer. Grid operators run
`go run ./grid/cmd/bootstrap-grid` to initialize central storage and local
database configuration. Region operators run `scripts/bootstrap-region.ps1` to
initialize region-local scene, asset, and log storage.

Region operators do not receive central PostgreSQL credentials. Regions access
identity, sessions, registration, presence, and other central state through
authenticated grid APIs. Both scripts will grow with their respective service
configuration and migration requirements.
