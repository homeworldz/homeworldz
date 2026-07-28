-- Drops the rendition registry and conversion queue of ADR 0033. Rendition
-- blobs themselves are ordinary registry blobs and are not touched; they
-- become unreferenced, which the deferred blob collection reclaims. Canonical
-- asset bytes are unaffected — renditions are derived data by definition.
BEGIN;

DROP TABLE rendition_jobs;
DROP TABLE asset_renditions;

DELETE FROM schema_metadata WHERE version = 29;

COMMIT;
