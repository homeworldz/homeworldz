-- Normalized display-name key for uniqueness and flexible login. It mirrors the
-- authoritative userid derivation (DeriveUserid / src/lib/userid.js): lowercase,
-- replace every run of non-[a-z0-9'.] characters with a single period, collapse
-- consecutive periods, and trim leading/trailing periods. A blank result (no
-- usable characters) stores NULL so it never participates in uniqueness or login.
ALTER TABLE users
    ADD COLUMN display_name_key text GENERATED ALWAYS AS (
        nullif(
            trim(both '.' from
                regexp_replace(
                    regexp_replace(lower(display_name), '[^a-z0-9''.]+', '.', 'g'),
                    '\.+', '.', 'g'
                )
            ),
            ''
        )
    ) STORED;

-- No two accounts may share a normalized display name. Combined with the
-- existing unique constraint on username (the userid), this also blocks a
-- registration whose derived userid equals an existing normalized display name,
-- because at registration the userid and the display-name key are identical.
CREATE UNIQUE INDEX users_display_name_key_key ON users (display_name_key);

INSERT INTO schema_metadata (version) VALUES (19);
