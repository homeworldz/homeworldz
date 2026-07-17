# Repository Guidelines

## Project Structure & Module Organization

This directory contains the SolidJS marketing and administration frontend for HomeWorldz. It uses Vite without SolidStart or another application framework.

- `src/index.jsx` initializes Solid Router and the application.
- `src/App.jsx` contains shared navigation and page layout.
- `src/pages/` contains route-level components such as `LandingPage.jsx` and `ArchitecturePage.jsx`.
- `src/styles.css` contains Vitre token overrides and site-specific layout rules.
- Root-level SVG files are brand assets; `public/` contains files copied directly into the build.
- `api/openapi.yaml` defines the proposed JSON administration API at `api.homeworldz.com`.
- `../docs/ROADMAP.md` is imported at build time for the Roadmap route.

Generated output belongs in `dist/` and dependencies in `node_modules/`; neither should be committed.

## Build, Test, and Development Commands

Use pnpm exclusively. Do not add npm or Yarn lockfiles.

- `pnpm install` installs the locked dependencies.
- `pnpm dev` starts Vite with hot reloading at `http://127.0.0.1:43210/`.
- `pnpm build` produces the static Cloudflare Pages bundle in `dist/`.
- `pnpm preview` serves the production bundle locally for final inspection.

There is currently no automated test suite. Treat a successful production build as the minimum validation. Manually inspect affected routes and responsive layouts for visible changes.

## Coding Style & Naming Conventions

Use plain JavaScript and JSX only—never TypeScript or TSX. Follow the existing two-space indentation, semicolons, double quotes, and trailing commas. Name Solid components and files in `PascalCase`; use `camelCase` for functions and variables, and kebab-case for CSS classes. Prefer semantic HTML and Vitre CSS/JS primitives before adding custom presentation. Keep route components focused and accessible.

## Commit & Pull Request Guidelines

Recent commits use short, imperative summaries such as `Add roadmap progress dashboard` and `Support saving AIS outfit folders`. Keep each commit focused on one coherent change.

Pull requests should explain the user-visible outcome, list routes changed, and note `pnpm build` results. Include screenshots for layout or styling changes and identify any corresponding parent-project documentation or API changes.

## Security & Deployment

Never place service tokens, passwords, JWTs, or region access keys in source code. The site deploys as static files to Cloudflare Pages; preserve `public/_redirects` so direct route navigation reaches Solid Router.
