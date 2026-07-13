# ADR 0012: Region-To-Grid Registration Client

Status: Accepted

The C++ region includes a dependency-free HTTP/1.1 client for development grid
URLs. Supplying `HOMEWORLDZ_GRID_SERVICE_TOKEN` enables startup registration;
the region sends its bearer token and a request ID, renews halfway through each
lease, and deregisters during an orderly shutdown. Registration or renewal
failure prevents the region from continuing as an undiscoverable simulation.

The initial socket transport supports `http://` on Windows and Linux. Deployed
HTTPS support will use a maintained TLS transport rather than implementing TLS
inside HomeWorldz; production deployment remains out of scope for this
milestone.
