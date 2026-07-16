# ADR 0024: Provisioned Region Identity and Bootstrap Configuration

Status: Accepted

The grid service will maintain a persistent provisioned record for every
region. A region record is keyed by a UUID and contains a unique display name,
owner UUID, grid X/Y location, public service endpoints, enabled state, and a
hash of an independently generated region access key. Access keys are scoped to
one region and can be rotated or revoked without affecting any other region.
The grid never stores or returns the plaintext key after provisioning.

Grid-management endpoints will create, inspect, update, enable, disable,
relocate, rotate credentials for, and remove these records. Those operator
endpoints use grid-administration authorization separate from region access
keys. Coordinate and name uniqueness are enforced by the authoritative grid
database.

A region host starts from a small local bootstrap file containing the grid
service URL, a region UUID (preferred) or unique region name, and its region
access key. It authenticates to a bootstrap endpoint, receives its effective
grid-wide and region-specific configuration, and then acquires and renews an
online lease. Host-local bind addresses and storage paths use packaged defaults
unless a later installation requirement demonstrates that they need explicit
local settings.

Local-package examples may default the grid URL to `http://127.0.0.1:8002`.
The region must not silently fall back from a local address to a public grid:
doing so could disclose its access key to an unintended service and would make
startup behavior depend on external reachability. Hosted installations specify
their grid URL explicitly.

Provisioning and online presence are separate states. Stopping or losing a
lease marks a provisioned region offline; it does not delete the region record,
release its identity, or discard its configured location. Neighbor discovery
uses provisioned coordinates and online lease state to decide whether a border
can accept a crossing.

The current grid-wide service token, dynamically assigned registration UUID,
and full `region.ini` are transitional development mechanisms. They will be
replaced by per-region credentials and bootstrap configuration. This decision
supersedes the identity and deletion aspects of ADR 0008 and ADR 0012 while
retaining renewable leases as the online-liveness mechanism.

The first implementation uses a private grid-side `regions.json` array as the
provisioning authority. Each row contains `id`, `name`, `mapX`, `mapY`, and a
plaintext `accessKey`; startup rejects duplicate UUIDs, names, or coordinates.
The region supplies `--region-id` and `--access-key`, while its local INI keeps
host-specific ports, endpoints, paths, and the grid URL. This deliberately
small file-backed stage enables multiple fixed-identity regions before the
management endpoints and hashed database representation above are available.
