-- Dropping the index does not remove blob files from the vault filesystem; an
-- operator reverting this migration keeps the bytes and can re-index them by
-- re-ingesting or by a backfill.
DROP TABLE IF EXISTS vault_blobs;

DELETE FROM schema_metadata WHERE version = 24;
