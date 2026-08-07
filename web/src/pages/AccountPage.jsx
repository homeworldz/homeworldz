import { createSignal, onMount, Show } from "solid-js";
import { useNavigate } from "@solidjs/router";
import { ApiError, changePassword, getAccount, updateProfile } from "../lib/api";
import { currentIdentity, logout } from "../lib/auth";
import { PasswordField } from "../components/PasswordField";

export function AccountPage() {
  const navigate = useNavigate();
  const [identity, setIdentity] = createSignal(currentIdentity());
  const [loadError, setLoadError] = createSignal(null);

  const [displayName, setDisplayName] = createSignal(
    currentIdentity()?.displayName ?? "",
  );
  const [profileMsg, setProfileMsg] = createSignal(null);
  const [profileErr, setProfileErr] = createSignal(null);
  const [savingProfile, setSavingProfile] = createSignal(false);

  const [currentPassword, setCurrentPassword] = createSignal("");
  const [newPassword, setNewPassword] = createSignal("");
  const [revealPw, setRevealPw] = createSignal(false);
  const [pwErr, setPwErr] = createSignal(null);
  const [changingPw, setChangingPw] = createSignal(false);

  onMount(async () => {
    try {
      const fresh = await getAccount();
      setIdentity(fresh);
      setDisplayName(fresh.displayName);
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        logout();
        navigate("/login", { replace: true });
        return;
      }
      setLoadError("We couldn't load your account. Please try again.");
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
      const updated = await updateProfile({ displayName: trimmed });
      setIdentity(updated);
      setProfileMsg("Profile updated.");
    } catch (err) {
      if (err instanceof ApiError && err.status === 409) {
        setProfileErr("That display name is already taken. Please choose another.");
      } else {
        setProfileErr(
          err instanceof ApiError ? err.message : "Update failed. Please try again.",
        );
      }
    } finally {
      setSavingProfile(false);
    }
  };

  const submitPassword = async (event) => {
    event.preventDefault();
    setPwErr(null);
    if (newPassword().length < 8) {
      setPwErr("New password must be at least 8 characters.");
      return;
    }
    setChangingPw(true);
    try {
      await changePassword(currentPassword(), newPassword());
      // Changing the password invalidates the current token; re-authenticate.
      logout();
      navigate("/login", { replace: true });
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        setPwErr("Current password is incorrect.");
      } else {
        setPwErr(
          err instanceof ApiError ? err.message : "Change failed. Please try again.",
        );
      }
    } finally {
      setChangingPw(false);
    }
  };

  const doLogout = () => {
    logout();
    navigate("/", { replace: true });
  };

  return (
    <section class="auth-page account-page" aria-labelledby="account-title">
      <div class="auth-card">
        <h1 id="account-title">Your account</h1>
        <Show when={loadError()}>
          <p class="form-error" role="alert">
            {loadError()}
          </p>
        </Show>
        <Show when={identity()}>
          <dl class="account-facts">
            <div>
              <dt>Login ID</dt>
              <dd>
                <code>{identity().userid}</code>
              </dd>
            </div>
            <div>
              <dt>Display name</dt>
              <dd>{identity().displayName}</dd>
            </div>
            <div>
              <dt>Email</dt>
              <dd>
                <Show when={identity().email} fallback={<em>none on file</em>}>
                  {identity().email}
                </Show>
              </dd>
            </div>
            <div>
              <dt>Registered</dt>
              <dd>{new Date(identity().rezDate).toLocaleDateString()}</dd>
            </div>
            <Show when={identity().privs}>
              <div>
                <dt>Privileges</dt>
                <dd>{identity().privs}</dd>
              </div>
            </Show>
          </dl>
        </Show>
      </div>

      <form class="auth-card" onSubmit={saveProfile} novalidate>
        <h2>Display name</h2>
        <div class="field">
          <label for="account-displayName">Display name</label>
          <input
            id="account-displayName"
            type="text"
            value={displayName()}
            onInput={(event) => setDisplayName(event.currentTarget.value)}
            required
          />
        </div>
        <Show when={profileErr()}>
          <p class="form-error" role="alert">
            {profileErr()}
          </p>
        </Show>
        <Show when={profileMsg()}>
          <p class="form-note" role="status">
            {profileMsg()}
          </p>
        </Show>
        <div class="auth-actions">
          <button type="submit" disabled={savingProfile()}>
            {savingProfile() ? "Saving…" : "Save"}
          </button>
        </div>
      </form>

      <form class="auth-card" onSubmit={submitPassword} novalidate>
        <h2>Change password</h2>
        <div class="field">
          <label for="currentPassword">Current password</label>
          <PasswordField
            id="currentPassword"
            name="currentPassword"
            autocomplete="current-password"
            value={currentPassword()}
            onInput={(event) => setCurrentPassword(event.currentTarget.value)}
            visible={revealPw()}
            onToggle={() => setRevealPw((shown) => !shown)}
            required
          />
        </div>
        <div class="field">
          <label for="newPassword">New password</label>
          <PasswordField
            id="newPassword"
            name="newPassword"
            autocomplete="new-password"
            value={newPassword()}
            onInput={(event) => setNewPassword(event.currentTarget.value)}
            visible={revealPw()}
            onToggle={() => setRevealPw((shown) => !shown)}
            required
          />
          <p class="field-hint">
            At least 8 characters. You'll login again afterward.
          </p>
        </div>
        <Show when={pwErr()}>
          <p class="form-error" role="alert">
            {pwErr()}
          </p>
        </Show>
        <div class="auth-actions">
          <button type="submit" disabled={changingPw()}>
            {changingPw() ? "Updating…" : "Change password"}
          </button>
        </div>
      </form>

      <div class="auth-actions">
        <button type="button" data-variant="outline" onClick={doLogout}>
          Logout
        </button>
      </div>
    </section>
  );
}
