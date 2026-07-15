CREATE TABLE asset_metadata (
    asset_id uuid PRIMARY KEY,
    creator_user_id uuid NOT NULL,
    sha256 char(64) NOT NULL CHECK (sha256 ~ '^[0-9a-f]{64}$'),
    size bigint NOT NULL CHECK (size > 0),
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE asset_locations (
    asset_id uuid NOT NULL REFERENCES asset_metadata(asset_id) ON DELETE CASCADE,
    endpoint text NOT NULL CHECK (length(endpoint) BETWEEN 8 AND 2048),
    is_origin boolean NOT NULL DEFAULT false,
    verified_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (asset_id, endpoint)
);

CREATE INDEX asset_locations_asset ON asset_locations(asset_id, is_origin DESC, verified_at DESC);
INSERT INTO schema_metadata (version) VALUES (7);
