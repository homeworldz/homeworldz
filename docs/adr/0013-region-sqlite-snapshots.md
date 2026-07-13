# ADR 0013: Region SQLite Metadata And Atomic Snapshots

Status: Accepted

Each region keeps operational scene metadata in `region.db` using SQLite WAL
mode. The authoritative scene is serialized as deterministic JSON ordered by
entity ID and written to `scene/snapshot.json` through a same-directory
temporary file and atomic replacement. SQLite records the committed snapshot
revision and relative path after replacement.

Windows builds obtain pinned SQLite packages from the repository vcpkg
manifest; Linux builds use the distribution SQLite development package. The
region saves snapshots every 30 seconds and once more during orderly shutdown.
