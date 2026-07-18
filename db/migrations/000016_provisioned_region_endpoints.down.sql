ALTER TABLE provisioned_regions
    DROP COLUMN viewer_port,
    DROP COLUMN public_endpoint;
DELETE FROM schema_metadata WHERE version = 16;
