# Homeworldz management site

The **management site** served at `my.homeworldz.com`: login, registration,
email verification, account self-service, and user/region/system
administration. It is a thin client over the public `/v1` API in
[`grid/internal/api`](../grid/internal/api), which is why it lives in this
repository — every page it renders is a `/v1` call, so the two halves of that
contract version in lockstep and cannot drift.

The marketing site (`homeworldz.com`, its own repository) links here with
ordinary anchors; the two share no build and no session machinery. The
management pages were moved here from that repository on 2026-07-26, verbatim
where possible, so the two sites intentionally share conventions: SolidJS with
plain JSX (never TypeScript), Vite, the Vitre design system, pnpm exclusively.

Per [docs/STYLE.md](../docs/STYLE.md), this is the management site, never the
"frontend" — that word is reserved for the Homeworldz client's rendering
layers.

## Commands

- `pnpm install` — install locked dependencies
- `pnpm dev` — Vite dev server at `http://127.0.0.1:43220/` (fixed in
  `vite.config.js`, distinct from the marketing site's 43210 so both can run
  side by side)
- `pnpm build` — static bundle in `dist/`
- `pnpm preview` — serve the built bundle

There is no test suite; a successful `pnpm build` is the minimum validation,
plus a manual pass over affected routes.

## Layout

- **`src/index.jsx`** — entry point and routes. `/` redirects to `/account`;
  `RequireAuth` gates `/account`, `RequireAdmin` gates `/admin/*`.
- **`src/App.jsx`** — shared header and navigation. The link back to the
  marketing site is a plain `href`.
- **`src/pages/`** — Login, Register, Verify, Account, and the Admin pages
  (dashboard, users, user, regions, region, system).
- **`src/lib/api.js`** — the `/v1` fetch wrapper; `src/config.js` reads
  `VITE_API_BASE_URL` and defaults to `https://api.homeworldz.com/v1`.
- **`src/styles.css`** — moved whole from the marketing site; it still
  contains that site's roadmap/landing rules, which are inert here and can be
  trimmed as the two sites' styles diverge.
- **`public/_redirects`** — SPA fallback (`/* /index.html 200`) for static
  hosting; direct navigation to a router path must reach `index.html`.

## Cutover configuration (server side)

Two grid-side settings point at this site and change when `my.homeworldz.com`
goes live:

- `[website] allowed_origins` must include `https://my.homeworldz.com` (and a
  dev origin such as `http://127.0.0.1:43220` where wanted) or the browser
  blocks every API call.
- `[mail] verification_url` defaults to `https://homeworldz.com/verify` and
  must become `https://my.homeworldz.com/verify` once registration lives here,
  or emailed verification links land on a site without the page.
