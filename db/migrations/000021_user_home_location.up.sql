ALTER TABLE users
    ADD COLUMN home_region_id uuid REFERENCES regions(id) ON DELETE SET NULL,
    ADD COLUMN home_position_x real,
    ADD COLUMN home_position_y real,
    ADD COLUMN home_position_z real,
    ADD COLUMN home_look_x real,
    ADD COLUMN home_look_y real,
    ADD COLUMN home_look_z real,
    ADD COLUMN home_updated_at timestamptz;

INSERT INTO schema_metadata (version) VALUES (21);
