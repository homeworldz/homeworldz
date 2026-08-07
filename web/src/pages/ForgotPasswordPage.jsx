import { createSignal, Show } from "solid-js";
import { A } from "@solidjs/router";
import { ApiError, requestPasswordReset } from "../lib/api";

export function ForgotPasswordPage() {
  const [identifier, setIdentifier] = createSignal("");
  const [sent, setSent] = createSignal(false);
  const [error, setError] = createSignal(null);
  const [submitting, setSubmitting] = createSignal(false);

  const submit = async (event) => {
    event.preventDefault();
    setError(null);
    if (identifier().trim() === "") {
      setError("Enter your avatar name, for example first.last or First Last.");
      return;
    }

    setSubmitting(true);
    try {
      await requestPasswordReset(identifier().trim());
      setSent(true);
    } catch (err) {
      // Only conditions that say nothing about any account are reported. A
      // rejected identifier is a problem with what was typed; a rate limit is
      // about this browser. Anything else — including a server fault — falls
      // through to the same confirmation as success, because the endpoint
      // answers 202 for every account outcome and showing a different screen
      // here would re-create the account oracle it exists to prevent
      // (ADR 0034).
      if (err instanceof ApiError && err.status === 400) {
        setError("Enter your avatar name, for example first.last or First Last.");
      } else if (err instanceof ApiError && err.status === 429) {
        setError("Too many requests. Please wait a moment and try again.");
      } else {
        setSent(true);
      }
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <section class="auth-page" aria-labelledby="forgot-title">
      <Show
        when={!sent()}
        fallback={
          <div class="auth-card">
            <h1 id="forgot-title">Check your email</h1>
            {/* Deliberately says "if ... exists": this screen is shown whether or
                not an account matched, so it must not imply one did. */}
            <p class="lede">
              If an account exists for that name or address, a reset link is on
              its way to the email address on file. The link works once and
              expires in an hour.
            </p>
            <p class="lede">
              Nothing has changed yet, and you are still signed in anywhere you
              already were.
            </p>
            <p class="auth-alt">
              <A href="/login">Back to login</A>
            </p>
          </div>
        }
      >
        <form class="auth-card" onSubmit={submit} novalidate>
          <h1 id="forgot-title">Reset your password</h1>
          <p class="lede">
            Enter your avatar name and we will email you a link to choose a new
            password, at the address on that account.
          </p>

          <div class="field">
            <label for="identifier">Avatar name</label>
            <input
              id="identifier"
              name="identifier"
              type="text"
              autocomplete="username"
              autocapitalize="none"
              spellcheck={false}
              maxlength="254"
              value={identifier()}
              onInput={(event) => setIdentifier(event.currentTarget.value)}
              required
            />
            <p class="field-hint">For example first.last, or First Last.</p>
          </div>

          <Show when={error()}>
            <p class="form-error" role="alert">
              {error()}
            </p>
          </Show>

          <button type="submit" class="primary" disabled={submitting()}>
            {submitting() ? "Sending…" : "Email me a reset link"}
          </button>

          <p class="auth-alt">
            Remembered it? <A href="/login">Back to login</A>
          </p>
        </form>
      </Show>
    </section>
  );
}
