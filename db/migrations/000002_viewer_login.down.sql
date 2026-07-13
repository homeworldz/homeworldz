ALTER TABLE sessions DROP COLUMN secure_session_id;
ALTER TABLE users DROP COLUMN viewer_password_hash;
DELETE FROM schema_metadata WHERE version = 2;
