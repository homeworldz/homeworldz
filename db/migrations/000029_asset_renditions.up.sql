-- Asset renditions and the conversion queue (ADR 0033). A rendition is a
-- derived encoding of an asset — sl-mesh for viewers, gltf for the modern
-- client, extracted materials and textures — regenerable from the canonical
-- blob and therefore exempt from vault durability (the same standing baked
-- textures have under ADR 0026). The generator column names the tool version
-- that produced a rendition, so a better converter can find and regenerate
-- everything it supersedes.
BEGIN;

CREATE TABLE asset_renditions (
    asset_id uuid NOT NULL REFERENCES assets(asset_id) ON DELETE CASCADE,
    kind text NOT NULL CHECK (kind IN ('gltf', 'sl-mesh', 'sl-material', 'j2c-texture')),
    blob_id uuid NOT NULL REFERENCES blobs(blob_id),
    generator text NOT NULL CHECK (length(generator) BETWEEN 1 AND 128),
    generated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (asset_id, kind)
);

-- The queue the conversion worker consumes. One live job per (asset, kind):
-- requesting an already-queued conversion is a no-op, requesting a failed one
-- re-queues it. Leases rather than locks, so a worker that dies mid-job
-- simply lets the lease lapse and the job is claimable again.
CREATE TABLE rendition_jobs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    asset_id uuid NOT NULL REFERENCES assets(asset_id) ON DELETE CASCADE,
    kind text NOT NULL CHECK (kind IN ('gltf', 'sl-mesh', 'sl-material', 'j2c-texture')),
    state text NOT NULL DEFAULT 'queued' CHECK (state IN ('queued', 'leased', 'done', 'failed')),
    attempts integer NOT NULL DEFAULT 0 CHECK (attempts >= 0),
    leased_until timestamptz,
    error text NOT NULL DEFAULT '',
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (asset_id, kind)
);

CREATE INDEX rendition_jobs_claimable
    ON rendition_jobs(kind, created_at)
    WHERE state IN ('queued', 'leased');

INSERT INTO schema_metadata (version) VALUES (29);

COMMIT;
