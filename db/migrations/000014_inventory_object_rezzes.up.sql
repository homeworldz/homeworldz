CREATE TABLE inventory_object_rezzes (
    id uuid PRIMARY KEY,
    user_id uuid NOT NULL,
    source_item_id uuid NOT NULL,
    region_id uuid NOT NULL,
    object_id uuid NOT NULL,
    item jsonb NOT NULL,
    state text NOT NULL CHECK (state IN ('prepared', 'finalized', 'rolled_back')),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (user_id, source_item_id)
);

CREATE INDEX inventory_object_rezzes_pending_region
    ON inventory_object_rezzes(region_id)
    WHERE state = 'prepared';

INSERT INTO schema_metadata (version) VALUES (14);
