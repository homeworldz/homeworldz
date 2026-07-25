# ADR 0031: Sandboxed Lua Scripting and SLua Compatibility

Status: Accepted

This ADR records **current expectation and intent**, not a commitment. It
describes the direction HomeWorldz presently expects to take for Lua scripting
and the evidence behind it; the verification gates near the end are the specific
findings that would change it.

HomeWorldz expects to support **Lua** as a second scripting language alongside
LSL, and to do so by adopting **SLua** — Linden Lab's MIT-licensed fork of
Luau — as the Lua backend behind the script-runtime boundary in
[ADR 0021](0021-script-runtime-boundary.md), targeting SLua source and API
compatibility rather than defining a HomeWorldz-specific Lua dialect.

## Why adopt rather than build

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
[ADR 0016](0016-firestorm-compatibility-target.md), HomeWorldz reimplements
Second Life scripting semantics for **content and creator portability**, not for
its own sake. Now that Second Life has an official Lua, "HomeWorldz Lua" should
mean "SLua-compatible Lua" for exactly the same reason — otherwise Lua content,
examples, and the emerging transpiler tooling do not transfer, which is the
fragmentation ADR 0016 exists to prevent. Unlike LSL, where reimplementation was
forced because the Second Life server is closed, here the actual implementation
is available under a compatible license.

## Compatibility target

The expectation is to follow SLua's surface rather than invent one:

- Linden functions under the **`ll` namespace in PascalCase** — `ll.Say`,
  `ll.GetPos` — not LSL's `llSay`.
- Events registered with **`LLEvents:on("touch_start", function(...) end)`**
  rather than LSL event blocks.
- Timers via **`LLTimers`**; JSON via **`lljson`**; bitwise via **`bit32`**.
- Luau **gradual typing**, with type annotations available and used in SL's own
  examples.
- A **128 KB** logical memory limit, above LSL's 64 KB.

HomeWorldz-specific additions, if any, must use a separately documented
namespace and their own compatibility decision, consistent with the rule
SCRIPTING.md already applies to LSL.

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

## What HomeWorldz still owns

Adopting SLua does not outsource the region's responsibilities. HomeWorldz still
implements the **`ll` host surface against its own authoritative scene**,
integrates the VM with the region scheduler under bounded instruction slices,
owns the crossing transaction and snapshot container, enforces aggregate
resource budgets, and provides serializable continuations for asynchronous host
operations. A script still reaches nothing outside the host boundary.

## Reversal: metatables

The earlier revision excluded user metatables outright, because mid-instruction
metamethod dispatch is the sharpest hazard for a snapshot-safe interpreter.
**That exclusion is expected to be dropped.** Luau supports metatables,
metatable-based OOP is idiomatic in SLua content, and Ares is designed to
serialize the resulting object graphs. Excluding them would break a large class
of otherwise-portable scripts for a hazard the upstream implementation already
addresses.

## Verification gates

These are unresolved and would change the direction above. None should be
assumed settled.

- **Serialization granularity.** SLua documents that a *yielded* thread can be
  serialized. SCRIPTING.md requires suspension after any completed instruction,
  including mid-handler. Lua's inability to yield across a C-call boundary may
  mean a script inside a host function cannot be suspended. This is the decisive
  gate.
- **CodeGen interaction.** Luau ships a native code generator. Native frames
  cannot be snapshotted, so establish whether serialization requires CodeGen
  disabled, and what that costs.
- **Aggregate budgets.** SLua's memory limits are per-script; SCRIPTING.md also
  requires owner, object, and parcel aggregates plus wall-clock guards.
- **Host-operation continuations.** HomeWorldz's asynchronous host operations
  need transferable tokens; confirm these survive an Ares round trip.
- **Maturity.** As of 2026-07 SLua is in **open beta**, limited to Second Life
  sandbox regions and the beta grid, and compiling requires Linden Lab's Project
  Lua Editor viewer — Firestorm does not yet compile SLua. The API may still
  move. MIT licensing means a version can be pinned or forked if it does.

## Fallback

If the gates above cannot be met, the fallback remains the previous plan: a
purpose-built second p-code machine with a restricted subset — register-based
instructions widening Falcon's `{Op, a, b}` triple, heap references as integer
handles rather than pointers so snapshots need no relocation, explicit call
frames with `pcall` as an error-handler frame, incremental mark-sweep GC whose
worklist is itself serializable, and metatables excluded to avoid
mid-instruction dispatch. That design remains sound; it is simply more expensive
than adopting an implementation that already exists.

## Language selection

Second Life selects a script's language with a **compiler selector** in the
script editor, implying a protocol-level mechanism that HomeWorldz would prefer
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
