DELETE FROM users
WHERE id = '00000000-0000-0000-0000-000000000002'
  AND username = 'homeworldz.library';

DELETE FROM schema_metadata WHERE version = 6;
