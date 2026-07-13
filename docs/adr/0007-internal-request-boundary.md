# ADR 0007: Internal Request Boundary

Status: Accepted

Grid routes under `/api/` require a configured service token in the standard
`Authorization: Bearer <token>` header. Operational `/ping`, `/ready`, and
`/version` routes remain unauthenticated so operators and process supervisors
can inspect service state without receiving internal API credentials.

Both services accept safe caller-provided `X-Request-ID` values and generate an
identifier when the header is absent or unsafe. The identifier is returned in
the response and included in structured JSON request logs. Authentication
credentials and other secrets are never logged.
