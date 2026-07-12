# HomeWorldz

HomeWorldz is the planned modernized successor to Halcyon, a virtual world
simulation server compatible with the Second Life viewer protocol.

This folder is currently for planning and coordination. It does not yet contain
runtime code for a new server implementation.

## Goals

- Preserve Halcyon's core virtual world behavior and Second Life viewer
  compatibility.
- Establish a reproducible build and test baseline for the existing projects.
- Modernize the C# runtime and dependency model in controlled stages.
- Add Postgres storage support while preserving MySQL during the transition.
- Reduce long-term dependency on centralized WHIP/Aperture asset services by
  first introducing a cleaner asset storage abstraction.
- Leave room for a future per-region or decentralized asset architecture.

## Current Plan

See [PLAN.md](PLAN.md) for the working modernization roadmap.

