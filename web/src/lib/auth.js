import { createSignal } from "solid-js";

const STORAGE_KEY = "homeworldz.auth";

function load() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) {
      return null;
    }
    const parsed = JSON.parse(raw);
    if (!parsed || !parsed.accessToken || !parsed.expiresAt) {
      return null;
    }
    return parsed;
  } catch {
    return null;
  }
}

const [session, setSession] = createSignal(load());

function isExpired(entry) {
  return !entry || new Date(entry.expiresAt).getTime() <= Date.now();
}

// Reactive: reading this inside a component tracks login/logout changes.
export function currentSession() {
  const entry = session();
  return isExpired(entry) ? null : entry;
}

export function isAuthenticated() {
  return currentSession() !== null;
}

export function getToken() {
  const entry = currentSession();
  return entry ? entry.accessToken : null;
}

export function currentIdentity() {
  const entry = currentSession();
  return entry ? entry.identity : null;
}

// Accepts a TokenResponse ({ accessToken, expiresAt, identity }).
export function login(tokenResponse) {
  const next = {
    accessToken: tokenResponse.accessToken,
    expiresAt: tokenResponse.expiresAt,
    identity: tokenResponse.identity,
  };
  localStorage.setItem(STORAGE_KEY, JSON.stringify(next));
  setSession(next);
}

export function logout() {
  localStorage.removeItem(STORAGE_KEY);
  setSession(null);
}
