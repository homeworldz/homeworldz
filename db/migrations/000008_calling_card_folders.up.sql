DROP INDEX inventory_folders_system_type;

CREATE UNIQUE INDEX inventory_folders_system_type
    ON inventory_folders(owner_user_id, type_default)
    WHERE type_default >= 0 AND type_default <> 2;

INSERT INTO schema_metadata (version) VALUES (8);
