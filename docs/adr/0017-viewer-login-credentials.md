# ADR 0017: Viewer Login Credentials

Status: Accepted

Firestorm's OpenSim login sends `passwd` as `$1$` followed by the MD5 digest of
the entered password. A bcrypt-only password record cannot validate that value
because the grid never receives the original password during viewer login.

Development user creation therefore stores both the bcrypt password hash used
by HomeWorldz JSON login and the lowercase Firestorm-compatible MD5 digest used
only by `/login`. The MD5 value is password-equivalent legacy protocol material:
it is never returned, never logged, and must receive the same database and
backup protection as the bcrypt hash. This is compatibility containment, not a
general endorsement of MD5 authentication.

Migration 000002 also adds a private secure-session UUID. Viewer login creates
both session identifiers and returns them only in the external login response.
Existing development users created before migration 000002 must be recreated
to gain a viewer credential until password reset is implemented.
