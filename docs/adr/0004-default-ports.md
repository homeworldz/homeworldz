# ADR 0004: Default Development Ports

Status: Accepted

HomeWorldz uses `42000/tcp` for the grid HTTP API, `42001/tcp` for the region
HTTP API, and reserves `42002/udp` for the viewer circuit. The range
`42010-42099` is reserved for additional local regions and supporting
development services.

These are defaults rather than wire-protocol requirements. Deployments can
override them to avoid host conflicts or satisfy network policy.

