# ADR 0021: Script Runtime Boundary

Status: Proposed

HomeWorldz will expose an LSL-compatible source language and event model to
viewer-created content, but the authoritative scene and persistence formats
will not depend on a particular virtual machine. LSL compilation produces an
opaque module consumed through the C++ script engine boundary. The boundary
owns identities, event queues, deterministic execution fuel, logical memory
limits, host commands, diagnostics, and explicit persistent snapshots.

Scripts execute only when the region scheduler grants a slice. Host functions
enqueue validated commands for the authoritative scene loop rather than
mutating scene state from runtime threads. Network, filesystem, clocks, random
sources, and other ambient operating-system capabilities are unavailable
unless HomeWorldz supplies a narrow host function. Persistence and region
handoff occur at event boundaries using versioned engine snapshots; native VM
stacks are not scene data.

The recommended initial backend is Wasmtime running Core WebAssembly modules
without WASI. Its maintained C/C++ embedding API, tier-one Windows x64 support,
deterministic fuel, epoch interruption, and explicit linear-memory controls fit
the region's safety and packaging requirements. Fuel is the reproducible quota;
epoch interruption is only an emergency wall-clock guard. The LSL compiler and
HomeWorldz host ABI remain ours, so a different WebAssembly engine or a custom
bytecode interpreter can replace Wasmtime without changing scene objects.

The remaining decision is whether to accept Wasmtime's larger packaged runtime
and JIT/AOT complexity now, or begin with a smaller custom interpreter and pay
the implementation and security cost of maintaining a VM. No backend dependency
will be added until this ADR is accepted.

## References

- [Wasmtime C/C++ embedding API](https://docs.wasmtime.dev/c-api/)
- [Wasmtime platform support](https://docs.wasmtime.dev/stability-platform-support.html)
- [Wasmtime deterministic fuel and epoch interruption](https://docs.wasmtime.dev/examples-interrupting-wasm.html)
- [Second Life LSL states and events](https://wiki.secondlife.com/wiki/State)
- [Second Life LSL memory limits](https://create.secondlife.com/script/lsl-reference/functions/llsetmemorylimit/)
