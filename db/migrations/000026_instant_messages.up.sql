-- Instant messages between users, delivered over the grid channel with
-- store-and-forward (docs/CLIENT2.md, "What the grid channel carries today"):
-- a message is stored durably, delivered live to the recipient's open
-- channels when there are any, and replayed to the next channel they open
-- otherwise. delivered_at NULL marks the backlog.
CREATE TABLE instant_messages (
    id UUID PRIMARY KEY,
    from_user_id UUID NOT NULL REFERENCES users(id),
    to_user_id UUID NOT NULL REFERENCES users(id),
    message TEXT NOT NULL,
    sent_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    delivered_at TIMESTAMPTZ
);

CREATE INDEX instant_messages_undelivered
    ON instant_messages (to_user_id, sent_at)
    WHERE delivered_at IS NULL;

INSERT INTO schema_metadata (version) VALUES (26);
