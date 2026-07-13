CREATE TABLE schema_metadata (
    version bigint PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT now()
);
INSERT INTO schema_metadata (version) VALUES (1);

CREATE TABLE users (
    id uuid PRIMARY KEY,
    username text NOT NULL UNIQUE,
    password_hash text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE sessions (
    id uuid PRIMARY KEY,
    user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at timestamptz NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE regions (
    id uuid PRIMARY KEY,
    name text NOT NULL,
    grid_x integer NOT NULL,
    grid_y integer NOT NULL,
    public_endpoint text NOT NULL,
    lease_expires_at timestamptz NOT NULL,
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (grid_x, grid_y)
);
CREATE TABLE presence (
    user_id uuid PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    region_id uuid REFERENCES regions(id) ON DELETE SET NULL,
    last_seen_at timestamptz NOT NULL
);

