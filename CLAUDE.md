# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

The Homeworldz monorepo: the **grid** server (Go, `grid/`), the **region** server
(C++/Jolt, `region/`), the **script** VM (C++, `script/`), database migrations
(`db/migrations/`), and the architecture documentation and ADRs (`docs/`). The
public website is a separate repo, the sibling `../homeworldz.com`.

## Naming and terminology

**[docs/STYLE.md](docs/STYLE.md) is authoritative** — read it before writing
prose, log messages, CLI output, or user-visible strings.

The rules most easily got wrong:

- The name is **`Homeworldz`** in prose. Never `HomeWorldz`.
- Identifiers stay lowercase (`homeworldz::physics::World`, `homeworldz-region`,
  the `homeworldz` role) and environment variables upper snake (`HOMEWORLDZ_*`).
  That is language convention, not a stylization of the name.
- **"viewer"** means a third-party Second Life-lineage viewer (Firestorm and its
  peers), per [ADR 0016](docs/adr/0016-firestorm-compatibility-target.md). It
  never means the first-party client.
- The first-party client of [ADR 0030](docs/adr/0030-client-architecture.md) is
  just **Homeworldz**, or the **Homeworldz client** where the distinction from
  server software matters. **"frontend"** is reserved for that client's
  rendering layers.

## Build and test

Go (grid):

```
cd grid && go test ./...
```

C++ (region and script) needs the MSVC environment — `cmake` is not on `PATH`,
and building without `vcvars64.bat` fails to find the standard library:

```
cmd /c "\"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" >nul && cmake --build build\vcpkg --target <target>"
```

Test binaries land in `build/vcpkg/region/` and `build/vcpkg/script/` and are run
directly; exit code 0 is a pass. On the cloud box the region is built natively
with `scripts/build-region.sh`, never cross-compiled from Windows.

## Documentation

- ADRs in `docs/adr/` record decisions and are numbered sequentially; they state
  intent and are revised as evidence arrives rather than rewritten silently.
- `docs/ROADMAP.md` is vendored into the website repo. After editing it, run
  `node syncweb.mjs`, which copies it to `../homeworldz.com/content/ROADMAP.md`
  and leaves the change unstaged there for review.
- Markdown and plain text use native line endings (CRLF on Windows).
