ALTER TABLE sessions
    ADD COLUMN viewer_circuit_code bigint UNIQUE CHECK (viewer_circuit_code BETWEEN 1 AND 4294967295),
    ADD COLUMN destination_region_id uuid REFERENCES regions(id) ON DELETE SET NULL;
INSERT INTO schema_metadata (version) VALUES (3);
