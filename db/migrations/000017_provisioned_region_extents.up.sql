ALTER TABLE provisioned_regions
    ADD COLUMN size integer NOT NULL DEFAULT 1 CHECK (size IN (1, 2, 4)),
    ADD COLUMN maturity integer NOT NULL DEFAULT 0 CHECK (maturity BETWEEN 0 AND 2);

ALTER TABLE provisioned_regions
    ADD CONSTRAINT provisioned_regions_no_overlap
    EXCLUDE USING gist (
        int4range(grid_x, grid_x + size, '[)') WITH &&,
        int4range(grid_y, grid_y + size, '[)') WITH &&
    );

INSERT INTO schema_metadata (version) VALUES (17);
