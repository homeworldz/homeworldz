ALTER TABLE regions DROP COLUMN session_endpoint;

DELETE FROM schema_metadata WHERE version = 25;
