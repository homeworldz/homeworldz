ALTER TABLE regions
    ADD COLUMN viewer_port integer NOT NULL DEFAULT 42002 CHECK (viewer_port BETWEEN 1 AND 65535);
INSERT INTO schema_metadata (version) VALUES (4);
