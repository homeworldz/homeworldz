# ADR 0003: Operational Endpoints

Status: Accepted

`/ping` is a lightweight liveness endpoint and performs no dependency checks.
`/ready` reports whether the process can serve useful traffic, including its
required storage and startup state. `/version` identifies the service, build,
and internal API version. The names are intentionally plain and avoid the
Kubernetes-derived `healthz` and `readyz` convention.

