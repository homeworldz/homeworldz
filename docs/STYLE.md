# Documentation and Naming Style

## The project name

The name is one word, written **`Homeworldz`** in prose — an ordinary proper
noun, capitalized because it begins a sentence or because it is a name, and not
otherwise. There is no internal capital and no accepted abbreviation.

| Context | Form | Example |
| --- | --- | --- |
| Prose, headings, titles | `Homeworldz` | "Homeworldz is a virtual world server." |
| Wordmark, logo, domain | lowercase | `homeworldz.com` |
| Namespaces, binaries, roles, hostnames, package names | lowercase | `homeworldz::physics::World`, `homeworldz-region`, the `homeworldz` database role |
| Service identities | lowercase, dotted | `homeworldz.library` |
| Environment variables and macros | upper snake | `HOMEWORLDZ_TEST_DATABASE_URL` |

**`HomeWorldz` is not used.** The camel form invites an abbreviation the project
does not want and adds a distinction prose does not need: the logo already
separates the two halves of the name with color, and prose does not have to
reproduce that.

Identifiers follow their language's own conventions and are lowercase for
ordinary reasons, not as a stylization of the name. They are not user-facing and
need no coordination with the prose form.

## The client

The program a user runs is just **Homeworldz**, optionally with its platform —
"run Homeworldz", "Homeworldz for Windows", "Homeworldz in the browser". There
is no category noun for it.

Where the distinction from server software matters, it is the **Homeworldz
client**. See [ADR 0030](adr/0030-client-architecture.md).

Two words are reserved and should not be used loosely:

- **viewer** means a third-party Second Life-lineage viewer — Firestorm and its
  peers — which [ADR 0016](adr/0016-firestorm-compatibility-target.md) targets
  for compatibility. It never means the Homeworldz client.
- **frontend** means a rendering layer over the engine-neutral client core, in
  the sense ADR 0030 defines: the Godot, native WebGPU, and browser frontends.

## Line endings

Markdown and plain text files use native line endings — CRLF on Windows.
