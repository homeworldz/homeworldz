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
      regions.json          # provisioned region identities and access keys
  db/
    migrations/*.up.sql
  deploy/
    linux/
      Caddyfile.grid
      homeworldz-grid.service
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
- On Ubuntu, `ca-certificates`, PostgreSQL, and Caddy for the recommended
  public HTTPS layout.

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

[grid]
name = HomeWorldz

[auth]
service_token = replace-with-a-long-random-secret
```

- `address` is the interface and TCP port on which the grid listens.
- `public_url` is the URL given to viewers and regions. It must be reachable by
  them; use the host's DNS name and HTTPS URL in a public deployment.
- `name` is the grid's user-visible name in viewer grid selection and login.
  Operators can use a distinct name such as `HomeWorldz Local` for a private
  development grid.
- `service_token` authenticates every region. Generate a strong, unique value,
  transmit it privately to region owners, and never commit it to source
  control.

Copy and edit the provisioned-region registry as a second private file:

```cmd
copy config\examples\regions.json config\regions.json
```

Each array row defines one region UUID, user-visible name, map coordinates,
and unique startup access key:

```json
[
  {
    "id": "11111111-1111-4111-8111-111111111111",
    "name": "Welcome",
    "mapX": 1000,
    "mapY": 1000,
    "accessKey": "replace-with-a-unique-random-access-key"
  }
]
```

UUIDs and map coordinates must be unique. The grid refuses to start if this
file is absent or invalid. Protect it like a password file and restart the grid
after changing it.

Loopback port 8002 is appropriate when grid, region, and viewer are all on one
personal computer. It deliberately accepts no remote connections. The cloud
profile binds the grid privately at `127.0.0.1:8002` and advertises
`https://grid.homeworldz.com`. Caddy owns public ports 80 and 443 and forwards
HTTPS requests to that loopback listener.

## Install as an Ubuntu service

The Linux archive includes a Caddy site and hardened systemd unit. The
following layout keeps replaceable program files separate from private
configuration:

```text
/opt/homeworldz/grid/       # extracted package files and executables
/etc/homeworldz/grid/       # grid.ini, db.ini, regions.json
/etc/caddy/Caddyfile        # public HTTPS endpoint
```

As an administrator, create the service identity and directories:

```sh
sudo useradd --system --home /nonexistent --shell /usr/sbin/nologin homeworldz
sudo install -d -o root -g root -m 0755 /opt/homeworldz/grid
sudo install -d -o homeworldz -g homeworldz -m 0700 /etc/homeworldz/grid
```

Extract the contents of the archive's `homeworldz-grid` directory into
`/opt/homeworldz/grid`.

Install the private cloud examples, then edit both secrets and the provisioned
region rows before startup:

```sh
sudo install -o homeworldz -g homeworldz -m 0600 \
  /opt/homeworldz/grid/config/examples/grid-cloud.ini \
  /etc/homeworldz/grid/grid.ini
sudo install -o homeworldz -g homeworldz -m 0600 \
  /opt/homeworldz/grid/config/examples/regions.json \
  /etc/homeworldz/grid/regions.json
sudoedit /etc/homeworldz/grid/grid.ini
sudoedit /etc/homeworldz/grid/regions.json
```

Install the service and Caddy configuration:

```sh
sudo install -o root -g root -m 0644 \
  /opt/homeworldz/grid/deploy/linux/homeworldz-grid.service \
  /etc/systemd/system/homeworldz-grid.service
sudo install -o root -g root -m 0644 \
  /opt/homeworldz/grid/deploy/linux/Caddyfile.grid /etc/caddy/Caddyfile
sudo systemctl daemon-reload
```

The DNS-only A record for `grid.homeworldz.com` must point at the host, and its
firewall must allow inbound TCP 80 and 443 before Caddy can obtain its
certificate. Keep PostgreSQL and port 8002 off the public Internet.

## Create the database

Run the packaged bootstrap interactively.

Windows:

```cmd
bootstrap-grid.exe
```

Linux:

```sh
sudo -u homeworldz ./bootstrap-grid \
  -config-dir /etc/homeworldz/grid -migrations db/migrations
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
sudo systemctl enable --now homeworldz-grid caddy
```

The service reads `grid.ini`, `db.ini`, and `regions.json` from the directory
passed with `-config`; the packaged systemd unit uses `/etc/homeworldz/grid`.
Stop a foreground process with Ctrl+C or the Ubuntu service with
`sudo systemctl stop homeworldz-grid`; both perform a graceful HTTP shutdown.

## Manage provisioned regions

At startup, rows in `regions.json` are inserted into PostgreSQL when their UUID
does not already exist. They are bootstrap seeds, not a recurring source of
truth: API changes and rotated keys are not overwritten by a later restart.
After initial startup, use the authenticated management API instead of editing
seed rows. Use the Grid service token from `grid.ini` as a Bearer token. For
example, create an enabled region with an automatically generated UUID and
access key:

```text
POST /api/v1/provisioned-regions
Authorization: Bearer <grid-service-token>
Content-Type: application/json

{"name":"Sandbox","mapX":1001,"mapY":1000}
```

The create response is the only response that contains the new plaintext
`accessKey`; give that UUID and key to the Region owner. `GET` on the collection
or an individual record never returns a key. `PATCH
/api/v1/provisioned-regions/<uuid>` changes `name`, `ownerUserId`, `mapX`,
`mapY`, `publicEndpoint`, `viewerPort`, or `enabled`. The endpoint fields are an
operator-owned assignment and remain independent of the Region's current live
lease. Set `enabled` to `false` to reject subsequent startup and lease-renewal
authentication without deleting the identity. `POST
/api/v1/provisioned-regions/<uuid>/rotate-access-key` invalidates the old key
and returns its replacement once. `DELETE` permanently removes the provisioned
record. All successful mutations are committed to PostgreSQL.

PostgreSQL retains only the SHA-256 digest of generated 256-bit access keys. A
plaintext key still exists in the operator-private bootstrap file for its seed
rows, so restrict that file to the Grid service account and include it in
protected configuration backups. Development without PostgreSQL uses the same
API against an atomically replaced `regions.json` file.

## Operational endpoints

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
- Their region UUID and its unique access key from `regions.json`.
- Their assigned region name and map coordinates, for reference.
- The transitional shared service token required by other internal region APIs.
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
3. Back up `config/grid.ini`, `config/db.ini`, and `config/regions.json` securely.
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
