# Release Packaging

Homeworldz produces separate packages because grid operators and region owners
have different trust boundaries, configuration, storage, and dependencies.
Neither package contains source code, test programs, compilers, development
settings, or private configuration.

The grid package contains the grid service, database bootstrap and Library
configuration programs, PostgreSQL migrations, the example grid configuration,
the Caddy/systemd deployment files, and `INSTALL-GRID.md`. The region package contains the native region service,
its adjacent dependency DLLs, app-local Microsoft Visual C++ runtime DLLs,
static region assets, the example region configuration, a multi-instance
systemd template, and
`INSTALL-REGION.md`.

On a Windows or Linux x64 development host with the native region already
built, run:

```cmd
scripts\package-release.cmd -version 0.1.0-preview.1
```

Linux:

```sh
export VCPKG_ROOT="$HOME/vcpkg"
./scripts/build-region.sh --test --version 0.1.0-preview.1
go run ./grid/cmd/package-release -version 0.1.0-preview.1 \
  -region-executable build/linux-release/region/homeworldz-region
```

The build script requires Jolt and prevents accidentally packaging a
physics-less Linux region.

Use `-region-executable` to select a particular CMake output. Archives are
written to `dist` by default; `-output` changes that directory. The command
also writes `SHA256SUMS` for transport verification.

Grid operators can build the grid-owner archive without a native C++ region
build by passing `-grid-only`. This is useful when preparing a central grid on
a minimal Linux host; it does not weaken or alter the resulting grid package.
The static Go grid tools can also be cross-compiled from Windows for Ubuntu:

```cmd
scripts\package-release.cmd -grid-only -target-os linux
```

Full packages containing the native C++ region must still be built on their
target operating system.

The tool emits deterministic ZIP files on Windows and deterministic `tar.gz`
files on Linux. Linux packaging validates the region executable with `ldd` and
fails when a shared dependency is unresolved; it relies on the distribution's
glibc, C++ runtime, and SQLite runtime rather than bundling core operating
system libraries. Release automation should build the C++ region in Release
mode with `-DHOMEWORLDZ_VERSION=<version>` before packaging; the local Windows
fallback to a Debug executable exists only so developers can validate package
composition before a formal release build.

## Where the version comes from, in precedence order

The root `VERSION` file is the single source. Three things can override it, and
because each is silent about the others, a deployment can report a version that
exists nowhere in the repository — which is exactly what happened on the OVH box
until 2026-08-05, where the answer was `0.1.0-ovh-preview`:

1. **A `VERSION` file in the region's working directory**, read at startup by
   `runtime_version()` in `region/src/main.cpp`. It wins over the compiled value,
   so a stale one survives every rebuild and redeploy. Delete it unless you mean
   to pin a version without recompiling.
2. **`-DHOMEWORLDZ_VERSION` in `CMakeCache.txt`.** CMake reads the `VERSION`
   file only when the variable is undefined, and a `-D` from any earlier
   configure persists — so editing `VERSION` in an existing build directory does
   nothing. `scripts/build-region.sh` now always passes the file's value to
   overwrite it.
3. **`build-region.sh --version`**, for a deliberate one-off.

The Go binaries read none of these yet and report `unstamped`.
