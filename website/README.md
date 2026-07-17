# HomeWorldz website

The SolidJS marketing and account-management frontend for HomeWorldz.

## Development

```sh
pnpm install
pnpm dev
```

The Roadmap and Architecture routes render their source content directly from
the parent project's `docs/` directory at build time.

## Cloudflare Pages

Connect the parent HomeWorldz repository and use these Pages build settings:

- Root directory: `website`
- Build command: `pnpm build`
- Build output directory: `dist`

The `public/_redirects` rule sends direct requests for Solid Router paths to
`index.html`. The repository's parent `docs/` directory must remain available
to the Pages build because the topic routes import Markdown from it.
