-- Renditions are regenerable, so dropping the kind loses nothing that cannot be
-- rebuilt from canonicals. Existing rows must go first or the narrowed
-- constraint cannot be added.

DELETE FROM rendition_jobs WHERE kind = 'png-texture';
DELETE FROM asset_renditions WHERE kind = 'png-texture';

ALTER TABLE asset_renditions DROP CONSTRAINT asset_renditions_kind_check;
ALTER TABLE asset_renditions ADD CONSTRAINT asset_renditions_kind_check
    CHECK (kind IN ('gltf', 'sl-mesh', 'sl-material', 'j2c-texture'));

ALTER TABLE rendition_jobs DROP CONSTRAINT rendition_jobs_kind_check;
ALTER TABLE rendition_jobs ADD CONSTRAINT rendition_jobs_kind_check
    CHECK (kind IN ('gltf', 'sl-mesh', 'sl-material', 'j2c-texture'));
