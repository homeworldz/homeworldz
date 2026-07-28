-- Retained records for no-copy items that leave inventory into the scene
-- (ADR 0026, revision 4db2eaa). Rezzing a no-copy item, or dropping one into
-- an object's contents, removes its inventory row — but that row's metadata
-- is what makes the vault's bytes reconstitutable, and without it a region
-- bug, total data loss, or a malicious region destroys the item permanently.
--
-- A retained record is NOT inventory: it is a separate table precisely so no
-- inventory listing, viewer fetch, or export can leak one by forgetting a
-- filter. The item genuinely left the inventory. Recovery from a retained
-- record is a deliberate operator action; an ordinary take-back through the
-- live region clears it.
BEGIN;

CREATE TABLE retained_inventory_items (
    -- The departed item's own id: a retry of the same finalize is the same
    -- record, and a recovered item can keep its identity.
    id uuid PRIMARY KEY,
    owner_user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    -- No FK: the creator's account may be deleted later, and this record must
    -- outlive everything except the owner.
    creator_user_id uuid,
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
    created_at timestamptz NOT NULL,
    -- Where the item went: which region's scene, inside which object. This is
    -- what lets an ordinary take-back clear the record precisely, and an
    -- operator scope recovery to a lost region.
    region_id uuid NOT NULL,
    object_id uuid NOT NULL,
    retained_at timestamptz NOT NULL DEFAULT now()
);

-- Take-back clears by (owner, object, asset); operator recovery lists by owner.
CREATE INDEX retained_inventory_items_owner
    ON retained_inventory_items(owner_user_id, object_id, asset_id);

INSERT INTO schema_metadata (version) VALUES (28);

COMMIT;
