CREATE TABLE task_inventory_extractions (
    id uuid PRIMARY KEY,
    user_id uuid NOT NULL,
    region_id uuid NOT NULL,
    object_id uuid NOT NULL,
    source_task_item_id uuid NOT NULL,
    destination_folder_id uuid NOT NULL,
    personal_item_id uuid NOT NULL,
    item jsonb NOT NULL,
    state text NOT NULL CHECK (state IN ('prepared', 'finalized')),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (region_id, object_id, source_task_item_id)
);

CREATE INDEX task_inventory_extractions_pending_region
    ON task_inventory_extractions(region_id)
    WHERE state = 'prepared';

INSERT INTO schema_metadata (version) VALUES (13);
