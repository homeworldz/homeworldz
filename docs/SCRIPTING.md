# HomeWorldz Scripting

HomeWorldz will implement Linden Scripting Language compatibility with a
purpose-built, single-threaded C++ bytecode interpreter — the **Falcon VM**. The
supported language surface is Second Life LSL plus Halcyon/InWorldz extensions.
OpenSimulator-only extensions, including OSSL functions, are intentionally
excluded. See [VM.md](VM.md) for the Falcon p-code format and VM internals.

Scripting follows avatar synchronization and basic avatar physics in the
implementation plan. Walking, flight, terrain collision, authoritative viewer
updates, attachments, vehicles, teleports, and region crossings establish the
execution and transfer behavior that scripts must participate in. Building a
runtime before those boundaries work would optimize the wrong lifecycle.

## Ownership boundaries

The C++ region owns the authoritative scene, physics, inventory integration,
event production, script scheduler, resource accounting, crossings, and LSL
host functions. A script cannot mutate scene data from another thread or call
operating-system services directly.

The LSL compiler owns tokenization, parsing, type checking, compatibility
diagnostics, and HomeWorldz bytecode generation. It will use a handwritten
scanner, recursive-descent statement parser, and precedence/Pratt expression
parser rather than ANTLR or another parser-generator runtime.

The script VM owns explicit execution state: instruction pointer, LSL state,
globals, operand stack, call frames, locals, current event, queued events,
timers, listens, permissions, sleep state, memory accounting, and serializable
host-operation continuations. Compiled bytecode is an immutable derived asset,
cached by source hash plus compiler and ABI version. Runtime state belongs to a
particular script inventory item in an object or attachment.

The grid locates source and bytecode assets but does not execute scripts or own
runtime state. Regions transfer runtime state directly as part of an avatar,
attachment, vehicle, or object handoff.

## Compatibility target

Compatibility includes syntax, types, conversions, list and string behavior,
states, events, built-in functions, constants, event delays, permissions,
memory reporting, queue behavior, and observable error behavior. The Phlox
grammar, function tables, bytecodes, compiler tests, runtime tests, and Halcyon
region integration under `F:\HalcyonGrid` are behavioral references, not
runtime dependencies.

Halcyon/InWorldz additions will be inventoried explicitly and covered by
compatibility tests. OpenSimulator extensions will not be admitted accidentally
through shared historical code or names. HomeWorldz-specific additions, if any,
must use a separately documented namespace and compatibility decision.

Vehicle compatibility includes the SL/Halcyon `llSetVehicleType` model. As a
stretch goal, a script containing only a call such as
`llSetVehicleType(VEHICLE_TYPE_CAR);` produces a usable vehicle from the named
preset. Subsequent standard vehicle parameter calls refine that preset. The LSL
host interface passes vehicle intent and parameters through the engine-neutral
physics contract; each adapter maps them to Jolt or PhysX native vehicle,
motor, and constraint support. Scripts never select or address a physics engine.

## Execution and scheduling

Scripts run cooperatively on the authoritative region thread in a defined
simulation phase. There is no thread per script and no separate script engine
thread. Region scene access therefore requires no locks or semaphores.

Cooperation is enforced by the VM rather than trusted to script authors. The
scheduler may yield after every completed bytecode instruction. Scripts receive
small, weighted round-robin instruction budgets; an infinite LSL loop consumes
its allocation and resumes at the back of the queue during a later region tick.

Every opcode and synchronous host function must be bounded. Costs for strings,
lists, serialization, and similar work scale with input size rather than
counting as one cheap instruction. Slow external work such as HTTP starts a
nonblocking region operation, stores a serializable request token or
continuation, and yields. Completion becomes an LSL event on the region thread.

Initial resource controls include:

- A total script-time and weighted-instruction budget per simulation frame.
- Fair per-script scheduling plus aggregate owner, object, and parcel budgets.
- A 64 KiB logical memory limit compatible with current LSL expectations.
- A default 64-event queue with event-specific coalescing and overflow rules.
- Bounded call depth, stack size, strings, lists, events, and host payloads.
- Moving usage averages and progressive throttling for chronic offenders.
- A wall-clock guard for VM bugs or unexpectedly expensive native calls.

Poor scripts should normally run more slowly rather than destabilize the
region. Memory, stack, or invalid-bytecode failures stop only the affected
script and produce an LSL-visible runtime error where the protocol permits.

## Serializable execution state

HomeWorldz bytecode never uses the native C++ call stack as script state. A
script can be suspended after any completed bytecode instruction, including in
the middle of an event handler. This is essential for attachments and moving
vehicles: crossing cannot wait for a poorly written handler or infinite loop to
reach an event boundary.

The crossing snapshot contains at least:

- Bytecode asset UUID, bytecode ABI version, and compiler version.
- Instruction pointer and current LSL state.
- Globals, operand stack, call frames, return addresses, and locals.
- Current event, queued events, and detected-event variables.
- Running, waiting, sleeping, disabled, or crossing-wait state.
- Relative sleep and timer durations.
- Active listens, granted permissions, start parameter, and runtime counters.
- Memory accounting and deterministic random-generator state.
- Transferable pending host-operation tokens and continuation state.

The wire format is a compact, versioned HomeWorldz binary format with bounded,
length-prefixed fields and tagged LSL values. It is not JSON, XML, protobuf, or
a reflection dump. Bytecode is cached separately and transferred only when the
destination lacks the matching asset and ABI. Serialization uses reusable,
pre-sized buffers and is benchmarked independently from network transit.

## Attachment, vehicle, and object crossings

A crossing is a transaction over an authoritative entity bundle:

1. Assign a transit UUID and generation and mark the avatar, attachment set,
   vehicle, or object group as entering transit.
2. Complete the current VM instruction, then freeze relevant scripts and
   physics together.
3. Capture object, physics, inventory, and script state under the same transit
   identity.
4. Send missing assets plus the compact runtime snapshot to the destination.
5. Restore the complete bundle at the destination while it remains disabled.
6. After destination acknowledgement, atomically activate destination physics,
   scripts, and event delivery.
7. Relinquish and remove the source copy. On failure, cancel transit and resume
   the still-authoritative source state.

Attachments travel in the avatar bundle. A ridden vehicle and its scripts
travel as an object bundle coordinated with its seated avatars. No script may
resume before its owning entity is authoritative at the destination.

The transit UUID and generation prevent duplicate activation. Events arriving
during the freeze are assigned to a defined source/destination cutoff and are
transferred or forwarded rather than silently discarded. Side-effecting host
operations use stable operation identifiers so retry or rollback cannot apply
the same effect twice. Operations that cannot be transferred safely must reach
a bounded safe point, cancel cleanly, or cause the crossing to roll back.

## Acceptance gates

Before general scripting work begins, HomeWorldz must demonstrate:

- Continuous authoritative avatar position and flight-state updates in the
  viewer.
- A basic avatar capsule that can fly and walk on terrain without sinking or
  hovering incorrectly.
- Stable viewer appearance during movement and relog.
- Attachment lifecycle ownership sufficient to freeze and restore a bundle.
- A repeatable two-region avatar crossing and teleport transaction skeleton.

The script VM then requires compiler compatibility tests, deterministic
scheduler tests, resource-exhaustion tests, snapshot round trips at every opcode
class, and crossing benchmarks for large attachment and vehicle script sets.
