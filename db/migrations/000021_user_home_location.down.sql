ALTER TABLE users
    DROP COLUMN IF EXISTS home_updated_at,
    DROP COLUMN IF EXISTS home_look_z,
    DROP COLUMN IF EXISTS home_look_y,
    DROP COLUMN IF EXISTS home_look_x,
    DROP COLUMN IF EXISTS home_position_z,
    DROP COLUMN IF EXISTS home_position_y,
    DROP COLUMN IF EXISTS home_position_x,
    DROP COLUMN IF EXISTS home_region_id;
DELETE FROM schema_metadata WHERE version = 21;
