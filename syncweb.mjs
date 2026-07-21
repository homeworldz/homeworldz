// Syncs source-of-truth files from this repo into the sibling homeworldz.com
// website repo, so the Cloudflare Pages build (which only checks out that repo)
// bundles current copies. Run manually from this repo root: `node syncweb.mjs`.
//
// Changed files are left UNSTAGED in homeworldz.com for you to review, commit,
// and push (pushing homeworldz.com is what triggers the Pages deploy). No-ops
// when the sibling website repo is not present next to this one.
//
// Add entries to `files` as more content needs to travel from this repo to the
// website. Line endings are normalized to LF to match the website repo and
// avoid spurious working-tree churn.

import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(repoRoot, "../homeworldz.com");

// { from } is relative to this repo; { to } is relative to homeworldz.com.
const files = [{ from: "docs/ROADMAP.md", to: "content/ROADMAP.md" }];

const toLf = (text) => text.replace(/\r\n/g, "\n");

if (!existsSync(webRoot)) {
  console.log(`[syncweb] Sibling homeworldz.com not found (${webRoot}); nothing to do.`);
  process.exit(0);
}

let changed = 0;
for (const { from, to } of files) {
  const source = resolve(repoRoot, from);
  const dest = resolve(webRoot, to);
  if (!existsSync(source)) {
    console.warn(`[syncweb] Source missing, skipped: ${from}`);
    continue;
  }
  const sourceText = toLf(readFileSync(source, "utf8"));
  if (existsSync(dest) && toLf(readFileSync(dest, "utf8")) === sourceText) {
    console.log(`[syncweb] up to date: ${to}`);
    continue;
  }
  mkdirSync(dirname(dest), { recursive: true });
  writeFileSync(dest, sourceText);
  console.log(`[syncweb] updated:   ${to}  (from ${from})`);
  changed += 1;
}

console.log(
  changed > 0
    ? `[syncweb] ${changed} file(s) updated in homeworldz.com — review, commit, and push to deploy.`
    : "[syncweb] everything already in sync.",
);
