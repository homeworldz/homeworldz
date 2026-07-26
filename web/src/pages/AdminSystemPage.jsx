import { createSignal, For, onMount, Show } from "solid-js";
import { A, useNavigate } from "@solidjs/router";
import { ApiError, getSystemStatus } from "../lib/api";
import { logout } from "../lib/auth";

export function AdminSystemPage() {
  const navigate = useNavigate();
  const [status, setStatus] = createSignal(null);
  const [error, setError] = createSignal(null);

  onMount(async () => {
    try {
      setStatus(await getSystemStatus());
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        logout();
        navigate("/login", { replace: true });
        return;
      }
      if (err instanceof ApiError && err.status === 403) {
        setError("You don't have permission to view system status.");
        return;
      }
      setError("We couldn't load system status.");
    }
  });

  const stat = (label, value) => (
    <div class="admin-stat">
      <span class="admin-stat-value">{value}</span>
      <span class="admin-stat-label">{label}</span>
    </div>
  );

  return (
    <section class="admin-page" aria-labelledby="admin-system-title">
      <header class="admin-header">
        <A href="/admin" class="admin-back">← Administration</A>
        <h1 id="admin-system-title">System</h1>
      </header>

      <Show when={error()}>
        <p class="form-error" role="alert">{error()}</p>
      </Show>

      <Show when={status()}>
        <div class="admin-stats">
          {stat("Regions", status().regions.total)}
          {stat("Online", status().regions.online)}
          {stat("Offline", status().regions.offline)}
          {stat("Undeployed", status().regions.undeployed)}
          {stat("Active sessions", status().sessions.total)}
        </div>

        <div class="auth-card">
          <h2>Regions</h2>
          <Show
            when={status().regionStatus.length > 0}
            fallback={<p class="admin-empty">No regions provisioned.</p>}
          >
            <div class="admin-table-scroll">
              <table class="admin-table">
                <thead>
                  <tr>
                    <th scope="col">Name</th>
                    <th scope="col">Kind</th>
                    <th scope="col">State</th>
                    <th scope="col">Sessions</th>
                    <th scope="col">Lease expires</th>
                  </tr>
                </thead>
                <tbody>
                  <For each={status().regionStatus}>
                    {(region) => (
                      <tr>
                        <td>
                          <A href={`/admin/regions/${region.id}`}>{region.name}</A>
                        </td>
                        <td>
                          <span class="admin-badge">{region.kind}</span>
                        </td>
                        <td>
                          <span class="admin-badge" data-state={region.state}>
                            {region.state}
                          </span>
                        </td>
                        <td>{region.sessions}</td>
                        <td>
                          {region.leaseExpiresAt
                            ? new Date(region.leaseExpiresAt).toLocaleString()
                            : "—"}
                        </td>
                      </tr>
                    )}
                  </For>
                </tbody>
              </table>
            </div>
          </Show>
        </div>

        <p class="field-hint">
          Maintenance mode and broadcast-to-logged-in-users are planned system
          operations and are not yet available.
        </p>
      </Show>
    </section>
  );
}
