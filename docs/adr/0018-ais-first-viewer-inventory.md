# ADR 0018: AIS-First Viewer Inventory

Status: Accepted

HomeWorldz uses the Second Life Agent Inventory Service (AIS) version 3
protocol as its primary viewer-facing interface for inventory mutation. New
folder, item, move, rename, and delete workflows are designed and tested first
through AIS rather than through legacy viewer UDP messages.

The grid remains authoritative for inventory metadata under ADR 0017. AIS is
an external viewer protocol adapter over that model; it does not become an
internal HomeWorldz service boundary or dictate the PostgreSQL schema.

HomeWorldz may provide `CreateInventoryCategory` and selected legacy inventory
UDP operations for Firestorm, OpenSimulator, Halcyon, or other viewer
compatibility. Those adapters are optional shims and must call the same grid
inventory operations. They do not block delivery of an AIS-only mutation
feature and are not the compatibility baseline for new inventory work.

This choice follows the installed Firestorm 7.2.4 behavior: it prefers AIS,
then the `CreateInventoryCategory` capability for folder creation, and does not
fall back to `CreateInventoryFolder` UDP when neither HTTP interface is
advertised.
