# ADR 0022: Deployment Port Profiles

Status: Accepted

HomeWorldz distinguishes source-development ports from packaged deployment
profiles. ADR 0004 continues to reserve `42000/tcp`, `42001/tcp`, and
`42002/udp` for repository development and smoke tests.

The packaged personal-machine profile exposes grid discovery and login only on
loopback `8002/tcp`. Its first region retains loopback `42001/tcp` for HTTP
capabilities and `42002/udp` for the viewer circuit.

The initial cloud profile binds grid discovery and login directly to public
`80/tcp`, without requiring a reverse proxy or load balancer. Its first region
is directly reachable on `42001/tcp` and `42002/udp`. A DNS name such as
`sandbox.homeworldz.com` lets a Firestorm user enter only the hostname; login
advertises the region ports automatically.

Direct port 80 is appropriate only for the initial test/demo posture. Public
production login requires TLS. Native HomeWorldz TLS listeners and certificate
operation are future work; binding plain HTTP to port 443 is not supported.
Each independent grid has its own DNS name, PostgreSQL database, service token,
configuration, regions, and persistent data.
