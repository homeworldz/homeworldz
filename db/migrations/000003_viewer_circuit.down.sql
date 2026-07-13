ALTER TABLE sessions DROP COLUMN destination_region_id;
ALTER TABLE sessions DROP COLUMN viewer_circuit_code;
DELETE FROM schema_metadata WHERE version = 3;
