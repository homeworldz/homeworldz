CREATE TABLE active_gestures (
    user_id      uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    item_id      uuid NOT NULL,
    asset_id     uuid NOT NULL,
    activated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (user_id, item_id)
);

INSERT INTO schema_metadata (version) VALUES (22);
