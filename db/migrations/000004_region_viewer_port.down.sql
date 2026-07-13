ALTER TABLE regions DROP COLUMN viewer_port;
DELETE FROM schema_metadata WHERE version = 4;
