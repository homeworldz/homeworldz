# ADR 0005: External PostgreSQL

Status: Accepted

HomeWorldz supports PostgreSQL 16 or newer and recommends PostgreSQL 18.4 for
new installations. It does not prescribe how PostgreSQL is installed or
operated. Development may use a native Windows or Linux installation, while
deployments may use a separately managed database service.

The grid service receives its connection string through
`HOMEWORLDZ_DATABASE_URL`. Repository migrations are ordinary SQL files that
can be applied with `psql`; container tooling is not required by the project.
