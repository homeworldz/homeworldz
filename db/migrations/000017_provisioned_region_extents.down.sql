ALTER TABLE provisioned_regions
    DROP CONSTRAINT provisioned_regions_no_overlap,
    DROP COLUMN maturity,
    DROP COLUMN size;
DELETE FROM schema_metadata WHERE version = 17;
