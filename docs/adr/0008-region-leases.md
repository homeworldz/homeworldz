# ADR 0008: Region Registration Leases

Status: Accepted

The grid assigns a UUID when a region registers and grants a renewable lease
for one grid coordinate pair. Leases default to 60 seconds and may be requested
for 10 through 300 seconds. Renewal replaces the expiry time relative to the
grid database clock.

Only regions with unexpired leases are discoverable or renewable. Registration
removes expired rows before claiming coordinates, allowing a replacement region
to recover a location without an operator manually deleting stale state.
