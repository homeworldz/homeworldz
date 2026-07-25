# ADR 0031: Sandboxed Lua Subset and Second P-Code Machine

Status: Accepted

HomeWorldz will offer a **restricted, sandboxed Lua** as a second scripting
language alongside LSL. Lua is compiled to p-code and runs behind the same
script-runtime boundary as LSL
([ADR 0021](0021-script-runtime-boundary.md)), so it inherits the scheduler,
resource accounting, and — critically — the crossing snapshot.

Lua is **not second-class**. A Lua-scripted attachment or vehicle crosses a
region border exactly as an LSL one does, with the same
suspend-after-any-instruction guarantee. Embedding a stock Lua interpreter would
not achieve this: there is no supported way to serialize a mid-execution
`lua_State`, so Lua objects could not cross while running. That, not any
licensing or dependency rule, is why Lua gets its own p-code machine.

**Neither language is a subset of the other.** Features may exist in LSL and not
Lua, or in Lua and not LSL, by design. Lua is not a compatibility surface.

## A second backend, not a superset VM

Lua gets its own p-code machine as a peer of Falcon behind ADR 0021's boundary,
which already states that the authoritative scene "will not depend on a
particular virtual machine."

**Shared** across both backends: the scheduler and instruction-fuel model, the
event dispatch model, the host-function registry, resource accounting, the
snapshot *container*, and the crossing transaction in
[SCRIPTING.md](../SCRIPTING.md).

**Per-language:** opcodes, value model, garbage collection, and call frames.

LSL therefore keeps Falcon's cheap statically-typed machine and pays nothing for
Lua's dynamism. Forcing LSL through a dynamically-typed superset would tax the
compatibility-critical path to serve the optional language.

## Design

**Heap references are integer handles, not pointers.** This is the load-bearing
decision. Tables, strings, closures, upvalues, and coroutines live in VM-owned
handle tables, so a snapshot is a near-linear dump of those tables with no
pointer relocation on restore. It extends Falcon's existing property — all state
as data — to a cyclic object graph. Raw pointers would make every crossing a
graph walk with address fixups.

**Register-based instructions.** Falcon's `{Op, a, b}` triple widens to
`{Op, a, b, c}`, matching Lua's A/B/C shape. Per-frame register windows cut
instruction counts substantially versus a stack machine.

**Explicit state, no native re-entrancy.** Call frames are a frame vector (base
register, return instruction pointer, closure handle, varargs) — never native
C++ frames. `pcall` is a frame carrying an error-handler marker, unwound by
walking that vector: **no `setjmp`/`longjmp`, and no C++ exceptions for
Lua-level errors**. C++ exceptions may still *stop* a script, exactly as Falcon
does today. Library functions that would otherwise call back into script
(`table.sort` comparators, iterators) are **implemented in Lua and compiled to
p-code**, so no native frame ever sits between two script frames.

**Garbage collection** is incremental mark-sweep over the handle table with a
tri-color invariant whose worklist is itself a handle vector. GC state is
therefore data and serializes with everything else, and collection pauses stay
inside the fuel budget rather than blocking the region thread.

**Numbers** are a single `f64`, following Luau, rather than Lua 5.3+'s
integer/float subtypes — this avoids an overload decision in every arithmetic
opcode.

**Optional type annotations**, Luau-style, let the compiler emit specialized
fast-path opcodes. This is the same trick that makes Falcon's LSL `AddInt` cheap,
applied to gradually-typed Lua.

**No JIT, ever.** JIT-compiled native code cannot be snapshotted mid-execution
and is a hostile-content attack surface. Roughly 2–5× stock-Lua interpretation
cost is accepted as the price of the crossing invariant.

## Excluded dynamism

The subset is defined by what it removes. Each exclusion has a specific reason:

| Excluded | Reason |
| --- | --- |
| **User metatables and all metamethods** | The decisive simplification — see below. |
| `load`, `loadstring`, `dofile`, `require` of runtime source | Keeps p-code an immutable asset cached by source hash, per ADR 0021. |
| `setfenv` / `getfenv` | Defeat static global resolution and sandboxing; Luau removed these too. |
| The `io`, `os`, `package`, and `debug` libraries | ADR 0021 already forbids ambient operating-system capability. |
| Weak tables (`__mode`) and `__gc` finalizers | Both make garbage collection re-enter script code. |
| Implicit string↔number arithmetic coercion | Require an explicit `tonumber`. |

**Metatables are excluded in v1** because they are the one construct that forces
a single instruction to call a script function and thereby become *partially*
complete: an `ADD` whose operand has an `__add` handler must invoke script
mid-instruction. Stock Lua recurses natively there, which a snapshot-safe VM
cannot do. Removing them removes the sharpest risk in the design.

Tables plus functions still express struct and module patterns; what is lost is
inheritance sugar and operator overloading. If this is ever revisited, the
reserved path is push-a-frame dispatch with idempotent instruction
re-execution — to be prototyped before being committed to.

## Retained

Closures and upvalues, varargs and multiple return values, and **coroutines**.
Coroutines are *easier* in an explicit-state VM than in stock Lua: each is
simply another frame stack plus register file, and all contexts snapshot
together. In stock Lua they are among the hardest things to serialize.

## Language selection

A script in a prim's Contents is still LSL-text as far as a legacy viewer is
concerned — Firestorm's editor, its Save/compile path, and the existing
Falcon compile-error reporting all assume it. Language is therefore selected by
a **first-line pragma** (`--!lua`) plus a `source_language` field in the
compiled `Program`, requiring no new asset type and no viewer change.

Adding `source_language` to the `Program` container is cheapest **before**
bytecode assets are cached in production, since adding it later means an ABI bump
and a cache invalidation.

## Snapshots and determinism

Deterministic RNG state travels in the snapshot, per SCRIPTING.md. Variance in
`libm` transcendentals across hosts is acceptable: exactly one copy of a script
is authoritative after a transfer, so post-crossing execution need not be
bit-identical to a counterfactual run on the source region.

## Naming

Falcon is the LSL VM. The Lua machine uses neutral module identifiers until it
is named, matching the precedent in [VM.md](../VM.md) — "the current PoC code
still uses neutral module identifiers, to be aligned as it graduates."

## Relationship to other ADRs

- **ADR 0021** — this sits behind the same script-runtime boundary as a second
  backend; the boundary's VM-neutrality is what makes it possible.
- **ADR 0030** — the client's Blockly-style visual scripting emits Lua or
  p-code, so non-coder authoring reuses this runtime.

## References

- [SCRIPTING.md](../SCRIPTING.md) — scheduling, resource, and crossing
  requirements both backends share
- [VM.md](../VM.md) — Falcon p-code format and VM internals
- [Luau](https://luau.org/) — the typed, sandboxed Lua dialect this subset
  follows on numbers, annotations, and the `setfenv`/`getfenv` removal
