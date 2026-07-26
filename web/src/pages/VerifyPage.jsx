import { createSignal, Show } from "solid-js";
import { A, useNavigate, useSearchParams } from "@solidjs/router";
import { ApiError, verify } from "../lib/api";
import { login } from "../lib/auth";
import { PasswordField } from "../components/PasswordField";

export function VerifyPage() {
  const [params] = useSearchParams();
  const navigate = useNavigate();
  const [password, setPassword] = createSignal("");
  const [confirm, setConfirm] = createSignal("");
  const [reveal, setReveal] = createSignal(false);
  const [error, setError] = createSignal(null);
  const [submitting, setSubmitting] = createSignal(false);

  const code = () => params.code;

  const submit = async (event) => {
    event.preventDefault();
    setError(null);
    if (password().length < 8) {
      setError("Password must be at least 8 characters.");
      return;
    }
    if (password() !== confirm()) {
      setError("Passwords do not match.");
      return;
    }

    setSubmitting(true);
    try {
      const token = await verify(code(), password());
      login(token);
      navigate("/account", { replace: true });
    } catch (err) {
      if (err instanceof ApiError && err.status === 400) {
        setError("This verification link is invalid or has expired.");
      } else if (err instanceof ApiError && err.status === 409) {
        setError("This account is already verified. You can login.");
      } else if (err instanceof ApiError && err.status === 429) {
        setError("Too many attempts. Please wait a moment and try again.");
      } else if (err instanceof ApiError) {
        setError(err.message);
      } else {
        setError("We couldn't reach the verification service. Please try again.");
      }
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <section class="auth-page" aria-labelledby="verify-title">
      <Show
        when={code()}
        fallback={
          <div class="auth-card">
            <h1 id="verify-title">Missing verification code</h1>
            <p class="lede">
              This page needs the link from your confirmation email. Open that
              link, or <A href="/register">register again</A>.
            </p>
          </div>
        }
      >
        <form class="auth-card" onSubmit={submit} novalidate>
          <h1 id="verify-title">Set your password</h1>
          <p class="lede">
            Choose a password to finish activating your avatar. You'll use it to
            login here and in-world.
          </p>

          <div class="field">
            <label for="password">Password</label>
            <PasswordField
              id="password"
              name="password"
              autocomplete="new-password"
              value={password()}
              onInput={(event) => setPassword(event.currentTarget.value)}
              visible={reveal()}
              onToggle={() => setReveal((shown) => !shown)}
              required
            />
            <p class="field-hint">At least 8 characters.</p>
          </div>

          <div class="field">
            <label for="confirm">Confirm password</label>
            <PasswordField
              id="confirm"
              name="confirm"
              autocomplete="new-password"
              value={confirm()}
              onInput={(event) => setConfirm(event.currentTarget.value)}
              visible={reveal()}
              onToggle={() => setReveal((shown) => !shown)}
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
              {submitting() ? "Verifying…" : "Verify & set password"}
            </button>
          </div>
        </form>
      </Show>
    </section>
  );
}
