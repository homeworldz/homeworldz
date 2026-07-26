# Homeworldz management site

This directory will house the **management site** served at
`my.homeworldz.com`: login, registration, account self-service, and region
management. It is a thin client over the public `/v1` API in
[`grid/internal/api`](../grid/internal/api), which is why it lives in this
repository — every page it renders is a `/v1` call, so the two halves of that
contract version in lockstep and cannot drift.

The marketing site (`homeworldz.com`, its own repository) links here with
ordinary anchors; the two share no build and no session machinery.

Per [docs/STYLE.md](../docs/STYLE.md), this is the management site, never the
"frontend" — that word is reserved for the Homeworldz client's rendering
layers.

Nothing is implemented yet; this file records the direction so the directory
exists at the repository boundary it belongs to.
