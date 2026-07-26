import { createSignal, For, onMount, Show } from "solid-js";
import { A, useNavigate, useParams } from "@solidjs/router";
import {
  ApiError,
  adminUpdateUser,
  banUser,
  getUser,
  replacePrivileges,
  setUserTags,
  unbanUser,
} from "../lib/api";
import { currentIdentity, logout } from "../lib/auth";
import {
  NAMED_PRIVILEGES,
  effectivePrivileges,
  hasPrivilege,
  isSuper,
  privilegeList,
} from "../lib/privileges";
import { USER_KINDS, KNOWN_USER_TAGS } from "../lib/classification";
import { TagsEditor } from "../components/TagsEditor";

export function AdminUserPage() {
  const params = useParams();
  const navigate = useNavigate();

  const actorPrivs = () => currentIdentity()?.privs ?? "";
  const canEditUsers = () => hasPrivilege(actorPrivs(), "users");
  const canEditPrivs = () => hasPrivilege(actorPrivs(), "admin");
  const canBan = () => hasPrivilege(actorPrivs(), "bans");
  const actorIsSuper = () => isSuper(actorPrivs());

  const [user, setUser] = createSignal(null);
  const [loadError, setLoadError] = createSignal(null);

  const [displayName, setDisplayName] = createSignal("");
  const [profileErr, setProfileErr] = createSignal(null);
  const [profileMsg, setProfileMsg] = createSignal(null);
  const [savingProfile, setSavingProfile] = createSignal(false);

  const [privSet, setPrivSet] = createSignal([]);
  const [privErr, setPrivErr] = createSignal(null);
  const [privMsg, setPrivMsg] = createSignal(null);
  const [savingPrivs, setSavingPrivs] = createSignal(false);

  const [reason, setReason] = createSignal("");
  const [expiresAt, setExpiresAt] = createSignal("");
  const [banErr, setBanErr] = createSignal(null);
  const [banBusy, setBanBusy] = createSignal(false);

  const handleAuthError = (err) => {
    if (err instanceof ApiError && err.status === 401) {
      logout();
      navigate("/login", { replace: true });
      return true;
    }
    return false;
  };

  // applyUser stores the fetched account and resets the editable fields to it.
  const applyUser = (fresh) => {
    setUser(fresh);
    setDisplayName(fresh.displayName);
    setPrivSet(privilegeList(fresh.privs));
  };

  onMount(async () => {
    try {
      applyUser(await getUser(params.id));
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      if (err instanceof ApiError && err.status === 403) {
        setLoadError("You don't have permission to view this account.");
      } else if (err instanceof ApiError && err.status === 404) {
        setLoadError("That account no longer exists.");
      } else {
        setLoadError("We couldn't load this account.");
      }
    }
  });

  const saveProfile = async (event) => {
    event.preventDefault();
    setProfileErr(null);
    setProfileMsg(null);
    const trimmed = displayName().trim();
    if (!trimmed) {
      setProfileErr("Display name cannot be empty.");
      return;
    }
    setSavingProfile(true);
    try {
      applyUser(await adminUpdateUser(params.id, { displayName: trimmed }));
      setProfileMsg("Profile updated.");
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      if (err instanceof ApiError && err.status === 409) {
        setProfileErr("That display name is already taken.");
      } else {
        setProfileErr(
          err instanceof ApiError ? err.message : "Update failed.",
        );
      }
    } finally {
      setSavingProfile(false);
    }
  };

  const togglePriv = (name, checked) => {
    setPrivSet((current) =>
      checked
        ? [...current, name]
        : current.filter((priv) => priv !== name),
    );
  };

  const savePrivs = async (event) => {
    event.preventDefault();
    setPrivErr(null);
    setPrivMsg(null);
    setSavingPrivs(true);
    try {
      applyUser(await replacePrivileges(params.id, privSet().join(",")));
      setPrivMsg("Privileges updated.");
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      if (err instanceof ApiError && err.status === 403) {
        setPrivErr("Only a super account may grant or revoke super.");
      } else if (err instanceof ApiError && err.status === 409) {
        setPrivErr("The final super account cannot be demoted.");
      } else {
        setPrivErr(
          err instanceof ApiError ? err.message : "Update failed.",
        );
      }
    } finally {
      setSavingPrivs(false);
    }
  };

  const submitBan = async (event) => {
    event.preventDefault();
    setBanErr(null);
    const trimmed = reason().trim();
    if (!trimmed) {
      setBanErr("A reason is required.");
      return;
    }
    setBanBusy(true);
    try {
      const iso = expiresAt()
        ? new Date(expiresAt()).toISOString()
        : undefined;
      applyUser(await banUser(params.id, trimmed, iso));
      setReason("");
      setExpiresAt("");
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      setBanErr(err instanceof ApiError ? err.message : "Ban failed.");
    } finally {
      setBanBusy(false);
    }
  };

  const removeBan = async () => {
    setBanErr(null);
    setBanBusy(true);
    try {
      applyUser(await unbanUser(params.id));
    } catch (err) {
      if (handleAuthError(err)) {
        return;
      }
      setBanErr(err instanceof ApiError ? err.message : "Unban failed.");
    } finally {
      setBanBusy(false);
    }
  };

  return (
    <section class="admin-page" aria-labelledby="admin-user-title">
      <header class="admin-header">
        <A href="/admin/users" class="admin-back">← Users</A>
        <h1 id="admin-user-title">
          {user() ? user().displayName : "Account"}
        </h1>
      </header>

      <Show when={loadError()}>
        <p class="form-error" role="alert">{loadError()}</p>
      </Show>

      <Show when={user()}>
        <div class="auth-card">
          <dl class="account-facts">
            <div>
              <dt>Login ID</dt>
              <dd><code>{user().userid}</code></dd>
            </div>
            <div>
              <dt>Registered</dt>
              <dd>{new Date(user().rezDate).toLocaleDateString()}</dd>
            </div>
            <div>
              <dt>State</dt>
              <dd>
                <span class="admin-badge" data-state={user().state}>
                  {user().state}
                </span>
              </dd>
            </div>
            <div>
              <dt>Kind</dt>
              <dd>
                <span class="admin-badge">{user().kind}</span>
              </dd>
            </div>
            <div>
              <dt>Tags</dt>
              <dd class="admin-privs-cell">{user().tags || "—"}</dd>
            </div>
            <div>
              <dt>Effective privileges</dt>
              <dd class="admin-privs-cell">
                {effectivePrivileges(user().privs).join(", ") || "—"}
              </dd>
            </div>
          </dl>
        </div>

        <Show when={user().ban}>
          <div class="auth-card admin-ban-notice">
            <h2>Banned</h2>
            <p><strong>Reason:</strong> {user().ban.reason}</p>
            <p>
              <strong>Banned:</strong>{" "}
              {new Date(user().ban.bannedAt).toLocaleString()}
            </p>
            <Show when={user().ban.expiresAt}>
              <p>
                <strong>Expires:</strong>{" "}
                {new Date(user().ban.expiresAt).toLocaleString()}
              </p>
            </Show>
          </div>
        </Show>

        <Show when={canEditUsers()}>
          <form class="auth-card" onSubmit={saveProfile} novalidate>
            <h2>Display name</h2>
            <div class="field">
              <label for="admin-displayName">Display name</label>
              <input
                id="admin-displayName"
                type="text"
                value={displayName()}
                onInput={(event) => setDisplayName(event.currentTarget.value)}
                required
              />
              <p class="field-hint">Two words; the login ID is derived from it.</p>
            </div>
            <Show when={profileErr()}>
              <p class="form-error" role="alert">{profileErr()}</p>
            </Show>
            <Show when={profileMsg()}>
              <p class="form-note" role="status">{profileMsg()}</p>
            </Show>
            <div class="auth-actions">
              <button type="submit" disabled={savingProfile()}>
                {savingProfile() ? "Saving…" : "Save"}
              </button>
            </div>
          </form>
        </Show>

        <Show when={canEditUsers()}>
          <TagsEditor
            kindOptions={USER_KINDS}
            knownTags={KNOWN_USER_TAGS}
            kind={user().kind}
            tags={user().tags}
            onSave={async (kind, tags) =>
              applyUser(await setUserTags(params.id, kind, tags))
            }
          />
        </Show>

        <Show when={canEditPrivs()}>
          <form class="auth-card" onSubmit={savePrivs} novalidate>
            <h2>Privileges</h2>
            <div class="admin-priv-grid">
              <For each={NAMED_PRIVILEGES}>
                {(name) => {
                  const superLocked = () => name === "super" && !actorIsSuper();
                  return (
                    <label class="admin-priv-option">
                      <input
                        type="checkbox"
                        checked={privSet().includes(name)}
                        disabled={superLocked()}
                        onChange={(event) =>
                          togglePriv(name, event.currentTarget.checked)
                        }
                      />
                      <span>{name}</span>
                    </label>
                  );
                }}
              </For>
            </div>
            <p class="field-hint">
              Effective: {effectivePrivileges(privSet().join(",")).join(", ") || "none"}
            </p>
            <Show when={privErr()}>
              <p class="form-error" role="alert">{privErr()}</p>
            </Show>
            <Show when={privMsg()}>
              <p class="form-note" role="status">{privMsg()}</p>
            </Show>
            <div class="auth-actions">
              <button type="submit" disabled={savingPrivs()}>
                {savingPrivs() ? "Saving…" : "Save privileges"}
              </button>
            </div>
          </form>
        </Show>

        <Show when={canBan()}>
          <Show
            when={user().state === "banned"}
            fallback={
              <form class="auth-card" onSubmit={submitBan} novalidate>
                <h2>Ban account</h2>
                <div class="field">
                  <label for="admin-ban-reason">Reason</label>
                  <input
                    id="admin-ban-reason"
                    type="text"
                    value={reason()}
                    onInput={(event) => setReason(event.currentTarget.value)}
                    required
                  />
                </div>
                <div class="field">
                  <label for="admin-ban-expires">Expires (optional)</label>
                  <input
                    id="admin-ban-expires"
                    type="datetime-local"
                    value={expiresAt()}
                    onInput={(event) => setExpiresAt(event.currentTarget.value)}
                  />
                  <p class="field-hint">Leave blank for a permanent ban.</p>
                </div>
                <Show when={banErr()}>
                  <p class="form-error" role="alert">{banErr()}</p>
                </Show>
                <div class="auth-actions">
                  <button type="submit" data-variant="danger" disabled={banBusy()}>
                    {banBusy() ? "Working…" : "Ban account"}
                  </button>
                </div>
              </form>
            }
          >
            <div class="auth-card">
              <h2>Ban</h2>
              <Show when={banErr()}>
                <p class="form-error" role="alert">{banErr()}</p>
              </Show>
              <div class="auth-actions">
                <button
                  type="button"
                  data-variant="outline"
                  disabled={banBusy()}
                  onClick={removeBan}
                >
                  {banBusy() ? "Working…" : "Lift ban"}
                </button>
              </div>
            </div>
          </Show>
        </Show>
      </Show>
    </section>
  );
}
