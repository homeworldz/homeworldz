import { createSignal, Show } from "solid-js";
import { A, useNavigate, useParams } from "@solidjs/router";
import { ApiError, consumePasswordReset } from "../lib/api";
import { PasswordField } from "../components/PasswordField";

export function ResetPasswordPage() {
  const params = useParams();
  const navigate = useNavigate();
  const [password, setPassword] = createSignal("");
  const [confirm, setConfirm] = createSignal("");
  const [reveal, setReveal] = createSignal(false);
  const [error, setError] = createSignal(null);
  const [submitting, setSubmitting] = createSignal(false);
  const [done, setDone] = createSignal(false);

  const token = () => params.token;

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
      await consumePasswordReset(token(), password());
      // No token is issued here, unlike verification: a reset proves the
      // account holder had access to the email, not that they want a session in
      // this browser. They sign in with the new password like anyone else.
      setDone(true);
    } catch (err) {
      // Unknown, already-used and expired tokens arrive as one 400 on purpose,
      // so they get one message. Guessing which applied would say whether the
      // token had ever existed (ADR 0034).
      if (err instanceof ApiError && err.status === 400) {
        setError("This reset link is invalid or has expired. Request a new one.");
      } else if (err instanceof ApiError && err.status === 429) {
        setError("Too many attempts. Please wait a moment and try again.");
      } else if (err instanceof ApiError) {
        setError(err.message);
      } else {
        setError("Something went wrong. Please try again.");
      }
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <section class="auth-page" aria-labelledby="reset-title">
      <Show
        when={token()}
        fallback={
          <div class="auth-card">
            <h1 id="reset-title">Missing reset link</h1>
            <p class="lede">
              This page needs the link from your reset email. Open that link, or{" "}
              <A href="/forgot">request a new one</A>.
            </p>
          </div>
        }
      >
        <Show
          when={!done()}
          fallback={
            <div class="auth-card">
              <h1 id="reset-title">Password changed</h1>
              <p class="lede">
                You can now login with your new password, here and in-world.
                Anywhere you were already signed in stays signed in.
              </p>
              <button
                type="button"
                class="primary"
                onClick={() => navigate("/login", { replace: true })}
              >
                Go to login
              </button>
            </div>
          }
        >
          <form class="auth-card" onSubmit={submit} novalidate>
            <h1 id="reset-title">Choose a new password</h1>
            <p class="lede">
              This link works once. You will use the new password to login here
              and in-world.
            </p>

            <div class="field">
              <label for="password">New password</label>
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
              <label for="confirm">Confirm new password</label>
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

            <button type="submit" class="primary" disabled={submitting()}>
              {submitting() ? "Saving…" : "Set new password"}
            </button>

            <p class="auth-alt">
              <A href="/login">Back to login</A>
            </p>
          </form>
        </Show>
      </Show>
    </section>
  );
}
