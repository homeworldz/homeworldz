# Release Packaging

HomeWorldz produces separate packages because grid operators and region owners
have different trust boundaries, configuration, storage, and dependencies.
Neither package contains source code, test programs, compilers, development
settings, or private configuration.

The grid package contains the grid service, database bootstrap and Library
configuration programs, PostgreSQL migrations, the example grid configuration,
and `INSTALL-GRID.md`. The region package contains the native region service,
its adjacent runtime DLLs, static region assets, the example region
configuration, and `INSTALL-REGION.md`.

On a Windows x64 development host with the region already built, run:

```cmd
scripts\package-release.cmd -version 0.1.0-preview.1
```

Use `-region-executable` to select a particular CMake output. Archives are
written to `dist` by default; `-output` changes that directory. The command
also writes `SHA256SUMS` for transport verification.

Packaging currently targets native Windows x64. Linux x64 archives should use
the same manifests once native C++ dependency collection and `tar.gz` output
are implemented. Release automation should build the C++ region in Release
mode with `-DHOMEWORLDZ_VERSION=<version>` before packaging; the local fallback
to a Debug executable exists only so developers can validate package
composition before a formal release build.
