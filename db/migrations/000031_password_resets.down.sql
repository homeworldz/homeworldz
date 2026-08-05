DROP INDEX IF EXISTS account_password_resets_token_hash_idx;
DROP TABLE IF EXISTS account_password_resets;

DELETE FROM schema_metadata WHERE version = 31;
