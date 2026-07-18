# ADR 0012: Region-To-Grid Registration Client

Status: Superseded by ADR 0024

The C++ region includes a dependency-free HTTP/1.1 client for development grid
URLs. In the original implementation, supplying a process-wide service token
enabled startup registration;
the region sends its bearer token and a request ID, renews halfway through each
lease, and deregisters during an orderly shutdown. Registration or renewal
failure prevents the region from continuing as an undiscoverable simulation.

ADR 0024 replaced process-wide registration identity with a provisioned Region
UUID or name plus its independently rotatable access key. The grid service
token remains a transitional file-backed credential for other internal APIs.

The initial socket transport supports `http://` on Windows and Linux. Deployed
HTTPS support will use a maintained TLS transport rather than implementing TLS
inside HomeWorldz; production deployment remains out of scope for this
milestone.
