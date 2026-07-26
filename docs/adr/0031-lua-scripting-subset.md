# ADR 0031: Sandboxed Lua Scripting and SLua Compatibility

Status: Accepted

This ADR records intent for work that has not started. Two things in it carry
different weight, and the distinction matters:

- **SLua compatibility is the baseline.** Homeworldz targets everything SLua
  supports and does not define a Homeworldz-specific Lua dialect. This is a
  decision, not an expectation.
- **Adopting the SLua implementation is the expected means.** It is the obvious
  way to deliver that baseline and to leverage problems Linden Lab has already
  solved, but it is subject to the verification gates below.

If a gate fails, the baseline does not move — only the means of delivering it
does. That is the relationship Falcon already has with LSL: the compatibility
target is fixed, the implementation is ours to choose.

Homeworldz will support **Lua** as a second scripting language alongside LSL,
behind the script-runtime boundary in
[ADR 0021](0021-script-runtime-boundary.md), with **SLua** — Linden Lab's
MIT-licensed fork of Luau — as both the compatibility target and the expected
implementation.

## Why reuse rather than build

An earlier revision of this ADR called for a purpose-built second p-code machine
with a deliberately restricted Lua subset. That reasoning rested on one claim:
that no supported way exists to serialize a mid-execution Lua state, so an
off-the-shelf Lua could not participate in region crossings, and Lua scripts
would therefore be second-class.

**That claim no longer holds.** Second Life's SLua is a "friendly fork of Luau"
under the MIT License that uses "a modified Eris called *Ares* to serialize agent
execution state," with the stated goal of "stateful, semi-autonomous objects that
can seamlessly roam across server instances" — the same crossing problem Falcon
was designed around. Its published capabilities cover the four things that made
a bespoke VM look necessary:

| Requirement | SLua status as published |
| --- | --- |
| Serialize running script state | Ares; "a yielded thread can be serialized, along with its global environment" |
| Preemptive scheduling | "Hooks for pre-emptive scheduling are implemented" |
| Per-script memory limits | "implemented through a custom heap traversal function" |
| Sandboxing | `luaL_sandbox` / `luaL_sandboxthread` protect builtin libraries |

A second argument now applies that did not before. Under
[ADR 0016](0016-firestorm-compatibility-target.md), Homeworldz reimplements
Second Life scripting semantics for **content and creator portability**, not for
its own sake. Now that Second Life has an official Lua, "Homeworldz Lua" should
mean "SLua-compatible Lua" for exactly the same reason — otherwise Lua content,
examples, and the emerging transpiler tooling do not transfer, which is the
fragmentation ADR 0016 exists to prevent. Unlike LSL, where reimplementation was
forced because the Second Life server is closed, here the actual implementation
is available under a compatible license.

## Compatibility target

Homeworldz follows SLua's surface rather than inventing one, and supports
**everything SLua supports**. The enumerated items below are the shape of that
surface, not its limit:

- Linden functions under the **`ll` namespace in PascalCase** — `ll.Say`,
  `ll.GetPos` — not LSL's `llSay`.
- Events registered with **`LLEvents:on("touch_start", function(...) end)`**
  rather than LSL event blocks.
- Timers via **`LLTimers`**; JSON via **`lljson`**; bitwise via **`bit32`**.
- Luau **gradual typing**, with type annotations available and used in SL's own
  examples.
- A **128 KB** logical memory limit, above LSL's 64 KB.

**No subtractions.** Homeworldz does not restrict the language surface below
SLua's; a feature SLua supports is a feature Homeworldz is expected to support,
including ones that are awkward to implement. Homeworldz-specific *additions*, if
any, must use a separately documented namespace and their own compatibility
decision, consistent with the rule SCRIPTING.md already applies to LSL.

Because SLua is still evolving, this baseline is a moving target and tracking it
is part of the commitment rather than a one-time port.

## Relationship to Falcon

Falcon remains the LSL VM. SLua is expected to sit beside it as a **second
backend behind ADR 0021's boundary**, which already states that the authoritative
scene "will not depend on a particular virtual machine."

