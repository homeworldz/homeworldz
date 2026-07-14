# ADR 0018: AIS-First Viewer Inventory

Status: Accepted

HomeWorldz uses the Second Life Agent Inventory Service (AIS) version 3
protocol as its primary viewer-facing interface for inventory mutation. New
folder, item, move, rename, and delete workflows are designed and tested first
through AIS rather than through legacy viewer UDP messages.

The grid remains authoritative for inventory metadata under ADR 0017. AIS is
an external viewer protocol adapter over that model; it does not become an
internal HomeWorldz service boundary or dictate the PostgreSQL schema.

AIS v3 is required for viewers that use HomeWorldz inventory mutation. A
mutation feature is complete when its AIS path meets acceptance criteria; it
does not require a parallel legacy implementation. HomeWorldz may still
provide `CreateInventoryCategory` and selected legacy inventory UDP operations
for Firestorm, OpenSimulator, Halcyon, or other viewer compatibility. Those
adapters are optional shims and must call the same grid inventory operations.

Read-only legacy inventory access is a stretch compatibility goal so older
viewers may be able to log in and browse inventory. Existing descendants and
item-fetch capabilities can be retained and improved on a best-effort basis,
but comprehensive legacy viewer support is not a committed requirement and
must pass a cost/benefit review before expanding the compatibility surface.

This choice follows the installed Firestorm 7.2.4 behavior: it prefers AIS,
then the `CreateInventoryCategory` capability for folder creation, and does not
fall back to `CreateInventoryFolder` UDP when neither HTTP interface is
advertised.
