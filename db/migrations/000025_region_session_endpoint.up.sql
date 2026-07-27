-- The region session (docs/CLIENT2-TRANSPORT.md): a region that serves the
-- WebSocket session transport reports its public session endpoint at
-- registration, and world entry hands it to clients as data. Empty means the
-- region does not serve the transport.
-- IF NOT EXISTS because this migration once ran without recording its
-- version (the schema_metadata insert below was initially missing).
ALTER TABLE regions ADD COLUMN IF NOT EXISTS session_endpoint TEXT NOT NULL DEFAULT '';

INSERT INTO schema_metadata (version) VALUES (25);
