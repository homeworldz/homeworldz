CREATE TABLE avatar_transits (
    id uuid PRIMARY KEY,
    generation bigint NOT NULL CHECK (generation > 0),
    agent_id uuid NOT NULL,
    session_id uuid NOT NULL,
    source_region_id uuid NOT NULL,
    destination_region_id uuid NOT NULL,
    position_x real NOT NULL,
    position_y real NOT NULL,
    position_z real NOT NULL,
    look_x real NOT NULL,
    look_y real NOT NULL,
    look_z real NOT NULL,
    flying boolean NOT NULL,
    state text NOT NULL CHECK (state IN ('prepared', 'accepted', 'activated', 'rolled_back')),
    rollback_reason text NOT NULL DEFAULT '',
    expires_at timestamptz NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CHECK (source_region_id <> destination_region_id),
    UNIQUE (agent_id, generation)
);

CREATE UNIQUE INDEX avatar_transits_one_active_per_agent
    ON avatar_transits(agent_id)
    WHERE state IN ('prepared', 'accepted');

CREATE INDEX avatar_transits_expiry
    ON avatar_transits(expires_at)
    WHERE state IN ('prepared', 'accepted');

INSERT INTO schema_metadata (version) VALUES (9);
