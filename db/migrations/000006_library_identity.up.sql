INSERT INTO users (id, username, password_hash, viewer_password_hash)
VALUES (
    '00000000-0000-0000-0000-000000000002',
    'homeworldz.library',
    '!',
    NULL
)
ON CONFLICT (id) DO NOTHING;

INSERT INTO schema_metadata (version) VALUES (6);
