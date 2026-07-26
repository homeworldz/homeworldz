import { createMemo, createSignal, Show } from "solid-js";
import { A } from "@solidjs/router";
import { ApiError, register, resendVerification } from "../lib/api";
import { deriveUserid, validateDisplayName } from "../lib/userid";

export function RegisterPage() {
  const [displayName, setDisplayName] = createSignal("");
  const [email, setEmail] = createSignal("");
  const [error, setError] = createSignal(null);
  const [submitting, setSubmitting] = createSignal(false);
  const [registered, setRegistered] = createSignal(null);
  const [resending, setResending] = createSignal(false);
  const [resendMsg, setResendMsg] = createSignal(null);
  const [resendErr, setResendErr] = createSignal(null);

  const previewUserid = createMemo(() => deriveUserid(displayName()));

  const submit = async (event) => {
    event.preventDefault();
    setError(null);

    const nameError = validateDisplayName(displayName());
    if (nameError) {
      setError(nameError);
      return;
    }
    if (!email().includes("@")) {
      setError("Enter a valid email address.");
      return;
    }

    setSubmitting(true);
    try {
      const pending = await register(displayName().trim(), email().trim());
      setRegistered(pending);
    } catch (err) {
      if (err instanceof ApiError && err.status === 409) {
        setError("That avatar name is already taken — choose a different one.");
      } else if (err instanceof ApiError && err.status === 429) {
        setError("Too many attempts. Please wait a moment and try again.");
      } else if (err instanceof ApiError) {
        setError(err.message);
      } else {
        setError("We couldn't reach the registration service. Please try again.");
      }
    } finally {
      setSubmitting(false);
    }
  };

  const resend = async () => {
    const pending = registered();
    if (!pending) {
      return;
    }
    setResendErr(null);
    setResendMsg(null);
    setResending(true);
    try {
      await resendVerification(pending.userid);
      setResendMsg("Sent again. Check your inbox in a moment.");
    } catch (err) {
      if (err instanceof ApiError && err.status === 429) {
        setResendErr("Too many attempts. Please wait a moment and try again.");
      } else {
        setResendErr("We couldn't resend the email. Please try again.");
      }
    } finally {
      setResending(false);
    }
  };

  return (
    <section class="auth-page" aria-labelledby="register-title">
      <Show
        when={!registered()}
        fallback={
          <div class="auth-card">
            <h1 id="register-title">Check your email</h1>
            <p class="lede">
              We've sent a verification link to <strong>{email().trim()}</strong>.
              Follow it to set your password and activate{" "}
              <strong>{registered()?.displayName}</strong> (login ID{" "}
              <code>{registered()?.userid}</code>).
            </p>
            <p class="auth-alt">
              Didn't get it? Check your spam folder,{" "}
              <button
                type="button"
                class="link-button"
                onClick={resend}
                disabled={resending()}
              >
                {resending() ? "resending…" : "resend the email"}
              </button>
              , or <A href="/register">start over</A>.
            </p>
            <Show when={resendMsg()}>
              <p class="form-note" role="status">
                {resendMsg()}
              </p>
            </Show>
            <Show when={resendErr()}>
              <p class="form-error" role="alert">
                {resendErr()}
              </p>
            </Show>
          </div>
        }
      >
        <form class="auth-card" onSubmit={submit} novalidate>
          <h1 id="register-title">Create your avatar</h1>
          <p class="lede">
            Register a Homeworldz avatar. You'll set a password from the link we
            email you.
          </p>

          <div class="field">
            <label for="displayName">
              Avatar Display Name (not your real name)
            </label>
            <input
              id="displayName"
              name="displayName"
              type="text"
              autocomplete="off"
              placeholder="First Last"
              value={displayName()}
              onInput={(event) => setDisplayName(event.currentTarget.value)}
              required
            />
            <p class="field-hint">
              Two words — a first and last name.
              <Show when={previewUserid()}>
                {" "}
                Your login ID will be <code>{previewUserid()}</code>.
              </Show>
            </p>
          </div>

          <div class="field">
            <label for="email">Email address</label>
            <input
              id="email"
              name="email"
              type="email"
              autocomplete="email"
              placeholder="you@example.com"
              value={email()}
              onInput={(event) => setEmail(event.currentTarget.value)}
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
              {submitting() ? "Registering…" : "Register"}
            </button>
          </div>

          <p class="auth-alt">
            Already have an avatar? <A href="/login">Login</A>.
          </p>
        </form>
      </Show>
    </section>
  );
}
