DELETE FROM inventory_folders child
USING inventory_folders parent
WHERE child.parent_id = parent.id
  AND child.owner_user_id = parent.owner_user_id
  AND child.type_default = 2
  AND parent.type_default = 2;

DROP INDEX inventory_folders_system_type;

CREATE UNIQUE INDEX inventory_folders_system_type
    ON inventory_folders(owner_user_id, type_default)
    WHERE type_default >= 0;

DELETE FROM schema_metadata WHERE version = 8;
