ALTER TABLE users ADD COLUMN viewer_password_hash text;
ALTER TABLE sessions ADD COLUMN secure_session_id uuid;
INSERT INTO schema_metadata (version) VALUES (2);
