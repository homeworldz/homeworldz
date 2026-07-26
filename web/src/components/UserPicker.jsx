import { createEffect, createSignal, For, onCleanup, Show } from "solid-js";
import { listUsers } from "../lib/api";

// A searchable account picker. Types a query, searches /admin/users, and calls
// onSelect(user | null) as the selection changes — the parent stores the user's
// id rather than having a human paste a UUID. Increment `resetKey` to clear it.
export function UserPicker(props) {
  const [query, setQuery] = createSignal("");
  const [results, setResults] = createSignal([]);
  const [selected, setSelected] = createSignal(null);
  const [open, setOpen] = createSignal(false);
  const [loading, setLoading] = createSignal(false);

  let timer;
  onCleanup(() => clearTimeout(timer));

  // Clear everything whenever the parent bumps resetKey (e.g. after a submit).
  createEffect(() => {
    props.resetKey;
    clearTimeout(timer);
    setQuery("");
    setResults([]);
    setSelected(null);
    setOpen(false);
  });

  const runSearch = async () => {
    const term = query().trim();
    if (!term) {
      setResults([]);
      setOpen(false);
      return;
    }
    setLoading(true);
    try {
      const page = await listUsers({ search: term, limit: 10 });
      setResults(page.users ?? []);
      setOpen(true);
    } catch {
      setResults([]);
      setOpen(false);
    } finally {
      setLoading(false);
    }
  };

  const onInput = (event) => {
    setQuery(event.currentTarget.value);
    // Editing invalidates any prior selection until a new one is chosen.
    if (selected()) {
      setSelected(null);
      props.onSelect?.(null);
    }
    clearTimeout(timer);
    timer = setTimeout(runSearch, 250);
  };

  const choose = (user) => {
    setSelected(user);
    setQuery(user.displayName || user.userid);
    setResults([]);
    setOpen(false);
    props.onSelect?.(user);
  };

  return (
    <div class="user-picker">
      <input
        id={props.id}
        type="search"
        role="combobox"
        aria-expanded={open()}
        aria-autocomplete="list"
        autocomplete="off"
        placeholder="Search by name or login ID"
        value={query()}
        onInput={onInput}
        onFocus={() => results().length && setOpen(true)}
        onBlur={() => setTimeout(() => setOpen(false), 150)}
      />
      <Show when={open() && results().length > 0}>
        <ul class="user-picker-results" role="listbox">
          <For each={results()}>
            {(user) => (
              <li
                role="option"
                aria-selected={selected()?.id === user.id}
                // mousedown fires before blur, so the selection registers.
                onMouseDown={(event) => {
                  event.preventDefault();
                  choose(user);
                }}
              >
                <span>{user.displayName || user.userid}</span>
                <code>{user.userid}</code>
              </li>
            )}
          </For>
        </ul>
      </Show>
      <Show when={open() && !loading() && results().length === 0 && query().trim()}>
        <p class="user-picker-empty">No accounts match that search.</p>
      </Show>
      <Show when={selected()}>
        <p class="field-hint">
          Owner: <strong>{selected().displayName || selected().userid}</strong>{" "}
          (<code>{selected().userid}</code>)
        </p>
      </Show>
    </div>
  );
}
