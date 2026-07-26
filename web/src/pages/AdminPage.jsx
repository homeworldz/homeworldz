import { Show } from "solid-js";
import { A } from "@solidjs/router";
import { currentIdentity } from "../lib/auth";
import { hasPrivilege, effectivePrivileges } from "../lib/privileges";

export function AdminPage() {
  const privs = () => currentIdentity()?.privs ?? "";
  const canUsers = () => hasPrivilege(privs(), "users");
  const canRegions = () =>
    ["regions", "map", "deploy", "undeploy"].some((priv) =>
      hasPrivilege(privs(), priv),
    );
  const canSystem = () => hasPrivilege(privs(), "system");

  return (
    <section class="admin-page" aria-labelledby="admin-title">
      <header class="admin-header">
        <h1 id="admin-title">Administration</h1>
        <p class="lede">
          Manage avatar accounts and provisioned regions.
        </p>
      </header>

      <div class="admin-cards">
        <Show when={canUsers()}>
          <A href="/admin/users" class="admin-card">
            <h2>Users</h2>
            <p>Search accounts, edit profiles, manage privileges and bans.</p>
          </A>
        </Show>
        <Show when={canRegions()}>
          <A href="/admin/regions" class="admin-card">
            <h2>Regions</h2>
            <p>Provision, move, deploy, and retire regions.</p>
          </A>
        </Show>
        <Show when={canSystem()}>
          <A href="/admin/system" class="admin-card">
            <h2>System</h2>
            <p>Grid and session status. Maintenance and broadcast to follow.</p>
          </A>
        </Show>
      </div>

      <div class="auth-card">
        <h2>Your privileges</h2>
        <p class="admin-privs">
          {effectivePrivileges(privs()).map((priv) => (
            <span class="admin-badge">{priv}</span>
          ))}
        </p>
      </div>
    </section>
  );
}
