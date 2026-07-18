ALTER TABLE provisioned_regions
    ADD COLUMN public_endpoint text NOT NULL DEFAULT '',
    ADD COLUMN viewer_port integer NOT NULL DEFAULT 0
        CHECK (viewer_port >= 0 AND viewer_port <= 65535);

INSERT INTO schema_metadata (version) VALUES (16);
