CREATE TABLE provisioned_regions (
    id uuid PRIMARY KEY,
    name text NOT NULL,
    owner_user_id uuid REFERENCES users(id) ON DELETE SET NULL,
    grid_x integer NOT NULL CHECK (grid_x >= 0),
    grid_y integer NOT NULL CHECK (grid_y >= 0),
    enabled boolean NOT NULL DEFAULT true,
    access_key_hash bytea NOT NULL CHECK (octet_length(access_key_hash) = 32),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (grid_x, grid_y)
);

CREATE UNIQUE INDEX provisioned_regions_name_ci_idx
    ON provisioned_regions (lower(name));

INSERT INTO schema_metadata (version) VALUES (15);
