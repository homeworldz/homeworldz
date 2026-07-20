DROP INDEX IF EXISTS users_display_name_key_key;
ALTER TABLE users DROP COLUMN IF EXISTS display_name_key;

DELETE FROM schema_metadata WHERE version = 19;
