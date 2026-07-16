DROP INDEX IF EXISTS regions_expired_transient_idx;

ALTER TABLE regions
    DROP COLUMN IF EXISTS provisioned;

DELETE FROM schema_metadata WHERE version = 11;
