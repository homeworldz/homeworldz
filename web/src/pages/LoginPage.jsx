import { createSignal, Show } from "solid-js";
import { A, useNavigate } from "@solidjs/router";
import { ApiError, createToken } from "../lib/api";
import { login } from "../lib/auth";
import { PasswordField } from "../components/PasswordField";

export function LoginPage() {
  const navigate = useNavigate();
  const [userid, setUserid] = createSignal("");
  const [password, setPassword] = createSignal("");
  const [error, setError] = createSignal(null);
  const [submitting, setSubmitting] = createSignal(false);

  const submit = async (event) => {
    event.preventDefault();
    setError(null);
    setSubmitting(true);
    try {
      const token = await createToken(userid().trim(), password());
      login(token);
      navigate("/account", { replace: true });
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        setError("Incorrect name/login ID or password.");
      } else if (err instanceof ApiError && err.status === 429) {
        setError("Too many attempts. Please wait a moment and try again.");
      } else if (err instanceof ApiError) {
        setError(err.message);
      } else {
        setError("We couldn't reach the login service. Please try again.");
      }
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <section class="auth-page" aria-labelledby="login-title">
      <form class="auth-card" onSubmit={submit} novalidate>
        <h1 id="login-title">Login</h1>

        <div class="field">
          <label for="userid">Avatar name or login ID</label>
          <input
            id="userid"
            name="userid"
            type="text"
            autocomplete="username"
            value={userid()}
            onInput={(event) => setUserid(event.currentTarget.value)}
            required
          />
        </div>

        <div class="field">
          <label for="password">Password</label>
          <PasswordField
            id="password"
            name="password"
            autocomplete="current-password"
            value={password()}
            onInput={(event) => setPassword(event.currentTarget.value)}
            required
          />
        </div>

        <Show when={error()}>
          <p class="form-error" role="alert">
            {error()}
          </p>
        </Show>

        <div class="auth-actions">
          <button type="submit" disabled={submitting()}>
            {submitting() ? "Logging in…" : "Login"}
          </button>
        </div>

        <p class="auth-alt">
          Need an avatar? <A href="/register">Register</A>.
        </p>
      </form>
    </section>
  );
}
