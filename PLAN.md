# HomeWorldz Modernization Plan

## Project Identity

HomeWorldz is the planned modernized successor to the Halcyon virtual world
simulation server. The goal is to preserve compatibility with the Second Life
viewer protocol and Halcyon's core simulation behavior while updating the
runtime, storage, build system, and dependency architecture.

HomeWorldz should start as a conservative modernization effort rather than a
rewrite. The first priority is to make the existing system understandable,
buildable, testable, and replaceable in controlled layers.

## Current Repository Landscape

The current workspace contains several related projects:

- `halcyon`: Main virtual world simulator derived from OpenSimulator and
  customized for the InWorldz/Halcyon architecture.
- `aperture`: C++ asset delivery/cache service used to serve asset data to
  viewers over HTTP.
- `whip-server`: Primary distributed immutable asset storage server.
- `whip-dotnet-client`: .NET client library and utilities for WHIP.
- `phlox`: Linden Scripting Language compiler and VM used by Halcyon.
- `PhysX.net`: Patched C# wrapper around NVIDIA PhysX used by Halcyon physics.
- `halcyon-physx`: Experimental C# PhysX conversion/generator project.
- `halcyon-import-export`: Import/export tools for Halcyon servers.
- `halcyon-setuptools`: Setup and database helper tooling.
- `AIS`: Incomplete AISv3 inventory REST API prototype; not currently part of
  the active modernization path.

## Modernization Strategy

The first milestone is a reproducible build and test baseline for the active
projects. This should happen before significant behavioral changes, because the
existing codebase has many legacy dependencies, vendored binaries, plugin
boundaries, and platform-specific assumptions.

The preferred runtime direction is a staged migration to .NET 8 LTS. Existing
.NET Framework projects should remain buildable until their replacements pass
equivalent tests. Project conversion should move in dependency order, beginning
with leaf libraries and tests before the main simulator executable.

Dependency reduction should be incremental:

- Move from `packages.config` and vendored DLL references to `PackageReference`
  where maintained packages exist.
- Replace vendored third-party source only when a supported equivalent exists or
  a small compatibility shim is safer.
- Keep high-risk components such as PhysX, OpenJPEG, viewer protocol handling,
  and script execution behind stable interfaces during the first phases.

## Storage Direction

Postgres should be introduced as a parallel provider rather than as a direct
MySQL replacement.

The initial storage work should add:

- `OpenSim.Data.Postgres` for grid, region, estate, user, and asset metadata
  persistence where the existing interfaces support it.
- `Halcyon.Data.Inventory.Postgres` implementing the current inventory storage
  interface.
- Postgres support in the simple database connection factory, backed by Npgsql.
- Config-driven provider selection so hard-coded MySQL call sites can be
  replaced without breaking existing deployments.

MySQL should remain supported until the Postgres provider has equivalent test
coverage and migration tooling exists.

## Asset Direction

The current asset path depends on WHIP, Aperture, and the Stratus asset client.
HomeWorldz should not replace those services immediately. Instead, the first
asset milestone is a cleaner storage boundary that can keep current behavior
working while allowing future replacement.

The new asset abstraction should support:

- Immutable asset writes.
- Synchronous and asynchronous reads.
- Asset existence checks.
- Metadata access.
- Streaming reads for large assets.
- Compatibility adapters for current WHIP-backed behavior.
- A local filesystem test backend.

Once this boundary is stable, HomeWorldz can add a per-region asset provider
model where region servers can serve region-local assets directly while global
or library assets continue to resolve through a shared backend.

## Native Services

WHIP and Aperture should be kept operational during the first phases. Their C++
builds should be updated from obsolete Conan 1/Bincrafters assumptions to a
modern CMake dependency workflow, such as Conan 2 or vcpkg.

Native service rewrites should wait until the C# asset abstraction and adapter
tests prove that runtime behavior can be preserved.

## Physics Direction

The current PhysX integration should remain in place initially. HomeWorldz
should isolate PhysX behind the existing physics plugin boundary before
attempting a runtime migration, wrapper replacement, or managed physics
alternative.

`halcyon-physx` should be treated as experimental unless it becomes useful for a
later wrapper replacement effort.

## Test And Acceptance Plan

The first baseline should capture:

- Current build status for each active subproject.
- Existing NUnit test results for the Halcyon assemblies listed in the local
  repository guidance.
- Minimal standalone region startup.
- Viewer login compatibility.
- Inventory load.
- Object rez, persistence, restart, and reload.
- Script compile and execution through Phlox.
- Texture, mesh, and asset delivery through the current asset path.

Postgres provider acceptance should include parity tests against the MySQL
providers for inventory, region persistence, grid data, estate data, user data,
asset data, UUID handling, binary data, timestamps, transactions, and paging.

Asset abstraction acceptance should include tests for WHIP adapter parity,
duplicate immutable writes, not-found behavior, metadata reads, streaming reads,
and local test storage.

## Assumptions

- HomeWorldz starts as a modernization/fork plan, not a new code project.
- Second Life viewer compatibility is a hard requirement.
- WHIP and Aperture remain supported until an adapter-backed replacement is
  proven.
- MySQL remains supported until Postgres reaches tested parity.
- AIS is excluded from active modernization unless AISv3 inventory APIs become a
  specific project goal.
- The initial deliverable is a stable build/test baseline and dependency map,
  followed by staged runtime, storage, and asset replacement work.
