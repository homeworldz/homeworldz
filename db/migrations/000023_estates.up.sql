CREATE TABLE estates (
    id integer PRIMARY KEY,
    name text NOT NULL,
    owner_user_id uuid REFERENCES users(id) ON DELETE SET NULL,
    parent_estate_id integer NOT NULL DEFAULT 1,
    flags bigint NOT NULL DEFAULT 0,
    public_access boolean NOT NULL DEFAULT true,
    sun_hour double precision NOT NULL DEFAULT 0,
    use_global_time boolean NOT NULL DEFAULT true,
    fixed_sun boolean NOT NULL DEFAULT false,
    billable_factor double precision NOT NULL DEFAULT 0,
    price_per_meter integer NOT NULL DEFAULT 0,
    redirect_grid_x integer NOT NULL DEFAULT 0,
    redirect_grid_y integer NOT NULL DEFAULT 0,
    abuse_email text NOT NULL DEFAULT '',
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX estates_name_ci_idx ON estates (lower(name));

-- Every provisioned region belongs to exactly one estate. Estates are shared:
-- multiple regions may map to the same estate id, and estate settings/lists then
-- apply to all of them.
CREATE TABLE estate_regions (
    region_id uuid PRIMARY KEY REFERENCES provisioned_regions(id) ON DELETE CASCADE,
    estate_id integer NOT NULL REFERENCES estates(id) ON DELETE RESTRICT
);

CREATE INDEX estate_regions_estate_idx ON estate_regions (estate_id);

-- Estate access control lists. role: 0 = manager, 1 = allowed user,
-- 2 = allowed group, 3 = banned user.
CREATE TABLE estate_members (
    estate_id integer NOT NULL REFERENCES estates(id) ON DELETE CASCADE,
    member_id uuid NOT NULL,
    role smallint NOT NULL CHECK (role BETWEEN 0 AND 3),
    PRIMARY KEY (estate_id, member_id, role)
);

INSERT INTO schema_metadata (version) VALUES (23);
