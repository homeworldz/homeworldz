-- The region session (docs/CLIENT2-TRANSPORT.md): a region that serves the
-- WebSocket session transport reports its public session endpoint at
-- registration, and world entry hands it to clients as data. Empty means the
-- region does not serve the transport.
ALTER TABLE regions ADD COLUMN session_endpoint TEXT NOT NULL DEFAULT '';
