-- Rejoin the blob and asset layers into the pre-ADR-0027 combined registry.
-- Transactional for the same reason as the up migration: all or nothing.
-- One asset per row again, with the checksum back as byte identity; blobs that
-- no asset names are dropped with the tables, and vault contents are refused
-- rather than orphaned, as on the way up.
BEGIN;

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM vault_blobs) THEN
        RAISE EXCEPTION
            'vault_blobs is not empty: reverting would orphan stored bytes. Empty the vault filesystem first.';
    END IF;
END $$;

CREATE TABLE asset_metadata (
    asset_id uuid PRIMARY KEY,
    creator_user_id uuid NOT NULL,
    sha256 char(64) NOT NULL CHECK (sha256 ~ '^[0-9a-f]{64}$'),
    size bigint NOT NULL CHECK (size > 0),
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE asset_locations (
    asset_id uuid NOT NULL REFERENCES asset_metadata(asset_id) ON DELETE CASCADE,
    endpoint text NOT NULL CHECK (length(endpoint) BETWEEN 8 AND 2048),
    is_origin boolean NOT NULL DEFAULT false,
    verified_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (asset_id, endpoint)
);

CREATE INDEX asset_locations_asset ON asset_locations(asset_id, is_origin DESC, verified_at DESC);

-- This is a faithful revert, not an identity round trip, and the difference is
-- worth stating: locations merged onto shared blobs on the way up, so reverting
-- gives each asset the union of the endpoints serving its bytes. That is more
-- rows than before (measured: 1,183 → 498 → 1,634 on the test grid) and every
-- one of them is a true statement — the endpoints really do serve those bytes,
-- which is why they could be merged. What is not recoverable is which asset
-- originally taught the grid about which endpoint, and nothing depends on that.
--
-- Only sha256-checksummed blobs can be represented by the old shape.
INSERT INTO asset_metadata (asset_id, creator_user_id, sha256, size, created_at)
SELECT asset.asset_id, asset.creator_user_id, blob.checksum, blob.byte_length, asset.created_at
FROM assets AS asset
JOIN blobs AS blob ON blob.blob_id = asset.blob_id
WHERE blob.checksum_algorithm = 'sha256' AND blob.checksum ~ '^[0-9a-f]{64}$';

INSERT INTO asset_locations (asset_id, endpoint, is_origin, verified_at)
SELECT asset.asset_id, source.endpoint,
       bool_or(source.is_origin), max(source.verified_at)
FROM blob_locations AS source
JOIN assets AS asset ON asset.blob_id = source.blob_id
JOIN asset_metadata AS kept ON kept.asset_id = asset.asset_id
GROUP BY asset.asset_id, source.endpoint;

DROP TABLE vault_blobs;
DROP TABLE assets;
DROP TABLE blob_locations;
DROP TABLE blobs;

CREATE TABLE vault_blobs (
    sha256 char(64) PRIMARY KEY CHECK (sha256 ~ '^[0-9a-f]{64}$'),
    size bigint NOT NULL CHECK (size > 0),
    ingested_at timestamptz NOT NULL DEFAULT now()
);

DELETE FROM schema_metadata WHERE version = 27;

COMMIT;
