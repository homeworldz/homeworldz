# ADR 0002: Physics Evaluation Gate

Status: Accepted

The region owns authoritative scene state and talks to physics through an
engine-independent interface. Jolt and PhysX 5 were evaluated through the same
Windows and Linux scenario harness described in `../PHYSICS.md`; results are in
`../PHYSICS_RESULTS.md`.

Jolt 5.5 is selected as the default HomeWorldz v1 physics backend. It passed all
required first-gate scenarios. It used substantially less suite CPU time on
both measured platforms, had the better Windows tick results, and has the
smaller dependency and packaging surface. PhysX had better average tick latency
in most small WSL scenarios, but that did not outweigh Jolt's overall CPU and
integration advantages for the initial region server.

The PhysX adapter remains buildable and must remain usable by the shared lab so
later avatar, mesh, constraint, vehicle, or native-Linux evidence can trigger a
new decision without changing the region-facing API. This decision does not
claim cross-platform bitwise determinism or production capacity.

