CREATE TABLE inventory_folders (
    id uuid PRIMARY KEY,
    owner_user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    parent_id uuid,
    name text NOT NULL CHECK (length(name) BETWEEN 1 AND 255),
    type_default smallint NOT NULL CHECK (type_default BETWEEN -1 AND 127),
    version bigint NOT NULL DEFAULT 1 CHECK (version > 0),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (id, owner_user_id),
    FOREIGN KEY (parent_id, owner_user_id)
        REFERENCES inventory_folders(id, owner_user_id) ON DELETE CASCADE,
    CHECK (parent_id IS NOT NULL OR type_default = 8)
);

CREATE UNIQUE INDEX inventory_folders_system_type
    ON inventory_folders(owner_user_id, type_default)
    WHERE type_default >= 0;
CREATE INDEX inventory_folders_parent ON inventory_folders(parent_id);

CREATE TABLE inventory_items (
    id uuid PRIMARY KEY,
    owner_user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    creator_user_id uuid REFERENCES users(id) ON DELETE SET NULL,
    folder_id uuid NOT NULL,
    asset_id uuid NOT NULL,
    asset_type smallint NOT NULL CHECK (asset_type BETWEEN 0 AND 127),
    inventory_type smallint NOT NULL CHECK (inventory_type BETWEEN 0 AND 127),
    name text NOT NULL CHECK (length(name) BETWEEN 1 AND 255),
    description text NOT NULL DEFAULT '' CHECK (length(description) <= 1024),
    flags bigint NOT NULL DEFAULT 0 CHECK (flags BETWEEN 0 AND 4294967295),
    base_permissions bigint NOT NULL DEFAULT 0 CHECK (base_permissions BETWEEN 0 AND 4294967295),
    current_permissions bigint NOT NULL DEFAULT 0 CHECK (current_permissions BETWEEN 0 AND 4294967295),
    everyone_permissions bigint NOT NULL DEFAULT 0 CHECK (everyone_permissions BETWEEN 0 AND 4294967295),
    next_permissions bigint NOT NULL DEFAULT 0 CHECK (next_permissions BETWEEN 0 AND 4294967295),
    sale_type smallint NOT NULL DEFAULT 0 CHECK (sale_type BETWEEN 0 AND 3),
    sale_price integer NOT NULL DEFAULT 0 CHECK (sale_price >= 0),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    FOREIGN KEY (folder_id, owner_user_id)
        REFERENCES inventory_folders(id, owner_user_id) ON DELETE CASCADE
);

CREATE INDEX inventory_items_folder ON inventory_items(folder_id);
CREATE INDEX inventory_items_asset ON inventory_items(asset_id);

INSERT INTO schema_metadata (version) VALUES (5);
