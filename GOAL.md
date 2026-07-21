# HomeWorldz Working Goal

This file is the primary handoff for continuing HomeWorldz in a fresh Codex
conversation. Read it before choosing work. The detailed design and acceptance
record remain in `docs/`, but this document captures the current objective,
constraints, working agreement, verified state, and immediate next step.

## Primary objective

Continue building HomeWorldz into a practical, creator-friendly virtual-world
grid and Region implementation compatible with the supported Firestorm viewer.
Advance useful work across the roadmap's phases in parallel; the phase numbers
describe product areas and broad sequencing, not gates that require one phase
to be complete before another can progress.

Work autonomously through coherent implementation steps. For each step:

1. Inspect the relevant code, documentation, protocol reference, and current
   repository state.
2. Implement the smallest coherent production change.
3. Add or update proportionate automated tests.
4. Run focused tests followed by the applicable full suite.
5. Update roadmap or feature documentation when the implementation state or an
   intentional product difference changes.
6. Commit the coherent step and push it to `origin/main`.
7. Deploy viewer-facing Region changes to the cloud Grid when appropriate,
   verify service health and logs, and ask for Firestorm feedback only when a
   visual or interactive acceptance test is genuinely required.
8. Continue to the next useful step until an interactive decision or viewer
   action requires the user.

Do not wait for GitHub Actions: the account has exhausted its included Actions
minutes. Use local Windows tests and direct Linux build/tests on the OVH host.

## Repository ownership boundary

This work owns the entire current HomeWorldz repository. There is no local
`website/` subtree. The HomeWorldz website now lives in the separate sibling
repository `../homeworldz.com` with its own conversation and tooling. Do not
edit, stage, commit, deploy, or otherwise operate on that sibling repository
unless the user explicitly requests cross-repository work. The Go Grid and its
website-facing backend APIs in this repository remain in scope.

The worktree may contain changes made by the user or another agent. Preserve
unrelated work and stage only the files belonging to the current coherent step.

## Current implementation focus

The immediate focus is the Falcon LSL integration while connected-region and
physics work remain valid parallel priorities.

The next concrete step is to connect Firestorm object-touch traffic to Falcon:

- identify and decode the initial `ObjectGrab`/touch packet separately from the
  already-supported physical `ObjectGrabUpdate` drag packet;
- authorize the touching avatar and resolve the clicked child/root object;
- dispatch `touch_start(integer total_number)` to each enabled compiled script
  in the appropriate object's task inventory, initially with detected count 1;
- preserve bounded cooperative scheduling and avoid firing duplicate
  `touch_start` events for grab-update motion;
- add packet, runtime-dispatch, and integration tests;
- deploy and confirm the existing corrected touch script says `Touched!` when
  the prim is clicked.

After basic touch acceptance, likely scripting priorities are:

- implement the corresponding sustained-touch and touch-end semantics;
- make scheduling fair across more scripts while retaining aggregate and
  per-script instruction budgets and adding wall-clock/resource guards;
- restore enabled compiled task scripts after Region restart and persist their
  VM state;
- expand the language, events, built-ins, types, states, and compatibility
  diagnostics toward complete Second Life LSL plus Halcyon/InWorldz support;
- carry script state atomically with objects, attachments, vehicles, and Region
  crossings.

Use `docs/ROADMAP.md` for the complete prioritized product scope. Its current
effort estimates are overall 27%, Phase 1 98%, Phase 2 70%, Phase 3 39%, Phase
4 15%, Phase 5 9%, Phase 6 12%, and Phase 7 2%. Revise these deliberately when
new evidence materially changes the estimates.

## Verified Falcon state

The following is implemented, tested, committed, deployed, and accepted in
Firestorm as of 2026-07-21:

- Firestorm `RezScript` creates a default script in prim Contents or copies or
  transfers an Inventory script into Contents.
- Creator-attributed script source can be created, retrieved, edited, saved,
  and reopened in personal Inventory and object Contents.
- Firestorm's terminal NUL on script-editor uploads is normalized before
  compilation.
- The dependency-free handwritten Falcon lexer, parser, type checker, compiler,
  versioned bytecode, and explicit-state VM support an initial typed subset.
- An enabled task script is compiled, instantiated, and sent `state_entry`.
- Task-script saves recompile and run the new instance; a failed compile leaves
  the previous valid runtime instance intact.
