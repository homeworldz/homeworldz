-- Single-use, email-delivered password reset tokens.
--
-- Only the SHA-256 of the token is stored; the plaintext goes out by email once
-- and is never recoverable from here. Same shape as account_verifications,
-- deliberately: one row per user, so requesting a reset replaces any pending one
-- rather than accumulating live tokens for the same account.
--
-- used_at rather than deleting the row on use. A consumed token has to stay
-- distinguishable from an unknown one for as long as it would otherwise have
-- been valid, so a second click on the same emailed link is refused for the
-- right reason and can be seen to have been refused. Rows are cleared by the
-- expiry sweep, not by the successful reset.
CREATE TABLE account_password_resets (
    user_id uuid PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    token_hash bytea NOT NULL CHECK (octet_length(token_hash) = 32),
    email text NOT NULL,
    expires_at timestamptz NOT NULL,
    used_at timestamptz,
    created_at timestamptz NOT NULL DEFAULT now()
);

-- The consume path looks a token up by its hash and knows nothing else about it,
-- so the hash needs its own index rather than relying on the user_id key.
CREATE UNIQUE INDEX account_password_resets_token_hash_idx
    ON account_password_resets (token_hash);
