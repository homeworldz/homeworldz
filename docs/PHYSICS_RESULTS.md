# Physics Evaluation Results

The native `homeworldz-physics-lab` runs the same backend-neutral scenarios
against Jolt 5.5.0 and PhysX 5.5.0. These first-gate measurements are intended
to choose the v1 implementation direction; they are not production capacity
figures.

## Method

- Date: 2026-07-13
- Build: Release, Visual Studio 2026, x64
- Host: Intel Core i9-11900F, 31.7 GiB RAM, Windows build 26200
- Fixed simulation step: 1/60 second
- Each backend was launched in its own process so peak resident memory was not
  inherited from the other engine.
- Command: `build/windows-vcpkg/region/Release/homeworldz-physics-lab.exe
  <jolt|physx>`

Tick measurements cover `World::step` only. CPU time covers the entire common
scenario suite, including world construction and teardown. Peak RSS is the
process high-water mark. The small scenarios are correctness probes, so their
sub-millisecond timings are especially sensitive to host scheduling.

## Scenario Results

All eight scenarios passed on both engines.

| Scenario | Bodies | Steps | Jolt avg ms | Jolt worst ms | PhysX avg ms | PhysX worst ms |
|---|---:|---:|---:|---:|---:|---:|
| Avatar controller | 2 | 30 | 0.062 | 0.143 | 0.153 | 0.442 |
| Terrain collision | 2 | 120 | 0.039 | 0.111 | 0.172 | 0.771 |
| Object stacking | 6 | 180 | 0.025 | 0.099 | 0.194 | 0.616 |
| Scripted impulse | 2 | 30 | 0.048 | 0.074 | 0.176 | 0.259 |
| Vehicle-style motion | 2 | 60 | 0.052 | 0.085 | 0.196 | 0.488 |
| Region handoff | 1 | 15 | 0.047 | 0.057 | 0.174 | 0.225 |
| State restore | 1 | 20 | 0.045 | 0.061 | 0.161 | 0.241 |
| Region load | 257 | 30 | 0.080 | 0.150 | 0.312 | 0.488 |

## Summary

| Measurement | Jolt | PhysX |
|---|---:|---:|
| Suite CPU time | 30 ms | 101 ms |
| Peak resident memory | 10,244 KiB | 8,144 KiB |
| Repeated-run position drift | 0.000000 | 0.000000 |
| Transfer state per body | 96 bytes | 96 bytes |
| Adapter implementation | 229 lines | 203 lines |

The region handoff and restore probes preserve position and velocity within
0.01 world units. The deterministic replay probe produced identical final
positions in two runs for both engines. Neither result claims cross-platform
bitwise determinism.

The current avatar and vehicle scenarios exercise the shared adapter shape—a
kinematic capsule and impulse-driven chassis—not a finished player controller
or wheel model. Mesh terrain, compound constraints, collision event quality,
and long-duration soak behavior remain engineering work after the backend gate.
