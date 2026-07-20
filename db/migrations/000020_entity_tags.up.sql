-- Classification for accounts and provisioned regions: a single mutually-
-- exclusive "kind" plus a normalized, comma-separated "tags" list of additional
-- labels. User kinds: system | testing | default. Region kinds: grid | user
-- (provenance; essential status is a tag, not a kind). Tags are open-ended
-- lowercase tokens (e.g. system, admin, ocean). Both default empty tags; kind
-- carries a sensible default per entity.

ALTER TABLE users
    ADD COLUMN kind text NOT NULL DEFAULT 'default',
    ADD COLUMN tags text NOT NULL DEFAULT '';

ALTER TABLE provisioned_regions
    ADD COLUMN kind text NOT NULL DEFAULT 'user',
    ADD COLUMN tags text NOT NULL DEFAULT '';

-- Existing provisioned regions are grid-provided starter regions.
UPDATE provisioned_regions SET kind = 'grid';

INSERT INTO schema_metadata (version) VALUES (20);