- Firestorm receives LLSD compile status and an escaped `errors` array.
  Lexical errors include line and column locations.
- `llSay(0, ...)` reaches nearby viewer object chat and `llOwnerSay(...)`
  reaches the owner.
- Falcon runs on the authoritative Region thread with bounded total and
  per-script instruction slices; an infinite loop yields rather than hanging
  the Region.
- VM snapshot tests restore after every completed instruction and preserve
  globals and a handler suspended in the middle of `touch_start`.
- Removing a task script removes its live VM.
- Scripted prims advertise the `SCRIPTED` and `HANDLE_TOUCH` object-update flags
  so Firestorm enables the Touch action; a script rez, recompile, enable,
  disable, or removal re-broadcasts the object update so the flag change reaches
  the viewer without a relog.
- The initial `ObjectGrab` touch packet is decoded distinctly from the physical
  `ObjectGrabUpdate` drag path and dispatches `touch_start(1)` to each enabled
  compiled script in the clicked prim and its linkset root through a bounded
  per-script event queue; grab-update drag motion fires no duplicate touch.
  Clicking a `touch_start` prim in Welcome produced its `Touched!` chat.
- Enabled task scripts are re-rezzed on Region startup so a restart no longer
  leaves a scripted prim non-touchable (each restart re-runs `state_entry`
  because VM state is not yet persisted across restarts).

Important limitations: sustained-touch and touch-end are not yet delivered,
running script instances are not restored after Region restart, the scheduler
is bounded but not yet fully fair or resource-accounted, and Falcon implements
only a small portion of the required LSL language and host surface.

Recent scripting checkpoints include:

- `8780f83` — run task scripts with Falcon in Regions;
- `42db4a2` — accept Firestorm-terminated script source;
- `05cc9c2` — report Falcon compile errors to Firestorm;
- `962d890` — add locations to Falcon lexer diagnostics;
- `fff1308` — update roadmap for Falcon scripting progress.

Do not assume these remain the tip of `main`; inspect the current log because
the user or other repository tooling may have added later commits.

## Product and architecture decisions

- Region runtime: native C++20 and CMake.
- Central Grid and management services: Go with PostgreSQL.
- Region-local durable state: configuration files, SQLite, and filesystem asset
  blobs. Prefer persistent configuration files over environment-variable
  configuration. Region startup credentials may come from the provisioned
  Region configuration and systemd environment files required for service
  launch, but application settings belong in configuration files.
- Physics: engine-neutral plugin boundary, initially and by default Jolt.
  Preserve the ability to support NVIDIA PhysX 5.x later. Region code expresses
  mass, material, shape, vehicle, and world intent; physics adapters implement
  engine behavior.
- Scripting: the purpose-built, single-threaded, cooperative native C++ Falcon
  VM. Do not introduce Wasmtime, ANTLR, a third-party scripting engine, a native
  thread per script, or locks around script/scene access.
- LSL target: complete Second Life LSL plus Halcyon/InWorldz extensions. Do not
  add OpenSimulator/OSSL extensions. The local Halcyon tree at
  `F:\HalcyonGrid` is an important behavioral reference, especially for
  crossings and permissions, but it is not a runtime dependency.
- Inventory: AIS v3 is the required write model. Read-only legacy viewer access
  is a possible future stretch goal, not an early implementation requirement.
- Permissions: Second Life semantics are the primary target and Halcyon is a
  better implementation reference than OpenSimulator, particularly for nested
  object and contents permissions. Write permission masks in hexadecimal.
- Assets: every asset records creator/uploader provenance distinct from owner.
- Regions: support exactly 1x1 (256 m), 2x2 (512 m), and 4x4 (1024 m) Regions.
  Standard water height is 20 m. PNG is the accepted terrain-image format;
  avoid lossy JPEG heightmaps.
- Terrain and maps: viewer terrain edits are authoritative, update Jolt
  immediately, persist, and update live world-map tiles immediately.
- Currency display is `C$`; texture uploads are free.
- The system inventory root is simply `Library` and follows the familiar Second
  Life organizational model where practical.
- Cloud Regions use direct public TCP/UDP endpoints. An on-host Caddy proxy owns
  public HTTP/HTTPS for Grid discovery and web APIs; do not put Region UDP
  behind a generic reverse proxy or Cloudflare proxy.