**Shared** across both backends: the scheduler and instruction-fuel model, the
host-function registry, resource accounting, and the crossing transaction in
[SCRIPTING.md](../SCRIPTING.md). **Per-language:** the VM, value model, garbage
collection, and compiler.

**Neither language is a subset of the other.** Features may exist in LSL and not
Lua, or in Lua and not LSL. LSL remains the compatibility surface for existing
content; Lua is a parallel one.

## What Homeworldz still owns

Adopting SLua does not outsource the region's responsibilities. Homeworldz still
implements the **`ll` host surface against its own authoritative scene**,
integrates the VM with the region scheduler under bounded instruction slices,
owns the crossing transaction and snapshot container, enforces aggregate
resource budgets, and provides serializable continuations for asynchronous host
operations. A script still reaches nothing outside the host boundary.

**Perms-table discipline is a hard requirement, not an implementation detail.**
Ares can persist a C function only when it is registered in the permanents table;
light C functions cannot be persisted at all
(`ERIS_ERR_CFUNC`, `ERIS_ERR_CFUNC_UPVALS`). Every `ll` host function must
therefore be a C closure in the perms table, and every suspendable one must yield
through a continuation registered there too. A host function added without this
produces scripts that cannot cross a region border — a failure that surfaces at
the border, not at the call.

## Modules and bytecode caching

SLua provides `require` and a module system, so under the no-subtractions rule
Homeworldz supports it. This has a consequence for ADR 0021, which describes
bytecode as an immutable derived asset "cached by source hash plus compiler and
ABI version": once a script can depend on modules, its compiled form depends on
them too, so the cache key must cover the resolved dependency closure and
invalidation must follow module edits. Module resolution also needs a defined,
sandbox-safe source — inventory contents rather than a filesystem.

This is unresolved and warrants its own decision.

## Reversal: metatables

The earlier revision excluded user metatables outright, because mid-instruction
metamethod dispatch is the sharpest hazard for a snapshot-safe interpreter.
**That exclusion is dropped.** Luau supports metatables, metatable-based OOP is
idiomatic in SLua content, and Ares is designed to serialize the resulting object
graphs. Excluding them would break a large class of otherwise-portable scripts
for a hazard the upstream implementation already addresses — and it would violate
the no-subtractions rule above.

## Verification gates

These decide **how** SLua compatibility is delivered, not whether. Assessed
2026-07-25 by reading the SLua sources and RFCs. **Nothing here has been built
and run**, so each finding is evidence of feasibility, not proof of it.

- **Serialization inside a host call — passes.** This was the decisive gate.
  `VM/src/ares.cpp` serializes C call frames explicitly (`ERIS_CI_KIND_C`,
  selected by the `clvalue(thread->ci->func)->isC` branch) and validates a saved
  continuation (`ERIS_ERR_THREADCTX`, "bad C continuation function"). A host
  function that yielded mid-call therefore restores. The limits are that a thread
  must be yielded rather than running (`ERIS_ERR_THREAD`), that a script must not
  be yielded from inside a hook (`ERIS_ERR_HOOK`), and the perms-table rule
  above.
- **CodeGen — passes.** "Luau's JIT can be used mostly as-is, and serializing
  state while inside a JITed function is fully supported." Native code generation
  need not be disabled, and the position taken in an earlier revision of this ADR
  that a JIT could never be permitted is **withdrawn**.
- **Preemption — passes.** The scheduler injects a valueless yield from an
  interrupt handler, while a `state` switch yields an integer, so the two are
  distinguished by the number of yielded values
  (`rfcs/lsl_state_handling.md`). One constraint: a script's "constructor" runs
  synchronously and must not yield.
- **Aggregate budgets — partially resolved.** `rfcs/memory-limitations.md`
  (Implemented) provides hard per-script *logical* memory limits through memcat
  tagging plus heap traversal from the script root, holding the contract that a
  script's reported memory must not depend on which other scripts are resident.
  Owner, object, and parcel aggregates plus wall-clock guards remain Homeworldz
  work layered on top; nothing blocks them.
