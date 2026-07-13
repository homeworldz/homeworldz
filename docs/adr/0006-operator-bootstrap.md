# ADR 0006: Operator Bootstrap Boundaries

Status: Accepted

Grid and region operators use separate bootstrap workflows. Grid operators own
PostgreSQL and run `scripts/bootstrap-grid.ps1` to initialize central storage.
Region operators run `scripts/bootstrap-region.ps1` to initialize region-local
scene, asset, and log storage.

Region operators do not receive central PostgreSQL credentials. Regions access
identity, sessions, registration, presence, and other central state through
authenticated grid APIs. Both scripts will grow with their respective service
configuration and migration requirements.

