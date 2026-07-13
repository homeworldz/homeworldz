# ADR 0009: Development Users And Sessions

Status: Accepted

Milestone 1 uses explicitly created development users with normalized lowercase
usernames and bcrypt password hashes. Plaintext passwords are accepted only at
the user-creation and session-creation boundaries and are never stored or
logged.

The grid issues random UUID session identifiers. Sessions default to 12 hours,
may be requested for 5 minutes through 24 hours, and are valid only before their
database expiry time. Grid clients validate sessions through the authenticated
internal API; deleting a session revokes it immediately.