- **Maturity — open.** As of 2026-07 SLua is in **open beta**, limited to Second
  Life sandbox regions and the beta grid, and compiling requires Linden Lab's
  Project Lua Editor viewer — Firestorm does not yet compile SLua. The API may
  still move. MIT licensing means a version can be pinned or forked if it does.

What remains is empirical: build SLua, embed it behind the ADR 0021 boundary, and
confirm a round trip of a thread yielded inside a Homeworldz host call.

## Architecture worth adopting

SLua's own state model (`rfcs/lsl_state_handling.md`) solves problems Homeworldz
would otherwise solve independently, and is worth following rather than
paralleling:

- A **Grandparent → Base Image → Forker → Child** thread layout, where each
  script's Child thread owns its globals and each event handler runs in a thread
  pushed onto it, so a handler can be cancelled without tearing down the script.
- Serialization **diffs a Child thread against the base image**, so only the
  delta travels and function prototypes are never duplicated in a snapshot. This
  is a materially better crossing payload than serializing each script whole.
- Host-side data such as the current and next LSL state is serialized
  **separately, outside the VM**, so it neither counts against script memory nor
  requires reaching into VM internals. That split is the same one Homeworldz
  draws between the snapshot container and the runtime state inside it.

## Fallback

If empirical work overturns the findings above, the fallback is to implement
**SLua-compatible semantics on a purpose-built runtime** — not to retreat to a
smaller language.
The compatibility baseline holds either way, so such a runtime must still provide
metatables, coroutines, and the rest of the SLua surface.

The design sketched in an earlier revision of this ADR remains the starting point
for that vehicle: register-based instructions widening Falcon's `{Op, a, b}`
triple, heap references as integer handles rather than pointers so snapshots need
no relocation, explicit call frames with `pcall` as an error-handler frame, and
incremental mark-sweep GC whose worklist is itself serializable. Metatables would
have to be implemented rather than excluded, which means solving mid-instruction
metamethod dispatch — push-a-frame with idempotent instruction re-execution.

That is strictly harder than the restricted subset originally contemplated, and
considerably harder than adopting an implementation that already exists. It is a
fallback, and its cost is itself an argument for making the gates work.

## Language selection

Second Life selects a script's language with a **compiler selector** in the
script editor, implying a protocol-level mechanism that Homeworldz would prefer
to adopt over an invention of its own. Because Firestorm cannot yet compile
SLua, a first-line pragma (`--!lua`) is expected to serve as the interim
mechanism, requiring no new asset type and no viewer change.

Either way, a `source_language` field in the compiled `Program` is worth adding
**before** bytecode caching hardens in production: cheap now, an ABI bump and
cache invalidation later.

## Relationship to other ADRs

- **ADR 0021** — this sits behind the same script-runtime boundary as a second
  backend; the boundary's VM-neutrality is what permits adopting an external VM
  at all.
- **ADR 0016** — SLua compatibility follows the same content-portability logic
  as Second Life protocol compatibility.
- **ADR 0030** — the client's Blockly-style visual scripting is expected to emit
  Lua, so non-coder authoring reuses this runtime.

## References

- [SLua implementation](https://github.com/secondlife/slua/) — the Luau fork,
  Ares serialization, sandboxing, and scheduling hooks
- [Luau](https://luau.org/) — the upstream typed, sandboxed Lua dialect
- [Second Life Lua documentation](https://wiki.secondlife.com/wiki/Lua_Alpha) —
  namespace, events, timers, and memory limit
- [SLua open beta announcement](https://community.secondlife.com/news/featured-news/announcing-the-slua-open-beta-modern-scripting-comes-to-second-life-r11237/)
- [SCRIPTING.md](../SCRIPTING.md) — scheduling, resource, and crossing
  requirements both backends share
- [VM.md](../VM.md) — Falcon p-code format and VM internals
