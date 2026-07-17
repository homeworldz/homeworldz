# HomeWorldz website API contract

`openapi.yaml` is the proposed browser-facing contract served from
`https://api.homeworldz.com/v1`. It is intentionally separate from:

- viewer login and simulator traffic at `grid.homeworldz.com`; and
- the Grid's service-token-protected internal `/api/v1` contract.

## Authentication boundary

Registration and `POST /tokens` are unauthenticated and rate-limited. All other
operations accept only a short-lived website JWT in the `Authorization: Bearer`
header. Tokens do not create or reuse viewer sessions.

The JWT carries the complete website identity (`sub`, `userid`, `displayName`,
`rezDate`, `privs`) and an authorization-version claim (`ver`). The API still
loads current account state for privileged operations. Password, privilege,
and ban changes increment the user's authorization version so previously issued
tokens stop authorizing requests without a stored website-session record.

## Privileges

The initial named privileges are `users`, `bans`, `regions`, `map`, `deploy`,
`undeploy`, `admin`, and `super`. `admin` initially expands to `users`, `bans`,
`regions`, `map`, and `deploy`; the destructive `undeploy` capability must be
assigned separately. `super` grants every current and future privilege.
Endpoint-specific requirements appear as `x-required-privilege` in the OpenAPI
operations.

## Browser access

The API must allow only configured website origins, initially
`https://homeworldz.com` and `https://www.homeworldz.com`. CORS preflight must
allow `Authorization`, `Content-Type`, and `X-Request-ID`, and responses should
expose `X-Request-ID`. Development origins should be configured separately and
must not broaden the production allowlist.

All responses containing tokens use `Cache-Control: no-store`. Passwords,
tokens, access keys, and authorization headers must never be logged.