Intentional differences and durable decisions belong in `docs/FEATURES.md` and
`docs/adr/`. Scripting design belongs in `docs/SCRIPTING.md` and `docs/VM.md`.

## Development and verification conventions

- Prefer Go, C++, Node, and `.cmd` automation over PowerShell scripts. The user
  has repeatedly encountered PowerShell-specific failures. Using the current
  PowerShell shell for ordinary commands is acceptable; avoid adding new
  PowerShell workflow dependencies where a portable alternative is reasonable.
- Use configuration files for tests and runtime instead of passing large sets
  of environment variables to executables.
- Visual Firestorm observations are acceptance evidence. Record meaningful
  confirmed behavior in `docs/FIRESTORM.md` and update roadmap checkboxes only
  when their complete wording has evidence.
- Use Firestorm source, Second Life viewer/protocol behavior, and Halcyon code
  to resolve protocol ambiguity. Packet capture is appropriate when source and
  logs are insufficient.
- `PSLIST` and `TASKKILL` are on the Windows search path. When a clean viewer
  restart is required for deployment or acceptance, it is acceptable to locate
  Firestorm and use `TASKKILL /IM <image-name>` instead of repeatedly asking the
  user to close it manually.
- Keep changes small and reviewable. Commit and push after each coherent step.
- Use hexadecimal for protocol and permission bitmasks.
- Preserve user and other-agent changes; never use destructive Git cleanup.

Windows verification currently uses the configured build at
`build/windows-vcpkg`:

```text
cmake --build build/windows-vcpkg --config Debug --target <target>
ctest --test-dir build/windows-vcpkg -C Debug --output-on-failure
```

At the last scripting checkpoint, the full Windows suite had 18 passing tests.

## Cloud deployment handoff

Cloud Grid hostname: `grid.homeworldz.com`.

SSH command:

```text
ssh -i C:\Users\paul\.ssh\homeworldz_ovh_unattended_ed25519 ubuntu@grid.homeworldz.com
```

Remote source is `/home/ubuntu/homeworldz`. The existing vcpkg/Jolt tree is
`/home/ubuntu/src/vcpkg`. A production Linux Region build and suite can be run
with:

```text
cd ~/homeworldz
git pull --ff-only
VCPKG_ROOT=$HOME/src/vcpkg HOMEWORLDZ_BUILD_JOBS=2 \
  ./scripts/build-region.sh --build-dir build/linux-falcon --test
```

The deployed Region binary is `/opt/homeworldz/region/homeworldz-region`.
Install atomically, then restart the Region services. All four Regions should
normally be online, especially after a code update. The active cloud services
are:

- `homeworldz-grid.service`
- `homeworldz-api.service`
- `homeworldz-region@welcome.service`
- `homeworldz-region@sandbox.service`
- `homeworldz-region@gamma.service`
- `homeworldz-region@beta.service`

Beta previously fell out of rotation because it had crashed and was already down
when a later restart ran, so a restart-only command silently skipped it. Do not
reproduce that cycle: after installing the shared binary, restart every Region,
and if one is unexpectedly `inactive`, start it and confirm it comes up rather
than leaving it down. Derive the Region list from the live `systemctl` state
instead of a hardcoded subset.

For a Region-only deployment:

```text
sudo install -m 0755 build/linux-falcon/region/homeworldz-region \
  /opt/homeworldz/region/homeworldz-region.new
sudo mv /opt/homeworldz/region/homeworldz-region.new \
  /opt/homeworldz/region/homeworldz-region
sudo systemctl restart homeworldz-region@welcome.service \
  homeworldz-region@sandbox.service homeworldz-region@gamma.service \
  homeworldz-region@beta.service
```

After deployment, confirm `systemctl is-active`, inspect recent journals for
errors and warnings, and only then request the precise Firestorm acceptance
action. A Region restart disconnects Firestorm; tell the user to reconnect when
needed. Firestorm stores the smoke account password, so do not ask the user to
re-enter or repeatedly copy it.

## Definition of a successful continuation

A fresh conversation succeeds when it can use this file to select the next
roadmap step, implement and test it without regressing the accepted viewer
slice, keep documentation honest, commit and push the result, deploy when
appropriate, and present the user with one clear interactive acceptance test
only when automation can go no further.
