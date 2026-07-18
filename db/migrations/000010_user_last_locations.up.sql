ALTER TABLE users
    ADD COLUMN last_region_id uuid REFERENCES regions(id) ON DELETE SET NULL,
    ADD COLUMN last_position_x real,
    ADD COLUMN last_position_y real,
    ADD COLUMN last_position_z real,
    ADD COLUMN last_look_x real,
    ADD COLUMN last_look_y real,
    ADD COLUMN last_look_z real,
    ADD COLUMN last_flying boolean,
    ADD COLUMN last_location_updated_at timestamptz;

CREATE INDEX users_last_region_idx ON users(last_region_id);

INSERT INTO schema_metadata (version) VALUES (10);
