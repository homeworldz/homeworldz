# ADR 0019: Permission Semantics And References

Status: Accepted

HomeWorldz treats observable Second Life viewer and protocol behavior as the
primary compatibility contract for object and inventory permissions. The
official Second Life viewer and message definitions are the preferred sources
for viewer-facing flags, packets, and interaction behavior.

Halcyon is the preferred server-side reference implementation when the public
protocol does not fully specify permission evaluation. OpenSimulator behavior
is not authoritative for permissions and must not be copied merely for
compatibility. Tests and HomeWorldz's own security model decide ambiguous or
conflicting cases.

Creator provenance and ownership are independent UUIDs. A transfer may change
the owner but must not replace the creator. Permission checks must evaluate
nested object inventory where an operation depends on its contents; known
legacy behavior that checks only an outer object is not a compatibility goal.

Permission masks and viewer object flags are bitmasks. Source code and
developer documentation use named constants with hexadecimal values for these
masks. Decimal representations are acceptable only at boundaries whose format
requires ordinary JSON or database numbers.
