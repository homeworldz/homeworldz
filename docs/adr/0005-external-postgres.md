# ADR 0005: External PostgreSQL

Status: Accepted

HomeWorldz supports PostgreSQL 16 or newer and recommends PostgreSQL 18.4 for
new installations. It does not prescribe how PostgreSQL is installed or
operated. Development may use a native Windows or Linux installation, while
deployments may use a separately managed database service.

The grid service receives its connection string through `config/db.ini` or the
`HOMEWORLDZ_DATABASE_URL` override. The Go grid bootstrap applies repository
migrations directly through the PostgreSQL driver, so neither `psql` nor
container tooling is required by HomeWorldz.
