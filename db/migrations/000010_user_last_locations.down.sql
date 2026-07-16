DROP INDEX IF EXISTS users_last_region_idx;
ALTER TABLE users
    DROP COLUMN IF EXISTS last_location_updated_at,
    DROP COLUMN IF EXISTS last_flying,
    DROP COLUMN IF EXISTS last_look_z,
    DROP COLUMN IF EXISTS last_look_y,
    DROP COLUMN IF EXISTS last_look_x,
    DROP COLUMN IF EXISTS last_position_z,
    DROP COLUMN IF EXISTS last_position_y,
    DROP COLUMN IF EXISTS last_position_x,
    DROP COLUMN IF EXISTS last_region_id;
