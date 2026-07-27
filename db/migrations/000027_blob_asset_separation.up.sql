-- Separate the blob, asset, and instance layers (ADR 0027). Byte identity
-- becomes a grid-assigned blob_id and the digest is demoted to an integrity
-- checksum; creator, provenance, and the exportable option live on the asset;
-- serving locations attach to the blob, because identical bytes reachable at
-- an endpoint are reachable regardless of which asset names them.
--
-- Instances do not move: inventory items and rezzed objects already carry the
-- owner and the SL permission masks, and permissions were never on assets.
--
-- The region-facing wire shape is deliberately unchanged by this migration.
-- A region still names assets and verifies bytes with the checksum; blob_id
-- is grid-internal indirection, so no region needs upgrading for this step.

-- Explicitly transactional: this migration moves the rows inventory depends
-- on, and a half-applied run would be worse than a failed one. Wrapping it
-- here makes it all-or-nothing under any runner rather than relying on one's
-- implicit behaviour.
BEGIN;

-- The vault is re-keyed from the digest to blob_id, which is free only while
-- it is empty: its bytes live in a tree sharded by the old key, so dropping
-- the index with blobs present would orphan every file. Refuse rather than
-- silently strand them.
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM vault_blobs) THEN
        RAISE EXCEPTION
            'vault_blobs is not empty: re-keying to blob_id would orphan stored bytes. Migrate the vault filesystem first.';
    END IF;
END $$;

CREATE TABLE blobs (
    blob_id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    byte_length bigint NOT NULL CHECK (byte_length > 0),
    -- Metadata, not identity: its load-bearing purpose is verifying bytes
    -- fetched across a trust boundary. The algorithm is recorded so a
    -- stronger one can replace it as a data migration, not a redesign.
    checksum text NOT NULL CHECK (length(checksum) BETWEEN 32 AND 128),
    checksum_algorithm text NOT NULL DEFAULT 'sha256',
    created_at timestamptz NOT NULL DEFAULT now()
);

-- Deliberately not unique: the design permits byte-identical blobs, and the
-- asynchronous deduplication service coalesces them by literal byte
-- comparison when and if it runs.
CREATE INDEX blobs_checksum ON blobs (checksum, byte_length);

CREATE TABLE blob_locations (
    blob_id uuid NOT NULL REFERENCES blobs(blob_id) ON DELETE CASCADE,
    endpoint text NOT NULL CHECK (length(endpoint) BETWEEN 8 AND 2048),
    is_origin boolean NOT NULL DEFAULT false,
    verified_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (blob_id, endpoint)
);

CREATE INDEX blob_locations_blob ON blob_locations (blob_id, is_origin DESC, verified_at DESC);

CREATE TABLE assets (
    asset_id uuid PRIMARY KEY,
    blob_id uuid NOT NULL REFERENCES blobs(blob_id),
    creator_user_id uuid NOT NULL,
    -- Creator-level policy over shared bytes, which is why it cannot live on
    -- the blob: two creators may share bytes and disagree about export.
    exportable boolean NOT NULL DEFAULT true,
    provenance jsonb NOT NULL DEFAULT '{}'::jsonb,
    -- Derived optimization. Any destructive action on a zero must recompute
    -- from back-links first; a cached non-zero is safe to trust.
    cached_refcount integer NOT NULL DEFAULT 0,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX assets_blob ON assets (blob_id);
CREATE INDEX assets_creator ON assets (creator_user_id);

-- Seed one blob per distinct (digest, length) already registered, repoint each
-- asset at it, and move location rows from the asset to its blob.
INSERT INTO blobs (byte_length, checksum, checksum_algorithm, created_at)
SELECT DISTINCT ON (sha256, size) size, sha256, 'sha256', min(created_at)
FROM asset_metadata
GROUP BY sha256, size;

INSERT INTO assets (asset_id, blob_id, creator_user_id, created_at)
SELECT source.asset_id, blob.blob_id, source.creator_user_id, source.created_at
FROM asset_metadata AS source
JOIN blobs AS blob
  ON blob.checksum = source.sha256 AND blob.byte_length = source.size;

-- Aggregated rather than upserted: assets that share a blob commonly share
-- endpoints too, and several proposed rows for one (blob_id, endpoint) cannot
-- be resolved by ON CONFLICT within a single statement. Origin latches on if
-- any contributing asset called that endpoint an origin.
INSERT INTO blob_locations (blob_id, endpoint, is_origin, verified_at)
SELECT asset.blob_id, source.endpoint,
       bool_or(source.is_origin), max(source.verified_at)
FROM asset_locations AS source
JOIN assets AS asset ON asset.asset_id = source.asset_id
GROUP BY asset.blob_id, source.endpoint;

-- Reference counts from the instance layer's back-links, which is the source
-- of truth; inventory items are instances.
UPDATE assets SET cached_refcount = counted.instances
FROM (SELECT asset_id, count(*) AS instances FROM inventory_items GROUP BY asset_id) AS counted
WHERE counted.asset_id = assets.asset_id;

DROP TABLE asset_locations;
DROP TABLE asset_metadata;

-- The vault indexes blobs, and a blob it holds must not be deletable while
-- its bytes are there: no cascade, so a delete is refused rather than
-- silently leaking files. Garbage collection remains deferred.
DROP TABLE vault_blobs;

CREATE TABLE vault_blobs (
    blob_id uuid PRIMARY KEY REFERENCES blobs(blob_id),
    byte_length bigint NOT NULL CHECK (byte_length > 0),
    ingested_at timestamptz NOT NULL DEFAULT now()
);

INSERT INTO schema_metadata (version) VALUES (27);

COMMIT;
