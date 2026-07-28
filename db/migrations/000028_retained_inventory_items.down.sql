-- Drops the retained records of ADR 0026. Reverting forfeits the disaster
-- recovery they exist for: any no-copy item currently rezzed or embedded in
-- a scene loses its reconstitutable metadata, so a later region loss would
-- destroy those items permanently. The bytes stay in the vault either way.
BEGIN;

DROP TABLE retained_inventory_items;

DELETE FROM schema_metadata WHERE version = 28;

COMMIT;
