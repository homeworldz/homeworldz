ALTER TABLE regions
    ADD COLUMN provisioned boolean NOT NULL DEFAULT false;

CREATE INDEX regions_expired_transient_idx
    ON regions(lease_expires_at)
    WHERE NOT provisioned;

INSERT INTO schema_metadata (version) VALUES (11);
