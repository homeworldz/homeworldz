DROP TABLE account_bans;
DROP TABLE account_verifications;

-- Restore the pre-website NOT NULL guarantee. Any unverified (password-less)
-- accounts must be removed before reverting this migration.
DELETE FROM users WHERE password_hash IS NULL;
ALTER TABLE users ALTER COLUMN password_hash SET NOT NULL;

ALTER TABLE users
    DROP COLUMN auth_version,
    DROP COLUMN privileges,
    DROP COLUMN verified_at,
    DROP COLUMN display_name,
    DROP COLUMN email;

DELETE FROM schema_metadata WHERE version = 18;
