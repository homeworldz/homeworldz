# ADR 0021: Script Runtime Boundary

Status: Accepted

HomeWorldz will expose an LSL-compatible source language and event model to
viewer-created content, but the authoritative scene and persistence formats
will not depend on a particular virtual machine. LSL compilation produces an
opaque module consumed through the C++ script engine boundary. The boundary
owns identities, event queues, deterministic execution fuel, logical memory
limits, host commands, diagnostics, and explicit persistent snapshots.

Scripts execute cooperatively on the authoritative region thread only when the
region scheduler grants a bounded instruction slice. Host functions perform
bounded work or begin nonblocking region operations; scripts cannot access
network, filesystem, clocks, random sources, or other ambient operating-system
capabilities directly.

The initial and default backend is a purpose-built C++ bytecode interpreter.
It uses explicit VM stacks and a handwritten LSL compiler without ANTLR or
another parser-generator dependency. The language target is Second Life LSL
plus Halcyon/InWorldz extensions; OpenSimulator-only extensions are excluded.

Runtime state is serializable after every completed bytecode instruction, not
only at event boundaries. Compact versioned snapshots transfer with attachments,
vehicles, and objects so destination regions can restore execution without a
native stack or third-party VM representation. See [SCRIPTING.md](../SCRIPTING.md)
for scheduling, resource, compatibility, and crossing requirements.

## References

- [Second Life LSL states and events](https://wiki.secondlife.com/wiki/State)
- [Second Life LSL memory limits](https://create.secondlife.com/script/lsl-reference/functions/llsetmemorylimit/)
