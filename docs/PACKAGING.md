# Release Packaging

HomeWorldz produces separate packages because grid operators and region owners
have different trust boundaries, configuration, storage, and dependencies.
Neither package contains source code, test programs, compilers, development
settings, or private configuration.

The grid package contains the grid service, database bootstrap and Library
configuration programs, PostgreSQL migrations, the example grid configuration,
and `INSTALL-GRID.md`. The region package contains the native region service,
its adjacent dependency DLLs, app-local Microsoft Visual C++ runtime DLLs,
static region assets, the example region configuration, and
`INSTALL-REGION.md`.

On a Windows or Linux x64 development host with the native region already
built, run:

```cmd
scripts\package-release.cmd -version 0.1.0-preview.1
```

Linux:

```sh
go run ./grid/cmd/package-release -version 0.1.0-preview.1 \
  -region-executable build/release/region/homeworldz-region
```

Use `-region-executable` to select a particular CMake output. Archives are
written to `dist` by default; `-output` changes that directory. The command
also writes `SHA256SUMS` for transport verification.

Grid operators can build the grid-owner archive without a native C++ region
build by passing `-grid-only`. This is useful when preparing a central grid on
a minimal Linux host; it does not weaken or alter the resulting grid package.

The tool emits deterministic ZIP files on Windows and deterministic `tar.gz`
files on Linux. Linux packaging validates the region executable with `ldd` and
fails when a shared dependency is unresolved; it relies on the distribution's
glibc, C++ runtime, and SQLite runtime rather than bundling core operating
system libraries. Release automation should build the C++ region in Release
mode with `-DHOMEWORLDZ_VERSION=<version>` before packaging; the local Windows
fallback to a Debug executable exists only so developers can validate package
composition before a formal release build.
