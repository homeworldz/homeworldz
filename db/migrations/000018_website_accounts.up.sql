-- Website account attributes for browser-facing registration and administration.
-- These columns extend the existing viewer-login users table with the identity,
-- verification, privilege, and authorization-version state the website API needs.
ALTER TABLE users
    ADD COLUMN email text,
    ADD COLUMN display_name text,
    ADD COLUMN verified_at timestamptz,
    ADD COLUMN privileges text NOT NULL DEFAULT '',
    ADD COLUMN auth_version integer NOT NULL DEFAULT 0;

-- Unverified accounts exist (to reserve the derived userid) before a password is
-- set, so password material is only populated at verification time.
ALTER TABLE users ALTER COLUMN password_hash DROP NOT NULL;

-- Single-use, email-delivered confirmation codes for unverified accounts. Only
-- the SHA-256 of the code is stored; the plaintext is delivered by email once.
CREATE TABLE account_verifications (
    user_id uuid PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    code_hash bytea NOT NULL CHECK (octet_length(code_hash) = 32),
    email text NOT NULL,
    expires_at timestamptz NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now()
);

-- Account suspensions. A present row means the account is banned; an
-- expires_at in the future is a temporary ban, NULL is permanent.
CREATE TABLE account_bans (
    user_id uuid PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    reason text NOT NULL CHECK (char_length(reason) BETWEEN 1 AND 1024),
    expires_at timestamptz,
    banned_at timestamptz NOT NULL DEFAULT now(),
    banned_by uuid NOT NULL REFERENCES users(id) ON DELETE RESTRICT
);

INSERT INTO schema_metadata (version) VALUES (18);
