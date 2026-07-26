import { createSignal, For, onMount, Show } from "solid-js";
import { A, useNavigate } from "@solidjs/router";
import { ApiError, createRegion, listRegions } from "../lib/api";
import { currentIdentity, logout } from "../lib/auth";
import { hasPrivilege } from "../lib/privileges";
import { UserPicker } from "../components/UserPicker";
import { REGION_KINDS, REGION_SIZES, regionSizeLabel } from "../lib/classification";

export function AdminRegionsPage() {
  const navigate = useNavigate();
  const canDeploy = () => hasPrivilege(currentIdentity()?.privs ?? "", "deploy");

  const [regions, setRegions] = createSignal([]);
  const [loaded, setLoaded] = createSignal(false);
  const [error, setError] = createSignal(null);

  const [name, setName] = createSignal("");
  const [ownerUserId, setOwnerUserId] = createSignal("");
  const [ownerResetKey, setOwnerResetKey] = createSignal(0);
  const [gridX, setGridX] = createSignal("");
  const [gridY, setGridY] = createSignal("");
  const [publicEndpoint, setPublicEndpoint] = createSignal("");
  const [viewerPort, setViewerPort] = createSignal("42002");
  const [kind, setKind] = createSignal("user");
  const [size, setSize] = createSignal(1);
  const [tags, setTags] = createSignal("");
  const [createErr, setCreateErr] = createSignal(null);
  const [creating, setCreating] = createSignal(false);
  const [accessKey, setAccessKey] = createSignal(null);

  const handleAuthError = (err) => {
    if (err instanceof ApiError && err.status === 401) {
      logout();
      navigate("/login", { replace: true });
      return true;
    }
    return false;
  };

  const load = async () => {
    setError(null);
    try {
      const page = await listRegions();
      setRegions(page.regions ?? []);
      setLoaded(true);
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      if (err instanceof ApiError && err.status === 403) {
        setError("You don't have permission to view regions.");
      } else {
        setError(err instanceof ApiError ? err.message : "We couldn't load regions.");
      }
    }
  };

  onMount(load);

  const submitCreate = async (event) => {
    event.preventDefault();
    setCreateErr(null);
    setAccessKey(null);
    const x = Number(gridX());
    const y = Number(gridY());
    const port = Number(viewerPort());
    if (!name().trim() || !ownerUserId().trim() || !publicEndpoint().trim()) {
      setCreateErr("Name, owner, and public endpoint are required.");
      return;
    }
    if (!Number.isInteger(x) || !Number.isInteger(y) || x < 0 || y < 0) {
      setCreateErr("Grid X and Y must be non-negative integers.");
      return;
    }
    setCreating(true);
    try {
      const deployment = await createRegion({
        name: name().trim(),
        ownerUserId: ownerUserId().trim(),
        gridX: x,
        gridY: y,
        publicEndpoint: publicEndpoint().trim(),
        viewerPort: port,
        size: size(),
        kind: kind(),
        tags: tags().trim(),
      });
      setAccessKey(deployment.accessKey);
      setName("");
      setOwnerUserId("");
      setOwnerResetKey((key) => key + 1);
      setGridX("");
      setGridY("");
      setPublicEndpoint("");
      setViewerPort("42002");
      setKind("user");
      setSize(1);
      setTags("");
      await load();
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      if (err instanceof ApiError && err.status === 409) {
        setCreateErr("A region already occupies that name or map position.");
      } else {
        setCreateErr(err instanceof ApiError ? err.message : "Creation failed.");
      }
    } finally {
      setCreating(false);
    }
  };

  return (
    <section class="admin-page" aria-labelledby="admin-regions-title">
      <header class="admin-header">
        <A href="/admin" class="admin-back">← Administration</A>
        <h1 id="admin-regions-title">Regions</h1>
      </header>

      <Show when={error()}>
        <p class="form-error" role="alert">{error()}</p>
      </Show>

      <Show when={loaded() && !error()}>
        <Show
          when={regions().length > 0}
          fallback={<p class="admin-empty">No regions have been provisioned.</p>}
        >
          <div class="admin-table-scroll">
            <table class="admin-table">
              <thead>
                <tr>
                  <th scope="col">Name</th>
                  <th scope="col">Map</th>
                  <th scope="col">Size</th>
                  <th scope="col">Kind / tags</th>
                  <th scope="col">Endpoint</th>
                  <th scope="col">State</th>
                </tr>
              </thead>
              <tbody>
                <For each={regions()}>
                  {(region) => (
                    <tr>
                      <td>
                        <A href={`/admin/regions/${region.id}`}>{region.name}</A>
                      </td>
                      <td>
                        {region.gridX != null && region.gridY != null
                          ? `${region.gridX}, ${region.gridY}`
                          : "—"}
                      </td>
                      <td>{regionSizeLabel(region.size)}</td>
                      <td>
                        <span class="admin-badge">{region.kind}</span>
                        <Show when={region.tags}>
                          <span class="admin-privs-cell"> {region.tags}</span>
                        </Show>
                      </td>
                      <td>
                        {region.publicEndpoint
                          ? <code>{region.publicEndpoint}</code>
                          : "—"}
                      </td>
                      <td>
                        <span class="admin-badge" data-state={region.state}>
                          {region.state}
                        </span>
                      </td>
                    </tr>
                  )}
                </For>
              </tbody>
            </table>
          </div>
        </Show>
      </Show>

      <Show when={canDeploy()}>
        <form class="auth-card" onSubmit={submitCreate} novalidate>
          <h2>Provision a region</h2>
          <div class="field">
            <label for="region-name">Name</label>
            <input
              id="region-name"
              type="text"
              value={name()}
              onInput={(event) => setName(event.currentTarget.value)}
              required
            />
          </div>
          <div class="field">
            <label for="region-owner">Owner</label>
            <UserPicker
              id="region-owner"
              resetKey={ownerResetKey()}
              onSelect={(user) => setOwnerUserId(user ? user.id : "")}
            />
          </div>
          <div class="admin-field-row">
            <div class="field">
              <label for="region-kind">Kind</label>
              <select
                id="region-kind"
                value={kind()}
                onChange={(event) => setKind(event.currentTarget.value)}
              >
                <For each={REGION_KINDS}>
                  {(option) => <option value={option}>{option}</option>}
                </For>
              </select>
            </div>
            <div class="field">
              <label for="region-size">Size (square, edge length)</label>
              <select
                id="region-size"
                value={String(size())}
                onChange={(event) => setSize(Number(event.currentTarget.value))}
              >
                <For each={REGION_SIZES}>
                  {(option) => (
                    <option value={String(option.units)}>{option.edge} m</option>
                  )}
                </For>
              </select>
            </div>
          </div>
          <div class="admin-field-row">
            <div class="field">
              <label for="region-x">Grid X</label>
              <input
                id="region-x"
                type="number"
                min="0"
                value={gridX()}
                onInput={(event) => setGridX(event.currentTarget.value)}
                required
              />
            </div>
            <div class="field">
              <label for="region-y">Grid Y</label>
              <input
                id="region-y"
                type="number"
                min="0"
                value={gridY()}
                onInput={(event) => setGridY(event.currentTarget.value)}
                required
              />
            </div>
            <div class="field">
              <label for="region-port">Viewer port</label>
              <input
                id="region-port"
                type="number"
                min="1"
                max="65535"
                value={viewerPort()}
                onInput={(event) => setViewerPort(event.currentTarget.value)}
              />
            </div>
          </div>
          <div class="field">
            <label for="region-endpoint">Public endpoint</label>
            <input
              id="region-endpoint"
              type="url"
              placeholder="https://region.example.com"
              value={publicEndpoint()}
              onInput={(event) => setPublicEndpoint(event.currentTarget.value)}
              required
            />
          </div>
          <div class="field">
            <label for="region-tags">Tags (comma-separated, optional)</label>
            <input
              id="region-tags"
              type="text"
              placeholder="e.g. ocean"
              value={tags()}
              onInput={(event) => setTags(event.currentTarget.value)}
            />
          </div>
          <Show when={createErr()}>
            <p class="form-error" role="alert">{createErr()}</p>
          </Show>
          <div class="auth-actions">
            <button type="submit" disabled={creating()}>
              {creating() ? "Provisioning…" : "Provision & deploy"}
            </button>
          </div>
        </form>
      </Show>

      <Show when={accessKey()}>
        <div class="auth-card admin-accesskey">
          <h2>Region access key</h2>
          <p class="field-hint">
            Shown once. Transfer it securely to the region operator — it cannot
            be retrieved again.
          </p>
          <code class="admin-accesskey-value">{accessKey()}</code>
        </div>
      </Show>
    </section>
  );
}
