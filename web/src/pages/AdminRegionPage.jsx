import { createSignal, onMount, Show } from "solid-js";
import { A, useNavigate, useParams } from "@solidjs/router";
import {
  ApiError,
  deployRegion,
  getRegion,
  moveRegion,
  setRegionTags,
  undeployRegion,
  updateRegion,
} from "../lib/api";
import { currentIdentity, logout } from "../lib/auth";
import { hasPrivilege } from "../lib/privileges";
import { REGION_KINDS, KNOWN_REGION_TAGS, regionSizeLabel } from "../lib/classification";
import { TagsEditor } from "../components/TagsEditor";

export function AdminRegionPage() {
  const params = useParams();
  const navigate = useNavigate();

  const privs = () => currentIdentity()?.privs ?? "";
  const canEdit = () => hasPrivilege(privs(), "regions");
  const canMap = () => hasPrivilege(privs(), "map");
  const canDeploy = () => hasPrivilege(privs(), "deploy");
  const canUndeploy = () => hasPrivilege(privs(), "undeploy");

  const [region, setRegion] = createSignal(null);
  const [loadError, setLoadError] = createSignal(null);

  const [name, setName] = createSignal("");
  const [ownerUserId, setOwnerUserId] = createSignal("");
  const [publicEndpoint, setPublicEndpoint] = createSignal("");
  const [viewerPort, setViewerPort] = createSignal("");
  const [metaErr, setMetaErr] = createSignal(null);
  const [metaMsg, setMetaMsg] = createSignal(null);
  const [savingMeta, setSavingMeta] = createSignal(false);

  const [gridX, setGridX] = createSignal("");
  const [gridY, setGridY] = createSignal("");
  const [mapErr, setMapErr] = createSignal(null);
  const [mapMsg, setMapMsg] = createSignal(null);
  const [savingMap, setSavingMap] = createSignal(false);

  const [deployErr, setDeployErr] = createSignal(null);
  const [deployBusy, setDeployBusy] = createSignal(false);
  const [accessKey, setAccessKey] = createSignal(null);

  const handleAuthError = (err) => {
    if (err instanceof ApiError && err.status === 401) {
      logout();
      navigate("/login", { replace: true });
      return true;
    }
    return false;
  };

  const applyRegion = (fresh) => {
    setRegion(fresh);
    setName(fresh.name);
    setOwnerUserId(fresh.ownerUserId);
    setPublicEndpoint(fresh.publicEndpoint);
    setViewerPort(String(fresh.viewerPort));
    setGridX(fresh.gridX != null ? String(fresh.gridX) : "");
    setGridY(fresh.gridY != null ? String(fresh.gridY) : "");
  };

  onMount(async () => {
    try {
      applyRegion(await getRegion(params.id));
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      if (err instanceof ApiError && err.status === 403) {
        setLoadError("You don't have permission to view this region.");
      } else if (err instanceof ApiError && err.status === 404) {
        setLoadError("That region no longer exists.");
      } else {
        setLoadError("We couldn't load this region.");
      }
    }
  });

  const saveMeta = async (event) => {
    event.preventDefault();
    setMetaErr(null);
    setMetaMsg(null);
    if (!name().trim() || !ownerUserId().trim() || !publicEndpoint().trim()) {
      setMetaErr("Name, owner, and public endpoint are required.");
      return;
    }
    const port = Number(viewerPort());
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      setMetaErr("Viewer port must be between 1 and 65535.");
      return;
    }
    setSavingMeta(true);
    try {
      applyRegion(
        await updateRegion(params.id, {
          name: name().trim(),
          ownerUserId: ownerUserId().trim(),
          publicEndpoint: publicEndpoint().trim(),
          viewerPort: port,
        }),
      );
      setMetaMsg("Region updated.");
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      if (err instanceof ApiError && err.status === 409) {
        setMetaErr("That name conflicts with another region.");
      } else {
        setMetaErr(err instanceof ApiError ? err.message : "Update failed.");
      }
    } finally {
      setSavingMeta(false);
    }
  };

  const saveMap = async (event) => {
    event.preventDefault();
    setMapErr(null);
    setMapMsg(null);
    const x = Number(gridX());
    const y = Number(gridY());
    if (!Number.isInteger(x) || !Number.isInteger(y) || x < 0 || y < 0) {
      setMapErr("Grid X and Y must be non-negative integers.");
      return;
    }
    setSavingMap(true);
    try {
      applyRegion(await moveRegion(params.id, x, y));
      setMapMsg("Region moved.");
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      if (err instanceof ApiError && err.status === 409) {
        setMapErr("Another region already occupies that position.");
      } else {
        setMapErr(err instanceof ApiError ? err.message : "Move failed.");
      }
    } finally {
      setSavingMap(false);
    }
  };

  const doDeploy = async () => {
    setDeployErr(null);
    setAccessKey(null);
    setDeployBusy(true);
    try {
      const deployment = await deployRegion(params.id);
      applyRegion(deployment.region);
      setAccessKey(deployment.accessKey);
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      setDeployErr(err instanceof ApiError ? err.message : "Deploy failed.");
    } finally {
      setDeployBusy(false);
    }
  };

  const doUndeploy = async () => {
    setDeployErr(null);
    setAccessKey(null);
    setDeployBusy(true);
    try {
      applyRegion(await undeployRegion(params.id));
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      setDeployErr(err instanceof ApiError ? err.message : "Undeploy failed.");
    } finally {
      setDeployBusy(false);
    }
  };

  return (
    <section class="admin-page" aria-labelledby="admin-region-title">
      <header class="admin-header">
        <A href="/admin/regions" class="admin-back">← Regions</A>
        <h1 id="admin-region-title">{region() ? region().name : "Region"}</h1>
      </header>

      <Show when={loadError()}>
        <p class="form-error" role="alert">{loadError()}</p>
      </Show>

      <Show when={region()}>
        <div class="auth-card">
          <dl class="account-facts">
            <div>
              <dt>State</dt>
              <dd>
                <span class="admin-badge" data-state={region().state}>
                  {region().state}
                </span>
              </dd>
            </div>
            <div>
              <dt>Map position</dt>
              <dd>
                {region().gridX != null && region().gridY != null
                  ? `${region().gridX}, ${region().gridY}`
                  : "Unassigned"}
              </dd>
            </div>
            <div>
              <dt>Size (square)</dt>
              <dd>{regionSizeLabel(region().size)} per side</dd>
            </div>
            <div>
              <dt>Kind</dt>
              <dd>
                <span class="admin-badge">{region().kind}</span>
              </dd>
            </div>
            <div>
              <dt>Tags</dt>
              <dd class="admin-privs-cell">{region().tags || "—"}</dd>
            </div>
            <div>
              <dt>Enabled</dt>
              <dd>{region().enabled ? "Yes" : "No"}</dd>
            </div>
            <Show when={region().leaseExpiresAt}>
              <div>
                <dt>Lease expires</dt>
                <dd>{new Date(region().leaseExpiresAt).toLocaleString()}</dd>
              </div>
            </Show>
          </dl>
        </div>

        <Show when={canEdit()}>
          <form class="auth-card" onSubmit={saveMeta} novalidate>
            <h2>Metadata</h2>
            <div class="field">
              <label for="region-edit-name">Name</label>
              <input
                id="region-edit-name"
                type="text"
                value={name()}
                onInput={(event) => setName(event.currentTarget.value)}
                required
              />
            </div>
            <div class="field">
              <label for="region-edit-owner">Owner user ID</label>
              <input
                id="region-edit-owner"
                type="text"
                value={ownerUserId()}
                onInput={(event) => setOwnerUserId(event.currentTarget.value)}
                required
              />
            </div>
            <div class="admin-field-row">
              <div class="field">
                <label for="region-edit-endpoint">Public endpoint</label>
                <input
                  id="region-edit-endpoint"
                  type="url"
                  value={publicEndpoint()}
                  onInput={(event) => setPublicEndpoint(event.currentTarget.value)}
                  required
                />
              </div>
              <div class="field">
                <label for="region-edit-port">Viewer port</label>
                <input
                  id="region-edit-port"
                  type="number"
                  min="1"
                  max="65535"
                  value={viewerPort()}
                  onInput={(event) => setViewerPort(event.currentTarget.value)}
                />
              </div>
            </div>
            <Show when={metaErr()}>
              <p class="form-error" role="alert">{metaErr()}</p>
            </Show>
            <Show when={metaMsg()}>
              <p class="form-note" role="status">{metaMsg()}</p>
            </Show>
            <div class="auth-actions">
              <button type="submit" disabled={savingMeta()}>
                {savingMeta() ? "Saving…" : "Save metadata"}
              </button>
            </div>
          </form>
        </Show>

        <Show when={canEdit()}>
          <TagsEditor
            kindOptions={REGION_KINDS}
            knownTags={KNOWN_REGION_TAGS}
            kind={region().kind}
            tags={region().tags}
            onSave={async (kind, tags) =>
              applyRegion(await setRegionTags(params.id, kind, tags))
            }
          />
        </Show>

        <Show when={canMap()}>
          <form class="auth-card" onSubmit={saveMap} novalidate>
            <h2>Map position</h2>
            <div class="admin-field-row">
              <div class="field">
                <label for="region-map-x">Grid X</label>
                <input
                  id="region-map-x"
                  type="number"
                  min="0"
                  value={gridX()}
                  onInput={(event) => setGridX(event.currentTarget.value)}
                  required
                />
              </div>
              <div class="field">
                <label for="region-map-y">Grid Y</label>
                <input
                  id="region-map-y"
                  type="number"
                  min="0"
                  value={gridY()}
                  onInput={(event) => setGridY(event.currentTarget.value)}
                  required
                />
              </div>
            </div>
            <Show when={mapErr()}>
              <p class="form-error" role="alert">{mapErr()}</p>
            </Show>
            <Show when={mapMsg()}>
              <p class="form-note" role="status">{mapMsg()}</p>
            </Show>
            <div class="auth-actions">
              <button type="submit" disabled={savingMap()}>
                {savingMap() ? "Saving…" : "Move region"}
              </button>
            </div>
          </form>
        </Show>

        <Show when={canDeploy() || canUndeploy()}>
          <div class="auth-card">
            <h2>Deployment</h2>
            <Show when={deployErr()}>
              <p class="form-error" role="alert">{deployErr()}</p>
            </Show>
            <div class="auth-actions">
              <Show when={canDeploy() && region().state === "undeployed"}>
                <button type="button" disabled={deployBusy()} onClick={doDeploy}>
                  {deployBusy() ? "Working…" : "Deploy"}
                </button>
              </Show>
              <Show when={canUndeploy() && region().state !== "undeployed"}>
                <button
                  type="button"
                  data-variant="danger"
                  disabled={deployBusy()}
                  onClick={doUndeploy}
                >
                  {deployBusy() ? "Working…" : "Undeploy"}
                </button>
              </Show>
            </div>
          </div>
        </Show>

        <Show when={accessKey()}>
          <div class="auth-card admin-accesskey">
            <h2>New region access key</h2>
            <p class="field-hint">
              Shown once and it invalidates any earlier key. Transfer it
              securely to the region operator.
            </p>
            <code class="admin-accesskey-value">{accessKey()}</code>
          </div>
        </Show>
      </Show>
    </section>
  );
}
