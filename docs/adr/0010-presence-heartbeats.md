# ADR 0010: Presence Heartbeats

Status: Accepted

Authenticated grid clients maintain online presence by sending user and active
region identifiers. Each update replaces the user's region and records the grid
database time as the latest heartbeat.

Presence becomes stale after 90 seconds. Stale entries are excluded from direct
lookups and removed during presence discovery. Explicit disconnects delete the
entry immediately. Updates require both an existing user and a region with an
active registration lease.
