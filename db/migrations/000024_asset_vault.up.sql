-- The asset vault (ADR 0026): a grid-side, replica-only durable store for the
-- bytes behind every inventory-referenced asset. This table indexes what the
-- vault holds; the bytes themselves live on the vault filesystem.
--
-- Blobs are keyed by the same lowercase SHA-256 the region blob stores already
-- use (ADR 0014), so an existing registration needs no new identifier to be
-- ingested. Under ADR 0027 blob identity becomes a grid-assigned blob_id and the
-- digest is retained as the integrity checksum; that change re-keys this table
-- rather than reshaping it.
--
-- The vault never originates assets and is never in the viewer data path, so
-- there is deliberately no creator, owner, or asset UUID here: those belong to
-- the asset layer in asset_metadata. Many assets may reference one blob.
CREATE TABLE vault_blobs (
    sha256 char(64) PRIMARY KEY CHECK (sha256 ~ '^[0-9a-f]{64}$'),
    size bigint NOT NULL CHECK (size > 0),
    ingested_at timestamptz NOT NULL DEFAULT now()
);

INSERT INTO schema_metadata (version) VALUES (24);
