import { createSignal, For, onMount, Show } from "solid-js";
import { A, useNavigate } from "@solidjs/router";
import { ApiError, listUsers } from "../lib/api";
import { logout } from "../lib/auth";

export function AdminUsersPage() {
  const navigate = useNavigate();
  const [users, setUsers] = createSignal([]);
  const [search, setSearch] = createSignal("");
  const [cursor, setCursor] = createSignal("");
  const [loading, setLoading] = createSignal(false);
  const [error, setError] = createSignal(null);
  const [loaded, setLoaded] = createSignal(false);

  const handleError = (err) => {
    if (err instanceof ApiError && err.status === 401) {
      logout();
      navigate("/login", { replace: true });
      return;
    }
    if (err instanceof ApiError && err.status === 403) {
      setError("You don't have permission to view accounts.");
      return;
    }
    setError(
      err instanceof ApiError ? err.message : "We couldn't load accounts.",
    );
  };

  // append=false starts a fresh search; append=true continues via the cursor.
  const load = async (append) => {
    setError(null);
    setLoading(true);
    try {
      const page = await listUsers({
        search: search().trim() || undefined,
        cursor: append ? cursor() : undefined,
        limit: 50,
      });
      const rows = page.users ?? [];
      setUsers(append ? [...users(), ...rows] : rows);
      setCursor(page.nextCursor ?? "");
      setLoaded(true);
    } catch (err) {
      handleError(err);
    } finally {
      setLoading(false);
    }
  };

  onMount(() => load(false));

  const submitSearch = (event) => {
    event.preventDefault();
    setCursor("");
    load(false);
  };

  return (
    <section class="admin-page" aria-labelledby="admin-users-title">
      <header class="admin-header">
        <A href="/admin" class="admin-back">← Administration</A>
        <h1 id="admin-users-title">Users</h1>
      </header>

      <form class="admin-search" onSubmit={submitSearch} role="search">
        <input
          type="search"
          aria-label="Search accounts"
          placeholder="Search by name or login ID"
          value={search()}
          onInput={(event) => setSearch(event.currentTarget.value)}
        />
        <button type="submit" disabled={loading()}>
          {loading() ? "Searching…" : "Search"}
        </button>
      </form>

      <Show when={error()}>
        <p class="form-error" role="alert">{error()}</p>
      </Show>

      <Show when={loaded() && !error()}>
        <Show
          when={users().length > 0}
          fallback={<p class="admin-empty">No accounts match that search.</p>}
        >
          <div class="admin-table-scroll">
            <table class="admin-table">
              <thead>
                <tr>
                  <th scope="col">Display name</th>
                  <th scope="col">Login ID</th>
                  <th scope="col">State</th>
                  <th scope="col">Kind / tags</th>
                  <th scope="col">Privileges</th>
                </tr>
              </thead>
              <tbody>
                <For each={users()}>
                  {(user) => (
                    <tr>
                      <td>
                        <A href={`/admin/users/${user.id}`}>
                          {user.displayName}
                        </A>
                      </td>
                      <td><code>{user.userid}</code></td>
                      <td>
                        <span
                          class="admin-badge"
                          data-state={user.state}
                        >
                          {user.state}
                        </span>
                      </td>
                      <td>
                        <span class="admin-badge">{user.kind}</span>
                        <Show when={user.tags}>
                          <span class="admin-privs-cell"> {user.tags}</span>
                        </Show>
                      </td>
                      <td class="admin-privs-cell">{user.privs || "—"}</td>
                    </tr>
                  )}
                </For>
              </tbody>
            </table>
          </div>
        </Show>
      </Show>

      <Show when={cursor()}>
        <div class="auth-actions">
          <button
            type="button"
            data-variant="outline"
            disabled={loading()}
            onClick={() => load(true)}
          >
            {loading() ? "Loading…" : "Load more"}
          </button>
        </div>
      </Show>
    </section>
  );
}
