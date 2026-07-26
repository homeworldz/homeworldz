import { createEffect, createSignal, For, Show } from "solid-js";
import { isValidTagToken, tagList } from "../lib/classification";

// Edits an entity's classification: one mutually-exclusive kind plus a set of
// open-ended tags. props: kindOptions (string[]), knownTags (string[]),
// kind (string), tags (csv string), onSave (async (kind, csv) => void).
export function TagsEditor(props) {
  const [kind, setKind] = createSignal(props.kind);
  const [tags, setTags] = createSignal(tagList(props.tags));
  const [custom, setCustom] = createSignal("");
  const [saving, setSaving] = createSignal(false);
  const [err, setErr] = createSignal(null);
  const [msg, setMsg] = createSignal(null);

  // Resync from props whenever the saved entity changes (e.g. after a save).
  createEffect(() => {
    setKind(props.kind);
    setTags(tagList(props.tags));
  });

  const hasTag = (token) => tags().includes(token);
  const toggle = (token, on) =>
    setTags((current) =>
      on
        ? [...new Set([...current, token])]
        : current.filter((existing) => existing !== token),
    );
  const removeTag = (token) =>
    setTags((current) => current.filter((existing) => existing !== token));
  const addCustom = () => {
    const token = custom().trim().toLowerCase();
    if (!isValidTagToken(token)) {
      setErr("Tags start with a letter; use lowercase letters, digits, - or _.");
      return;
    }
    setTags((current) => [...new Set([...current, token])]);
    setCustom("");
    setErr(null);
  };

  // Current tags that aren't in the known set — shown as removable chips.
  const extraTags = () => tags().filter((token) => !props.knownTags.includes(token));

  const save = async (event) => {
    event.preventDefault();
    setErr(null);
    setMsg(null);
    setSaving(true);
    try {
      await props.onSave(kind(), tags().join(","));
      setMsg("Classification saved.");
    } catch (ex) {
      setErr(ex && ex.message ? ex.message : "Save failed.");
    } finally {
      setSaving(false);
    }
  };

  return (
    <form class="auth-card" onSubmit={save} novalidate>
      <h2>Classification</h2>

      <div class="field">
        <label>Kind</label>
        <div class="tags-kind-row">
          <For each={props.kindOptions}>
            {(option) => (
              <label class="tags-kind-option">
                <input
                  type="radio"
                  name="kind"
                  value={option}
                  checked={kind() === option}
                  onChange={() => setKind(option)}
                />
                <span>{option}</span>
              </label>
            )}
          </For>
        </div>
      </div>

      <div class="field">
        <label>Tags</label>
        <div class="tags-known-row">
          <For each={props.knownTags}>
            {(token) => (
              <label class="tags-kind-option">
                <input
                  type="checkbox"
                  checked={hasTag(token)}
                  onChange={(event) => toggle(token, event.currentTarget.checked)}
                />
                <span>{token}</span>
              </label>
            )}
          </For>
        </div>
        <Show when={extraTags().length > 0}>
          <div class="tags-chips">
            <For each={extraTags()}>
              {(token) => (
                <span class="tags-chip">
                  {token}
                  <button
                    type="button"
                    aria-label={`Remove ${token}`}
                    onClick={() => removeTag(token)}
                  >
                    ×
                  </button>
                </span>
              )}
            </For>
          </div>
        </Show>
        <div class="tags-add-row">
          <input
            type="text"
            placeholder="Add a tag"
            value={custom()}
            onInput={(event) => setCustom(event.currentTarget.value)}
            onKeyDown={(event) => {
              if (event.key === "Enter") {
                event.preventDefault();
                addCustom();
              }
            }}
          />
          <button type="button" data-variant="outline" onClick={addCustom}>
            Add
          </button>
        </div>
      </div>

      <Show when={err()}>
        <p class="form-error" role="alert">{err()}</p>
      </Show>
      <Show when={msg()}>
        <p class="form-note" role="status">{msg()}</p>
      </Show>
      <div class="auth-actions">
        <button type="submit" disabled={saving()}>
          {saving() ? "Saving…" : "Save classification"}
        </button>
      </div>
    </form>
  );
}
