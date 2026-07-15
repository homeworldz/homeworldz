# Install a HomeWorldz Grid

This guide is for a **grid operator**: the person running central login,
identity, inventory, presence, and region-registration services. If you only
own a region connected to somebody else's grid, use the `INSTALL-REGION.md`
included in the region package instead. Region owners do not need the grid's
PostgreSQL credentials.

Development snapshots can now be assembled as release ZIPs. Public release
publication and an installer remain future work.
Archive names, executable metadata, and the packaged `VERSION` file are derived
from the repository's root `VERSION` source when the package is built.

## Expected grid package

The `homeworldz-grid-<version>-windows-x64.zip` or future
`homeworldz-grid-<version>-linux-x64.tar.gz` will contain the prebuilt grid and
bootstrap programs plus the files they need:

```text
homeworldz-grid/
  INSTALL-GRID.md
  INSTALL-REGION.md       # reference for connected region owners
  VERSION
  homeworldz-grid[.exe]
  bootstrap-grid[.exe]
  configure-library[.exe]
  config/
    examples/
      grid.ini              # repository-development ports
      grid-personal.ini     # loopback port 8002
      grid-cloud.ini        # direct public port 80
  db/
    migrations/*.up.sql
  docs/
    FEATURES.md
    ROADMAP.md
```

Extract it to a dedicated directory. Run the commands in this guide from that
directory. The service account must be able to read `config/`; only the
operator and service account should be able to read files containing secrets.

## Requirements

- PostgreSQL 16 or newer, locally installed or reachable over the network.
  PostgreSQL 18.4 is recommended for a new grid.
- A PostgreSQL administrator login for initial setup.
- The prebuilt package matching the host operating system and CPU.

The packaged grid does not require Go, Node.js, Visual Studio, CMake, `psql`,
Docker, or a C++ compiler.

## Configure the grid

Choose a deployment profile and copy it to the live, private configuration
file. Personal-machine grids should use `grid-personal.ini`; direct cloud test
grids should use `grid-cloud.ini`. The unqualified `grid.ini` retains the
repository-development ports.

Windows Command Prompt:

```cmd
copy config\examples\grid-personal.ini config\grid.ini
```

Linux:

```sh
cp config/examples/grid-personal.ini config/grid.ini
chmod 600 config/grid.ini
```

Edit `config/grid.ini`:

```ini
[server]
address = 127.0.0.1:8002
public_url = http://127.0.0.1:8002

[auth]
service_token = replace-with-a-long-random-secret
```

- `address` is the interface and TCP port on which the grid listens.
- `public_url` is the URL given to viewers and regions. It must be reachable by
  them; use the host's DNS name and HTTPS URL in a public deployment.
- `service_token` authenticates every region. Generate a strong, unique value,
  transmit it privately to region owners, and never commit it to source
  control.

Loopback port 8002 is appropriate when grid, region, and viewer are all on one
personal computer. It deliberately accepts no remote connections. The cloud
profile instead binds `0.0.0.0:80` and must be edited to use the grid's DNS
name, for example `http://sandbox.homeworldz.com`.

## Create the database

Run the packaged bootstrap interactively.

Windows:

```cmd
bootstrap-grid.exe
```

Linux:

```sh
./bootstrap-grid
```

It asks for the PostgreSQL administrator password and twice for the password to
assign to the `homeworldz` application role. The default operation connects to
`localhost:5432`, creates or updates that role, creates the `homeworldz`
database when absent, applies every pending migration, and writes the
application connection URL to private `config/db.ini`.

For remote or non-default PostgreSQL, use flags such as:

```text
bootstrap-grid -host database.example -port 5432 -admin-user postgres
```

Use `bootstrap-grid -help` for the complete current flag list. The bootstrap
must be run from the extracted package directory so it can find
`db/migrations`.

Protect `config/db.ini`: it contains the database password. PostgreSQL itself
must also be backed up and restricted according to normal database operations
practice. Do not give `db.ini` or the PostgreSQL password to region owners.

## Start and stop

Start in a terminal for the first run so startup errors remain visible:

Windows:

```cmd
homeworldz-grid.exe
```

Linux:

```sh
./homeworldz-grid
```

The service reads `config/grid.ini` and `config/db.ini`. Stop it with Ctrl+C;
it performs a graceful HTTP shutdown. A future installer is expected to add
Windows-service and systemd integration, but neither is implemented yet.

The default operational endpoints are:

- Grid/login service: `127.0.0.1:8002/tcp`
- Viewer grid-discovery URL: `http://127.0.0.1:8002/`
- Liveness: `http://127.0.0.1:8002/ping`
- Dependency-aware readiness: `http://127.0.0.1:8002/ready`
- Version: `http://127.0.0.1:8002/version`

Verify readiness with `curl.exe --fail http://127.0.0.1:8002/ready` on
Windows or `curl --fail http://127.0.0.1:8002/ready` on Linux. Readiness fails
when the configured PostgreSQL database cannot be used.

## Connect region owners

Give each authorized region owner these values over a secure channel:

- The internal grid API URL they can reach.
- The shared region service token.
- Their assigned region name and grid coordinates.
- The grid's policy for region HTTP and viewer UDP ports.

The owner uses them as described in [INSTALL-REGION.md](INSTALL-REGION.md).
Confirm `/ready` is healthy before their first region startup. The initial
defaults use `42001/tcp` and `42002/udp` for the first region, but these ports
belong on the region host, not necessarily the grid host.

## Network exposure

For a remote grid, bind only to the intended interface, expose only its TCP
port, and use narrowly scoped firewall rules. Production Internet exposure
should place TLS termination and normal access controls in front of the HTTP
service. Do not expose PostgreSQL publicly merely to support regions; regions
communicate with the authenticated grid HTTP API.

## Upgrades and backups

Before an upgrade:

1. Stop the grid cleanly and notify region operators.
2. Back up PostgreSQL with the database administrator's standard tools.
3. Back up `config/grid.ini` and `config/db.ini` securely.
4. Keep the previous program package available for rollback.

Extract the new package separately, copy in the private configuration files,
then run `bootstrap-grid -migrations-only`. Start the new grid and verify
`/version` and `/ready`. A migration may make a binary-only rollback unsafe, so
use the database backup and release notes when rollback is required.

## Optional Library login

The `HomeWorldz Library` identity is normally locked. A grid operator who needs
to curate Library content can assign it a local password with
`configure-library`. This is not required for users or region owners.

Building development snapshots from source is documented in the repository
README and is intentionally outside this operator installation guide.
