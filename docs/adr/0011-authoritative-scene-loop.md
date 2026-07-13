# ADR 0011: Authoritative Scene And Fixed-Step Loop

Status: Accepted

The region owns entity identity, transforms, velocities, and revision tracking
independently of any physics backend. Simulation advances through a fixed-step
loop that defaults to 45 Hz.

Elapsed wall time is accumulated for deterministic steps. Catch-up is bounded
to eight steps per advance so a delayed host cannot enter an unbounded spiral;
excess elapsed time is discarded while the sub-step remainder is retained for
interpolation. Physics adapters will mirror this authoritative state rather than
becoming its owner.
