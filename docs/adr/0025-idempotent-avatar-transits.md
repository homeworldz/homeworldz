# ADR 0025: Idempotent Avatar Transit Transactions

Status: Accepted

HomeWorldz transfers an avatar between Regions through a Grid-coordinated,
durable transaction. A transit has a caller-generated UUID, a monotonically
increasing per-avatar generation, the viewer session UUID, source and
destination Region UUIDs, arrival position/look direction/flying state, an
expiry, and one of four states: `prepared`, `accepted`, `activated`, or
`rolled_back`.

The source prepares a transit before changing local authority. The destination
then validates capacity and creates a non-authoritative provisional presence
before accepting it. Only after the viewer establishes its destination circuit
may the destination activate the transit and become authoritative. The source
retains authority until activation succeeds, then removes its presence. Either
Region may roll back a prepared or accepted transit. Expired active transits
are treated as rolled back.

Only one prepared or accepted transit may exist for an avatar. Generations
increase under a database transaction and distinguish a current transfer from
late messages belonging to an older attempt. Repeating an operation with the
same transit UUID and identical immutable data returns the existing result;
reusing that UUID with different data is a conflict. State operations are also
idempotent, while skipped or reversed transitions fail closed.

The Grid persists coordination state but does not simulate the avatar. Region
servers remain responsible for serializing appearance, attachments, controls,
physics, and later script state. Transit endpoints use internal service
authentication initially and must additionally verify that the viewer session,
source Region, destination Region, and calling Region agree with the stored
transaction before changing state.

This transaction is shared by explicit teleports and border crossings. Object
and vehicle transfers will use the same generation and idempotency principles,
but their larger transferable bundles may use a separate record type.
